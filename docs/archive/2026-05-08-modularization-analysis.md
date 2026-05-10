---
status: complete
completed: 2026-05-08
---

# Deep Modules — design log (archived)

A multi-session exercise to identify Deep Modules (Ousterhout, *A Philosophy of Software Design*) in the SIPI codebase, using the ubiquitous language as a probe and extending the language as gaps surface. The output is the input list and seam shape for the Bazel package layout introduced by [ADR-0003](../adr/0003-module-co-located-source-and-tests.md).

**This is a frozen artifact of a one-time exercise. Completed 2026-05-08.** The probe register (Probes 1-10), glossary delta register (~22 entries flushed to [`UBIQUITOUS_LANGUAGE.md`](../../UBIQUITOUS_LANGUAGE.md) on completion), method invariants, and closing-sweep findings are durable architectural records. The implementation work is tracked as Linear issues under the SIPI Modularization project; see [tracked separately] for the Y+8 rollout sequence.

**Outcome:** 13 SIPI-side Bazel packages + 1 binary, plus the separate `shttps/` context. `include/` directory deleted entirely (per Y+8d). Hypothesis (every glossary term corresponds to either a deep module, a scattered concept to extract, or a language-only term) confirmed — see Probe 10's closing sweep.

## Goal

Decide which Bazel packages SIPI should ship as, by treating each candidate package as a Deep Module: a directory whose public-header surface is small relative to the complexity hidden behind it. The deliverables, accumulated across sessions, are:

1. A list of Deep Module candidates with verdicts (deep / shallow / scattered / mis-bounded / god-object) and concrete actions (keep / split / merge / extract / rename).
2. A patch list against [`UBIQUITOUS_LANGUAGE.md`](../../UBIQUITOUS_LANGUAGE.md) for terms to add, sharpen, or retire — surfaced as side-effects of the module probes.
3. A short list of follow-up ADRs for decisions that meet the bar (hard to reverse, surprising without context, real trade-off).

## Scope

**Unit of "module" = Bazel package.** Per ADR-0003, a SIPI module is a directory under `src/` with co-located `.cpp` / `.h` / `*_test.cpp` and its own `BUILD.bazel` declaring a `cc_library` with explicit `hdrs` and `visibility`. The module's interface is the headers in `hdrs`; its depth is what sits behind them.

This unit was chosen over two alternatives:

- **Class-level** (e.g. is `SipiCache` itself deep?). Useful follow-up *inside* a package; not the load-bearing decision. Falls out from getting package boundaries right.
- **Context-level** (split or merge bounded contexts). Already operated on via [`CONTEXT-MAP.md`](../../CONTEXT-MAP.md) (SIPI ↔ shttps); a third context is a separate decision tracked by [ADR-0001](../adr/0001-shttps-as-strangler-fig-target.md).

**Why this matters now.** ADR-0003's reframing: AI-throughput coding has shifted the human's role from writing code to defining and policing architecture. Bazel's `cc_library` + `--strict_deps` + `package_group` + `visibility` turns the package list into build-graph invariants — a forbidden `#include` fails analysis, not code review. Picking the right package list is therefore a leverage point: every package boundary we draw is a rule the build will then enforce automatically.

## Hypothesis

> Every term in [`UBIQUITOUS_LANGUAGE.md`](../../UBIQUITOUS_LANGUAGE.md) should correspond to either:
>
> 1. a Deep Module that already exists in code (verdict: **deep, keep**),
> 2. a Deep Module that should exist but is scattered across files today (verdict: **scattered, extract**),
> 3. a pure vocabulary item with no module shape (verdict: **language-only**).
>
> Plus a fourth bucket: **modules in code with no glossary term** — these reveal where the language must be *extended*.

The (2)-bucket cases are the highest-value refactors: the language predicts a module the code hasn't built yet. The (1)-bucket cases tell us which boundaries are healthy and should be promoted to Bazel packages as-is. The (3)-bucket cases sharpen the language. The fourth bucket is where this exercise feeds back into the glossary.

## Methodology — probe-and-extend

Chosen over sweep-first.

- **Sweep-first** (rejected): walk the codebase systematically, surface every concept lacking a glossary name, complete the language, *then* run the completed glossary against the code to find Deep Modules. Thorough; long; risks producing a list disconnected from refactor priorities.
- **Probe-and-extend** (chosen): take the existing glossary as a partial map, run each term against the code, and *whenever a gap forces itself on us* — "I can't classify this because we don't have a name for X" — add X to the glossary in the same pass. Concepts get named in the order they matter for the Deep Module decision.

Mitigation for (2)'s blind spots: at the end of the term-driven probes, do a short directory-listing-only sweep to confirm no top-level subsystem went un-touched.

## Probe template

Each probe produces one row in the [Probe register](#probe-register) below. Fields:

| Field | What it captures |
| --- | --- |
| **1. Module name** | The future Bazel package (directory under `src/`). |
| **2. Glossary term(s)** | Which `UBIQUITOUS_LANGUAGE.md` term(s) the module implements; flagged if the term is missing. |
| **3. Public interface** | The small set of headers proposed for `hdrs` — what callers see. List the headers, not the symbols. |
| **4. Private surface** | What stays inside: private headers + `.cpp` files. Just the count + a one-line note on what they implement. |
| **5. Depth signal** | Ousterhout-style note: does the interface hide complexity, or pass it through? (Pass-through methods, leaked types, large `hdrs` count, callers needing internal knowledge — all shallow signals.) Include a rough public:private ratio. |
| **6. Verdict** | One of: `deep` / `shallow` / `scattered` / `mis-bounded` / `god-object` / `language-only`. |
| **7. Action** | Concrete next step: `keep` / `split into A,B,C` / `merge with X` / `extract X from Y` / `rename to Z`. Include the rationale in one sentence. |
| **8. Glossary delta** | Terms to add, sharpen, or retire as a side-effect of this probe. Empty if none. |

Probe rows accumulate; they are not rewritten as the analysis progresses (history of thinking is part of the artifact). If a later probe revises an earlier verdict, append a `**Revised:** <date> — …` line to the original row rather than editing the verdict in place.

## Probe order

Chosen to validate the template on easy ground first, then test gap-detection, then tackle the worst offender once vocabulary is loaded:

1. **`SipiCache`** — likely textbook deep module (small public surface: store/fetch/evict; deep internals: dual-limit LRU + crash recovery). Validates the template.
2. **Identifier resolution / path traversal validation** — scattered-concept candidate. Glossary mentions it inline (`Image root` / `Identifier` definitions) but no obvious file owns it. Tests the (2)-bucket detector.
3. **`SipiImage` (~80 KB hpp+cpp)** — prime god-object suspect. Highest leverage; messiest probe. By this point we should have enough vocabulary to decompose it without flailing.
4. **Subsystem directories that already exist as candidates**: `formats/`, `handlers/`, `iiifparser/`, `metadata/`. Likely already module-shaped; the probe confirms or sharpens their boundaries.
5. **`SipiHttpServer` (~84 KB)** — second-largest file. Probably mostly route logic that should live in `handlers/`; what's left is the server lifecycle.
6. **`SipiLua` (~60 KB)** — large, but bounded by the Lua FFI surface. Probably one module; the question is whether it splits along the three Lua entry points (Init / Preflight / Route handler).
7. **`Backpressure` cluster**: `SipiMemoryBudget` + `SipiRateLimiter` + (admission gate). Glossary defines `Backpressure` as the umbrella — the module probably exists in concept but is split across files.
8. **Curiosities with no glossary term**: `PhpSession`, `Salsah`, `Template`, `SipiReport`, `Logger`. Determine which are live, which are dead, which need a glossary term.
9. **Operational surface**: `SipiMetrics`, `SipiSentry`, `SipiPeakMemory`, `SipiConnectionMetricsAdapter`, config (`SipiConf`). May coalesce into one observability module.
10. **Top-level**: `sipi.cpp` (entry point, ~63 KB) — the CLI vs server-mode split. Likely thin once the rest is modularized; confirm.

After step 10, the closing sweep: list every directory and loose file under `src/` and confirm each has been classified.

## Probe register

Rows are added as probes complete. Format: one section per probe with the 8 template fields.

<!-- BEGIN PROBE ROWS -->

## Probe 1 — `SipiCache`

**1. Module name.** `src/cache/{cache.h, cache.cpp, cache_test.cpp}` (currently top-level `src/SipiCache.{cpp,h}` + `include/SipiCache.h`). Future Bazel package `//src/cache:cache`.

**2. Glossary terms.** Implements **Cache** (defined). Uses **Cache key** + **Canonical URL** (defined). Surfaces three glossary gaps — see [Glossary delta register](#glossary-delta-register).

**3. Public interface (proposed `hdrs`).** `cache.h` exposing class `Cache` with:
- `Cache(cachedir, max_size_bytes, max_files)` / `~Cache`
- `check(origpath, canonical) → BlockedScope` (RAII pin; replaces `block_file=true` + manual `deblock`)
- `add(origpath, canonical, cachepath, img_w, img_h, tile_w, tile_h, clevels, numpages)`
- `remove(canonical) → bool`
- `purge → int`
- `getNewCacheFileName → string`
- `loop(worker, userdata, sort_method)` — admin/Lua iteration
- `stats → Stats` — collapses the five getters
- Public types kept: `CacheRecord` (callback contract for `loop`), `SortMethod`, `ProcessOneCacheFile`, `Stats`, `BlockedScope`.

**4. Private surface.** `cache.cpp` + per-module unit test. Internals retained (not in `hdrs`):
- LRU eviction with dual-limit (size + file-count) and 80% low-water mark.
- Crash recovery: orphan scan when `.sipicache` is missing; index-corruption detection (size %% record-size); first-time directory creation.
- Persistence: `FileCacheRecord` on-disk format (now private), serialize on shutdown, deserialize on startup.
- Concurrency: one mutex + atomic counters for cache-used-bytes / nfiles.
- Pinned-file refcount map (`blocked_files`); `BlockedScope` owns the lifecycle.
- `tcompare` (timestamp comparison) — moved from public to private.
- `clearCacheDir` static helper.
- `_compare_access_time_*` / `_compare_fsize_*` static comparators.

**5. Depth signal.** Core is a genuine deep module — dual-limit LRU + crash recovery + atomic eviction with low-water mark + persistence + concurrency, all behind a small conceptual surface. Today's `include/SipiCache.h` has four shallowness leaks that the proposed `hdrs` removes:
- `tcompare` is public (pure internal utility).
- `check(block_file=true)` paired with `deblock(name)` — manual pairing, locking concern leaks (RAII `BlockedScope` fixes this).
- `getCacheDir` exposes filesystem layout to Lua admin scripts (audit during `SipiLua` probe; either remove or fold into `stats`).
- Five separate getters (`getCacheUsedBytes`, `getMaxCacheSize`, `getNfiles`, `getMaxNfiles`, `getCacheDir`) → one `Stats stats` struct.
- `FileCacheRecord` (on-disk `char[256]` format) is in the public header but only `SipiCache` itself uses it.

Plus one larger finding (see #6/#7) — the cache held two responsibilities, and the second is being amputated entirely rather than extracted.

**6. Verdict.** `deep` post-refactor; today, `deep with shallow leaks and a co-located non-cache responsibility`.

**7. Action.**

a. **Promote to Bazel package** per ADR-0003. Co-locate `cache_test.cpp` from current `test/unit/cache/cache.cpp`.

b. **Tighten public interface** per #3: RAII `BlockedScope`, collapse getters to `Stats stats`, privatize `FileCacheRecord` and `tcompare`. Mechanical; updates `SipiHttpServer.cpp` and `SipiLua.cpp` call sites.

c. **Amputate the image-shape responsibility** — delete `SizeRecord`, `sizetable`, and `getSize` from `SipiCache`. The two structs (`SizeRecord` and `CacheRecord`) overlap because they hold the same data with different keys; `sizetable` was a parasitic side-effect index populated only by `add`, never independently persisted, surviving eviction (`purge` and `remove` don't clean it up — bug, made moot by the deletion), and never populated for un-cached origpaths. It was barely a cache. Image shape lookup moves to format handlers — see [ADR-0004](../adr/0004-image-shape-ownership.md).

d. **Audit the Lua admin surface** during the `SipiLua` probe: `SipiLua.cpp` currently uses `getCacheDir`, `loop`, `remove`, `purge`, `add`. Decide whether the Lua-facing API is the same as the C++ public API or a thin facade. The `getCacheDir` exposure is the clearest case for separation.

**8. Glossary delta.** See [Glossary delta register](#glossary-delta-register) for the full set. Summary:
- Add **Image shape** — intrinsic shape of a source image; read by format handlers.
- Add **Operating mode** with **Server mode** + **CLI mode** sub-terms — the asymmetry between which format handler reads vs. writes is architecturally load-bearing.
- Add **Cache pin** (provisional) — confirm during `SipiHttpServer` probe.
- Sharpen **Essentials packet** — extend schema with image-shape fields per ADR-0004.

**9. Open questions for later probes.**
- `getCacheDir`'s removal or retention depends on the Lua admin surface (Probe `SipiLua`).
- The exact set of fields in `Stats` depends on what the `/metrics` endpoint and `SipiLua` consumers actually need (Probes `Operational surface` and `SipiLua`).
- `BlockedScope`'s API — pure RAII, or does it expose `blocked` for the caller to detect "all-blocked, can't add"? — depends on Probe `SipiHttpServer` flow analysis.

## Probe 2 — `metadata/`

**1. Module name.** `src/metadata/` (existing). Future Bazel package `//src/metadata:metadata`. One package, not split per glossary umbrella — the language distinguishes Embedded metadata vs. Essentials packet, but in code they share consumers (every format handler + `SipiImage`) and a real layering boundary would not pay back the visibility/dep multiplication.

**2. Glossary terms.** Implements the **Preservation metadata** umbrella with both subordinate parts: **Embedded metadata** (EXIF, IPTC, XMP, ICC) via `SipiExif`/`SipiIptc`/`SipiXmp`/`SipiIcc`, and **Essentials packet** via `SipiEssentials`. Surfaces two glossary deltas — one sharpening, one new term — see register.

**3. Public interface (proposed `hdrs`).**

| Header | Class | Concern |
| --- | --- | --- |
| `metadata/essentials.h` | `SipiEssentials` | SIPI-owned preservation packet schema |
| `metadata/exif.h` | `SipiExif` | EXIF wrapper (exiv2) |
| `metadata/icc.h` | `SipiIcc` | ICC profile wrapper (lcms2) — `iccBytes` chokepoint per ADR-0002 |
| `metadata/iptc.h` | `SipiIptc` | IPTC wrapper (exiv2) |
| `metadata/xmp.h` | `SipiXmp` | XMP wrapper (exiv2) |

The umbrella structure shows up as header organisation, not as separate Bazel packages.

**4. Private surface.** `metadata/internal/icc_normalization.h` (formerly `SipiIccDetail.h`) with `visibility = ["//src/metadata:__pkg__", "//test/unit/sipiicc:__pkg__"]`. Plus the `.cpp` files. Test seam pattern documented (see glossary delta).

**5. Depth signal.** Mixed at the per-class level:

| Class | hdr / cpp lines | Verdict | Reason |
| --- | --- | --- | --- |
| `SipiIcc` | 175 / 664 | **deep** | 1:4 ratio; `iccBytes` chokepoint is the textbook Ousterhout hidden-complexity case (per ADR-0002) |
| `SipiXmp` | 93 / 166 | reasonable | depth in exiv2 RDF/XML wrapping |
| `SipiIptc` | 66 / 59 | reasonable | small but bounded |
| `SipiEssentials` | 209 / 213 | **shallow** | 1:1 ratio; inline `operator<<`, inline `operator std::string`, 17 getter/setter pairs, pipe-delimited serialization in header |
| `SipiExif` | **321 / 150** | **shallow** | header > cpp; 22 inline `assign_val` template overloads + `typeid`-dispatched template `addKeyVal` pull the entire exiv2 type universe into every consumer's TU |

Plus one module-level leak: **third-party headers (`<exiv2/...>`, `<lcms2.h>`) appear in every public metadata header**. Every format handler, `SipiImage.hpp`, and `SipiSentry.h` get exiv2 + lcms2 in their compilation graph and see raw `Exiv2::`/`cms*` types in method signatures. Bazel `--strict_deps` will require either re-exporting these as transitive deps (anti-pattern) or hiding them via pImpl. One fix at the metadata boundary pays back at every consumer.

**6. Verdict.** Module-level `deep` (foundation layer of image processing — many consumers, real third-party-binding depth), with `shallow` leaks at the per-class level (Exif, Essentials) and a third-party-type leakage at the public-header boundary.

**7. Action.**

a. **Promote to Bazel package** `//src/metadata:metadata` per ADR-0003. Co-locate `*_test.cpp` from current `test/unit/sipiicc/`.

b. **Move `SipiIccDetail.h` → `metadata/internal/icc_normalization.h`** with `visibility` restricted to the package itself + the ICC unit test target. Use as the canonical **Test seam** reference in the glossary.

c. **Refactor `SipiExif` header**: move all inline `assign_val` and template `addKeyVal` definitions to `.cpp` with explicit instantiations for the types we use (`std::string`, `int`/`long`/`float`/`double` and their vectors, `Exiv2::Rational`/`URational`). Replace `typeid` dispatch with C++20 concepts or explicit specialization (matches the Rust-alignment direction discussed earlier). Hide `Exiv2::Rational` behind a SIPI-defined `Rational = std::pair<int32_t, int32_t>` if it doesn't break call sites materially.

d. **Refactor `SipiEssentials`**: tighten public interface to one `parse(bytes) → SipiEssentials` (free function or static factory) + one `serialize → std::vector<unsigned char>` + a small struct of accessors. Drop the inline string-conversion operators. Adopt versioned binary wire format per ADR-0005. Add image-shape fields per ADR-0004 (which lands inside the new format, not the legacy one).

e. **Modernize C-pointer ownership across the module**: drop the dual-overload pattern (`unsigned char* xxxBytes(unsigned int& len)` + `std::vector<unsigned char> xxxBytes`); keep only the vector form. Matches `cpp-style-guide.md` "no raw owning new/delete" and aligns with Rust's no-raw-pointer-ownership rule.

f. **Defer**: `SipiImage`-`SipiIcc` friend-class coupling (Probe 3); `SipiSentry::SipiIcc` dependency (Probe 9 — possibly vestigial); Lua-exposure surface (Probe 6).

**8. Glossary delta.** See [Glossary delta register](#glossary-delta-register). Two adds:
- **Sharpen `Essentials packet`** — note pipe-delimited fragility + planned versioned-binary successor per ADR-0005.
- **Add `Test seam`** — header in module's `internal/` subdirectory with restricted visibility.

**9. Open questions for later probes.**
- The `SipiImage.hpp:645` `friend class SipiIcc` — necessary coupling, or removable when `iccFormatter(SipiImage*)` is split? Probe 3 (`SipiImage`).
- `include/SipiSentry.h` includes `metadata/SipiIcc.h` — what does Sentry need from ICC? Probably context-data attachment for crash reports; might be vestigial. Probe 9.
- Lua admin surface (`SipiLua`) — does it directly manipulate metadata, or only consume it through `SipiImage`? Probe 6.
- ADR-0005 implementation choice between `tinycbor` / `jsoncons` / a small in-tree CBOR encoder — defer to implementation time; not an architectural decision.

## Probe 4 — `SipiImage` (god-object decomposition)

**1. Module name.** Two new Bazel packages, splitting the current `src/SipiImage.{cpp,hpp}` (~2,526 lines combined):

| New package | Concern |
| --- | --- |
| `src/image/` | Pure value type: geometry, photometric, RAII pixel buffer, metadata composite. ~15 public methods. |
| `src/image_processing/` | Free functions over `const Image&`: crop, scale, rotate, colour convert, channel ops, bit-depth reduction, dithering, watermark application, comparison, arithmetic. |

Plus three concerns leave the class entirely:
- Static `io` registry → `format_handlers/` (registry of self).
- `shttps::Connection*` field → disappears (subsumed by `OutputSink::HttpSink` per ADR-0006).
- `app14_transform` JPEG marker field → moves into the JPEG decode pipeline (consumed at decode, inverted before `Image` is "complete").

**2. Glossary terms.** Implements **Image** (the code-level class; domain-level term unchanged). Surfaces **Image processing** as a new umbrella term. Touches `metadata/`, `format_handlers/`, and the shttps boundary.

**3. Public interface (proposed `hdrs`).**

```
src/image/image.h — class Image (geometry, pixel access, metadata accessors, RAII pixel buffer)
src/image/image_metadata.h — ImageMetadata composite (5 standards bundle; possibly inlined into Image)

src/image_processing/crop.h
src/image_processing/scale.h — one scale taking ScalingQuality; replaces 3 named methods
src/image_processing/rotate.h
src/image_processing/color_convert.h — convertYCC2RGB, convert_to_icc
src/image_processing/channel_ops.h — remove_channel, remove_extra_samples
src/image_processing/bit_depth.h — to_8bps, to_bitonal
src/image_processing/watermark.h — DEFERRED to Probe 5 / Watermark glossary entry
src/image_processing/arithmetic.h — operator-, operator+, operator==, compare (or test-only)
```

**4. Private surface.** `.cpp` files implementing the above. The IO map / format dispatch moves to `format_handlers/`. The 5 friend classes (4 format handlers + `SipiIcc`) all go away — replaced by public `pixels_writable` API + metadata setters + 1-2 new accessors for `iccFormatter`. The raw `byte *pixels` becomes `std::vector<byte>` (or `unique_ptr<byte[]>` if benchmarking shows any regression — the audit PR decides).

**5. Depth signal.** Six distinct responsibility groups in one class — textbook god-object:

| Group | Surface |
| --- | --- |
| Image data container | `nx, ny, nc, bps, pixels (raw!), es, orientation, photo` |
| Metadata holder | 4 `shared_ptr<Sipi{Exif,Icc,Iptc,Xmp}>` + `SipiEssentials` value |
| Format I/O facade | static `io` map + `read` / `readOriginal` / `write` / `getDim(filepath)` |
| Image processing | `crop`, `scale`/`scaleFast`/`scaleMedium`, `rotate`, `convertYCC2RGB`, `convertToIcc`, `removeChannel`, `removeExtraSamples`, `to8bps`, `toBitonal`, `add_watermark`, `set_topleft` (12 methods) |
| HTTP integration | `conobj` raw `shttps::Connection*` + `connection` accessors |
| Algebra / comparison | `operator-=`, `operator-`, `operator+=`, `operator+`, `operator==`, `compare`, `operator<<` |

Plus **specific code smells**:
- Raw `byte *pixels` (manual `new[]`/`delete[]`; reason for explicit deep-copy + move semantics; violates `cpp-style-guide.md`'s "no raw owning new/delete").
- 5 `friend class` declarations (encapsulation deliberately broken).
- `app14_transform` field — JPEG-specific Adobe APP14 marker on the universal type.
- Static `io` registry — misplaced concern (registry of format handlers belongs in `format_handlers/`).
- Three scale methods (`scale`, `scaleMedium`, `scaleFast`) — separate names for the same operation at different qualities; unstable API.
- Two `getDim` methods with different semantics (filepath query vs. instance state out-params).
- Inconsistent metadata accessors (`getExif`, `getIcc`, `getXmp` exist; **no `getIptc`** despite `iptc` member).
- Heavy header includes — every consumer transitively pulls shttps + format-handler types + 5 metadata + exiv2 + lcms2.

**6. Verdict.** **`god-object`** — unambiguous. Highest-leverage decomposition target in the codebase.

**7. Action.** Per [ADR-0007](../adr/0007-sipiimage-decomposition.md). Eight staged sub-PRs (each independently reversible), tracked under a new Linear parent issue:

a. **Audit** every internal use of `pixels` (catalog read / write / pointer-pass / arithmetic / `new[]`/`delete[]` cases). Output is a doc + benchmark; no code change. Gates the rest.

b. **Replace `byte *pixels` with `std::vector<byte>`**. RAII; eliminates the explicit copy/move/dtor dance. Performance verified at parity in (a)'s benchmark.

c. **Remove `app14_transform` field**. JPEG handler inverts CMYK/YCCK at decode time; downstream sees standard CMYK.

d. **Move static `io` map** to `format_handlers/`. SipiImage stops being a registry of format handlers.

e. **Remove `conobj` field + `connection` accessors**. Coupled with [tracked separately] (OutputSink with TeeSink for dual-write — see ADR-0006 / ADR-0007).

f. **Add public `pixels_writable` API + metadata setters**; remove the 4 format-handler friend declarations.

g. **Extract image-processing methods** to `src/image_processing/` free functions over `const Image&`. ~12 method-to-free-function rewrites at every call site.

h. **Remove `SipiIcc` friend** (probably needs 1-2 new public Image accessors so `iccFormatter` works without internal access). Move arithmetic operators (`operator-`, `operator+`, `operator==`, `compare`) to free functions in `image_processing/arithmetic.{h,cpp}` or test-only.

Bazel package promotion (steps d, f, g specifically) gated on [tracked separately] reaching Y+6. Steps a, b, c can land in CMake era.

**8. Glossary delta.** Add **Image processing** (umbrella for the free-function module). Sharpen **Image** (the code-level class becomes a narrow value type post-refactor; domain term stays correct).

**9. Open questions for later probes.**

- **Lua-binding surface** ([SipiLua.cpp](../../src/SipiLua.cpp), Probe 6): the Lua API likely exposes `image:crop(...)` style method-call syntax; if so, the Lua binding layer absorbs the C++-method-to-free-function translation transparently. May require keeping thin facade methods on `Image` purely for binding ergonomics. Probe 6 resolves.
- **Watermark module** (Probe 5): the watermark loading + applying logic might live entirely in route handlers (close to where `Permission` decides to apply it) rather than in `image_processing/`. Defer placement.
- **`ImageMetadata` composite** — own type or just members of `Image`? Decide during implementation; tightly bundled either way.
- **TeeSink composition** preserves the dual-write optimization (encoder writes to HTTP socket *and* cache file simultaneously). Documented in ADR-0006 + ADR-0007; implemented in .

## Probe 3 — `format_handlers/` (renamed from `formats/`)

**1. Module name.** `src/format_handlers/` (renamed from `src/formats/` per Probe 1 follow-up). Future Bazel package `//src/format_handlers:format_handlers`. Co-located source / headers / `*_test.cpp` per ADR-0003. The `SipiIO` base-class header migrates from top-level `include/SipiIO.h` into the same package.

**2. Glossary terms.** Implements **Format handler** (defined). Uses **Codec** (defined; sharpened — see register). The earlier "Master format" term splits into **Service master format** (this module's read-and-write target) and **Archival master format** (out of SIPI server's read path; CLI mode produces in the future workflow). Surfaces the `getDim` → `read_shape` rename plus four new glossary entries — see register.

**3. Public interface (proposed `hdrs`).**

| Header | Concern |
| --- | --- |
| `format_handlers/sipi_io.h` | base class `SipiIO` + public types (`SipiImgInfo`, `ScalingMethod` / `ScalingQuality`, `Orientation`, `SubImageInfo`, `SipiCompressionParamName` / `SipiCompressionParams`) |
| `format_handlers/sipi_io_j2k.h` | `SipiIOJ2k` — service-master-format handler (JP2) |
| `format_handlers/sipi_io_tiff.h` | `SipiIOTiff` — bidirectional (pyramidal = service master, plain = output) |
| `format_handlers/sipi_io_jpeg.h` | `SipiIOJpeg` — output-only |
| `format_handlers/sipi_io_png.h` | `SipiIOPng` — output-only |

**4. Private surface.** Five `.cpp` files totalling 5,461 lines: J2k (1292), Jpeg (1301), Png (629), Tiff (2239). Per-handler `*_test.cpp` files added during package promotion (today: zero unit tests, only approval coverage — explicit ADR-0003 consequence). Per-handler private statics (`write_basic_tags`, `write_subfile`, `parse_photoshop`, etc.) move to anonymous namespaces in their `.cpp`.

**5. Depth signal.** Per-class header:cpp ratios are textbook deep:

| Class | hdr / cpp | Verdict |
| --- | --- | --- |
| `SipiIOJ2k` | 64 / 1292 (1:20) | **deep** |
| `SipiIOJpeg` | 66 / 1301 (1:20) | **deep** |
| `SipiIOPng` | 66 / 629 (1:10) | **deep** |
| `SipiIOTiff` | 119 / 2239 (1:19) | **deep** |
| `SipiIO` (base) | 183 / — | bloated public-type header in front of a small virtual contract |

**Layering oddities** (will fail Bazel `--strict_deps` until ADR-0003 colocation lands):

- `include/SipiIO.h` is at top-level, not in `include/formats/`. Colocation moves it into the package.
- Format-handler headers do `#include "../../src/SipiImage.hpp"` — cross-tree relative include from `include/formats/` to `src/`, *and* pull the full `SipiImage` definition when only `SipiImage*` is used in signatures.
- Inconsistency: `SipiIOJpeg.h` uses `#include "SipiImage.hpp"` (no `../../`) where the other three use the relative path.

**API modernization opportunities** (tracked in ADR-0006):

1. Five `read` overloads for default arguments → C++ default args.
2. `bool read` returns → `std::expected<void, IoError>`.
3. `SipiImgInfo::success` tri-state enum → `std::expected<SipiImgInfo, ImgInfoError>`.
4. `SipiCompressionParams = unordered_map<int, std::string>` (stringly-typed) → `std::variant<JpegParams, J2kParams, ...>`.
5. Magic-string filepaths (`"-"` for stdout, `"HTTP"` for HTTP-server output) → `std::variant<FilePath, StdoutSink, HttpSink>`.

**The big ADR-0004 correction.** `SipiIO::getDim(filepath) → SipiImgInfo` *already exists* and returns full image shape (`width, height, tile_w, tile_h, clevels, nc, bps, numpages, orientation, mimetype, resolutions`). ADR-0004's `read_shape` IS `getDim`, renamed. The actual change ADR-0004 makes is *"give the existing `getDim` an Essentials-packet fast path in service-master-format handlers"* — not *"add a new method."* The "nc/bps remain 0" overestimate at `SipiHttpServer.cpp:1563-1566` was a `SipiCache::SizeRecord` limitation, not a `getDim` limitation; ADR-0004 fixes it as a happy side effect. ADR-0004 amended in this same commit.

**Master/output asymmetry is operational, not class-level.** `SipiIOTiff` handles both pyramidal (service master) and non-pyramidal (output) TIFF. The Essentials-packet fast path is keyed by *packet presence* at runtime, not by class. `SipiIOJ2k` is service-master-only by current operational policy. `SipiIOJpeg` + `SipiIOPng` are output-only by current operational policy (server mode never reads them as source files).

**Misplaced symbol.** `SipiIOTiff.h:22` declares free function `read_watermark(wmfile, nx, ny, nc) → unsigned char*`. Loads a watermark image. Not TIFF-specific; belongs in a watermark module (pending `Watermark` glossary entry from Probe 1) or absorbed into the request handler that uses it. Defer to Probe 5.

**Doc-string bugs (code-quality, not architectural).** `SipiIOJ2k.h` says *"JPEG 2000 files using libtiff"* (wrong: Kakadu). `SipiIOJpeg.h` says *"JPEG 2000 files using libtiff"* (wrong on both counts). `SipiIOPng.h` says *"libtiff makes extensive use of `lseek`"* (wrong library). Fix in a one-PR cleanup; tracked as a Linear child issue.

**6. Verdict.** Module-level **`deep`** — 5,461 lines of codec implementation behind ~315 lines of public headers, codec-specific knowledge cleanly hidden, one-way dependency on `metadata/` + `iiif_parser/`. The base class header is wide but bounded; doesn't undermine the verdict.

**7. Action.**

a. **Promote to Bazel package** `//src/format_handlers:format_handlers` per ADR-0003 + Probe 1 follow-up rename. Add per-handler unit tests during promotion (ADR-0003 consequence list).

b. **Move headers**: `include/SipiIO.h` → `src/format_handlers/sipi_io.h`. `include/formats/SipiIO*.h` + `src/formats/SipiIO*.cpp` → co-located in `src/format_handlers/sipi_io_*.{h,cpp}`.

c. **Forward-declare `SipiImage`** in headers; full include only in `.cpp`s. Eliminates `../../src/` cross-tree includes.

d. **Implement ADR-0004** inside the existing `getDim` virtual, then rename `getDim` → `read_shape` for self-documentation.

e. **Modernize the API per ADR-0006** — five changes, staged one PR per format handler after package promotion.

f. **Relocate `read_watermark`** — defer to Probe 5 / `Watermark` glossary entry.

g. **Defer**: `SipiImage` heavy-include + friend-class coupling (Probe 4); magic `"HTTP"` sentinel resolution (Probe 5).

**8. Glossary delta.** Five adds + four sharpenings + one rename — see [Glossary delta register](#glossary-delta-register). The most consequential change is splitting the earlier `Master file` / `Master format` rows into `Service master` / `Service master format` (in-server-path) plus new `Archival master` / `Archival master format` (out-of-server-path; SIPI CLI produces, OAIS archive stores).

**9. Open questions for later probes.**

- `read_watermark` final placement — Probe 5 + `Watermark` glossary entry.
- `ScalingQuality` placement: it's in the format-handler API but conceptually a rendering concern — Probe 4 (`SipiImage`) may relocate.
- Magic `"HTTP"` filename sentinel: how should "write to HTTP response stream" be modelled once `SipiHttpServer` is decomposed? Probe 5.
- Archival-master workflow: which actor performs the archival → service-master conversion, and where does that code live? Surfaces only when Probe N hits it.
- The `SipiImage` ↔ `SipiIcc` friend coupling (Probe 2 finding) — does it survive the format-handler refactor? Probe 4.

## Probe 5 — `SipiHttpServer` (decomposition)

**1. Module name.** Five outcomes — two new packages, two extensions to existing planned packages, one deferred:

| Outcome | Concern |
| --- | --- |
| New package `src/server/` | Server lifecycle: `class Server` (composition over `shttps::Server`), `ServerConfig` value type, `realpath` imgroot validation, runtime-resource ownership (`Cache`, `RateLimiter`, `MemoryBudget`). |
| New package `src/route_handlers/` | URL-pattern-bound logic: shttps callbacks (`iiif_handler`, `health_handler`, `metrics_handler`, `favicon_handler`), `serve_*` per-route helpers, `register_routes(Server&, ServerContext)` public entry, preflight + error_response in `internal/` test seams, free-function `get_canonical_url`. |
| Extension to `format_handlers/` | Two new public headers — `output_sink.h` (variant of `FilePath` / `StdoutSink` / `HttpSink` / `TeeSink`, callback-shape `HttpSink`) and `input_source.h` (variant of `FilePath` / `RangeSource`, callback-shape `RangeSource` for S3). Symmetric I/O abstractions. |
| Extension to `image_processing/` | New file `watermark.{h,cpp}` exposing `apply_watermark(Image&, const Image&)`. Replaces `SipiImage::add_watermark` and the misplaced `read_watermark` free function (both deleted). |
| Deferred (no Probe 5 outcome) | `iiif_parser/` (folds in `src/handlers/iiif_handler.{cpp,hpp}` per ) — already on the plan; not new from this probe. |

Future Bazel packages: `//src/server:server`, `//src/route_handlers:route_handlers`. The `format_handlers/` and `image_processing/` extensions slot into existing planned packages.

**2. Glossary terms.** Implements `Server mode` at the lifecycle level (already in delta register from Probe 1); uses the `Route handler` umbrella with the `C++ route handler` sub-type added in the Probe 1 follow-up. Surfaces seven new terms + two confirmations of provisional entries — see register.

**3. Public interface (proposed `hdrs`).**

```
src/server/server.h — class Server (composition over shttps::Server)
src/server/server_config.h — struct ServerConfig (immutable bag of config)

src/route_handlers/route_handlers.h — register_routes(shttps::Server&, const ServerContext&)
src/route_handlers/server_context.h — struct ServerContext (typed dependency bundle, server-scope)
src/route_handlers/canonical_url.h — free fn get_canonical_url(...)

src/format_handlers/output_sink.h — variant + 4 alternatives + free-fn dispatch
src/format_handlers/input_source.h — variant + 2 alternatives + free-fn dispatch

src/image_processing/watermark.h — apply_watermark(Image&, const Image&)
```

**4. Private surface.**

`server/server.cpp` (~150 lines): constructor, `run` with `realpath` imgroot validation + `register_routes` + delegation to `_http.run`, runtime-resource owners.

`route_handlers/`:
- `iiif_handler.cpp` — shttps callback dispatcher (~50 lines), dispatches by URL parser into 5 `serve_*` functions.
- `serve_iiif.cpp` — the ~700-line IIIF route core.
- `serve_info_json.cpp`, `serve_knora_json.cpp`, `serve_redirect.cpp`, `serve_file_download.cpp` — one file per route.
- `system_handlers.cpp` — `health_handler`, `metrics_handler`, `favicon_handler` (~80 lines combined).
- `internal/preflight.{h,cpp}` — `call_iiif_preflight`, `call_file_preflight`, `check_file_access`, `resolve_client_id`. Test seam pattern (visibility restricted to `route_handlers/` + its tests).
- `internal/error_response.{h,cpp}` — 5 `send_error` overloads. Test seam.

`format_handlers/`:
- `output_sink.cpp` — free-function dispatchers (`write_chunk(OutputSink&, span)`, `finalize(OutputSink&)`); per-handler codec-stream adapters (`J2kHttpStream`, libjpeg `HtmlBuffer`, etc.) updated to take `OutputSink&` instead of `Connection*`.
- `input_source.cpp` — free-function dispatchers; per-handler `RangeSource` adapters (libtiff `TIFFClientOpen` callbacks, kakadu `kdu_compressed_source` subclass, libjpeg source manager, libpng `png_set_read_fn`) deferred to per-handler PRs.

`image_processing/watermark.cpp` — overlay math; precondition check (bps==8, contig, 3-4 channels) on the watermark `Image`.

**5. Depth signal.** The current `SipiHttpServer.cpp` is a textbook **mis-bounded scattered** module — 2,277 lines spanning six distinct responsibilities tangled in static helpers reaching into a god-pointer (`SipiHttpServer*` via `user_data`). After Probe 5, each new package has clean depth:

| Package | hdrs / cpp | Verdict |
| --- | --- | --- |
| `server/` | 2 / 1 (~180 lines) | **deep** (lifecycle + composition + validation behind `Server::Server` and `run`) |
| `route_handlers/` | 3 public / 2 internal / ~7 cpp (~1,800 lines) | **deep** (URL-table-binding + IIIF logic behind `register_routes`; per-route logic behind `ServerContext`-only dependency surface) |
| `format_handlers/output_sink` | 1 / 1 (~80 lines) | **deep** (variant dispatch + codec-stream adapter glue in handlers' `.cpp`) |
| `format_handlers/input_source` | 1 / 1 (~50 lines now; grows with S3 integration) | **deep** post-S3 |
| `image_processing/watermark` | 1 / 1 (~120 lines) | reasonable (small but bounded; replaces 2 misplaced functions) |

**Specific code smells eliminated by the decomposition:**

- **God-pointer dependency.** Every handler does `static_cast<SipiHttpServer*>(user_data)` to reach into ~30 fields, of which it actually uses ~5. `ServerContext` makes the actual dependency surface explicit; Bazel `--strict_deps` enforces against accidental widening.
- **Path-string typing for I/O.** Magic `"-"` and `"HTTP"` filename sentinels (called out in ADR-0006) replaced by `OutputSink`. Symmetric `InputSource` removes the path-string-only assumption from the read path, enabling S3 per ADR-0004.
- **Manual cache-pin pairing.** `serve_iiif` has three explicit `cache->deblock(cachefile)` sites per branch (try-success, two catch-failures) at `SipiHttpServer.cpp:1796/1801/1804` → RAII `BlockedScope` (Probe 1 plan, confirmed by code).
- **`shttps/Connection.h` in every format-handler `.cpp`.** Today every codec adapter (`J2kHttpStream`, `HtmlBuffer`, libpng `Connection*`, etc.) `#include`s shttps and reaches into `conobj->sendAndFlush`. After the move, only the route handler's lambda touches `Connection`; format handlers see only `OutputSink`.
- **Watermark loader duplicates `SipiIOTiff::read`.** `read_watermark` (decl `include/formats/SipiIOTiff.h:22`, def `src/formats/SipiIOTiff.cpp:314`) is a constrained mini-parser (bps==8 / `PLANARCONFIG_CONTIG` / 3-4 channels). Deleted; loading uses the format handler.
- **`SipiImage::add_watermark` god-method.** Loading + applying entangled in one method on the image type. Replaced by `apply_watermark(Image&, const Image&)` free function with explicit two-step usage at the route handler.
- **30-setter pattern on `SipiHttpServer`.** `sipi.cpp` lines 1581-1700 set 30 fields one by one (`server.ssl_port(...)`, `server.imgroot(...)`, …) on a half-built class. Replaced by `ServerConfig` value type passed at construction.
- **Inheritance over `shttps::Server`.** Replaced by composition (`shttps::Server _http;` member); aligns with strangler-fig direction (when shttps moves to Rust, only the composition target changes; SIPI's server class stays). Forwarding methods only for what's actually needed.

**6. Verdict.** **`mis-bounded` / `god-object`** for the current `SipiHttpServer.cpp`. The class itself is paper-thin (~200 lines of class proper); the file is 2,277 lines because route logic + I/O abstractions + helpers all squat in static functions. Probe 5's decomposition produces 4 cleanly-bounded modules (2 new packages + 2 extensions to existing packages).

**7. Action.** Twelve staged sub-PRs, each independently reversible:

a. **`OutputSink` introduced** in `format_handlers/output_sink.h` — variant + alternatives + free-function dispatch + `TeeSink`. Format-handler `write` signatures take `OutputSink&` instead of path string. Codec-stream adapters (`J2kHttpStream`, `HtmlBuffer`, etc.) updated. Removes the `"HTTP"` magic sentinel.

b. **`InputSource` introduced** in `format_handlers/input_source.h` — symmetric to (a). Format-handler `read` and `read_shape` take `InputSource&`. `FilePath` integration in this PR; per-handler `RangeSource` integration deferred to (j).

c. **`SipiImage::conobj` removed** (Probe 4 step e, unblocked by (a)). Coupled with 's `TeeSink` work.

d. **`watermark/` extracted** to `image_processing/watermark.{h,cpp}`. `read_watermark` deleted; `SipiImage::add_watermark` deleted; replaced by `apply_watermark(Image&, const Image&)`. Route handler does explicit two-step load + apply.

e. **`BlockedScope` RAII for cache pin** (Probe 1 step b, unblocked by handler refactoring in (g/h)). Three `deblock` sites per branch collapse to scope exit.

f. **`get_canonical_url` extracted** to free function in `route_handlers/canonical_url.{h,cpp}`. Stops being a static method on `SipiHttpServer`.

g. **`route_handlers/` package created** — move all `serve_*` + `*_handler` + preflight helpers + `send_error` family into it. Public surface `register_routes(Server&, ServerContext)`; preflight + error_response in `internal/` test seams. The `static_cast<SipiHttpServer*>(user_data)` pattern dies; handlers consume `ServerContext`.

h. **`server/` package created** — `class Server` (composition over `shttps::Server`), `ServerConfig` value type. The 30 setters in `sipi.cpp` collapse to one `ServerConfig{...}` aggregate construction (gated on Probe 9's `SipiConf` decomposition for the field list).

i. **ADR-0006 amended** to add `InputSource` variant symmetric to `OutputSink`. Single-section addition (not a new ADR).

j. **Per-format-handler `RangeSource` integration** — kakadu, libtiff, libjpeg, libpng each get a custom-source-manager adapter wrapping `RangeSource`. One PR per handler. Gated on (b) + Bazel package promotion (Y+8c).

k. **Bazel package promotion** of `server/` and `route_handlers/` per ADR-0003. Y+8 work; gated on / .

l. **Final cleanup** — delete `src/SipiHttpServer.{cpp,hpp}` once all responsibilities have moved.

Bazel package promotions for new packages (`server/`, `route_handlers/`) extend 's currently enumerated Y+8a..e scope; treat as Y+8f / Y+8g sub-PRs when decomposes.

**8. Glossary delta.** Seven new entries + two confirmations of provisional entries + one sharpening — see [Glossary delta register](#glossary-delta-register). The `Watermark` and `Cache pin` candidate gaps from Probe 1 are confirmed and promoted from candidates to delta entries.

**9. Open questions for later probes.**

- **`ServerConfig` field list** — depends on Probe 9's `SipiConf` decomposition. The conversion from `sipiConf` (Lua-bound) to `ServerConfig` (typed value) is mechanical; the field-by-field mapping settles in Probe 9.
- **`Permission` typing** — currently a `std::unordered_map<std::string, std::string>` returned by Lua preflight, accessed via string keys (`infile`, `watermark`, etc.). Should be a typed value (struct + variant by permission type). Probe 6 (`SipiLua`) decides — surfaces alongside the Lua-FFI surface design.
- **`ServerContext` field list** — locked in form (typed bundle, not god-pointer); detailed fields settle when (g) + Probe 9 land. May split into `ServerContext { const ServerConfig&; Cache&; ... }` or stay flat.
- **`salsah_prefix`** — config field for legacy Salsah PHP integration. `SipiHttpServer.cpp:2218` defaults it to `"imgrep"`. Defer to Probe 8 (curiosities) — likely dead code.
- **Per-handler `RangeSource` adapter shape** — each codec has its own seekable-source contract: kakadu's `kdu_compressed_source`, libtiff's `TIFFClientOpen` with `tiffSeekProc`/`tiffReadProc`/`tiffSizeProc`/`tiffMapProc`/`tiffUnmapProc`, libjpeg's `jpeg_source_mgr`, libpng's `png_set_read_fn`. Per-handler implementation choice; deferred to per-handler PRs.

## Probe 6 — `lua_bindings/` (renamed from `SipiLua` decomposition)

**1. Module name.** Two new Bazel packages from this probe:

| Package | Concern |
| --- | --- |
| `src/lua_bindings/` | The C++ ↔ Lua FFI surface: registration of Lua globals, three binding clusters (`helper.*`, `SipiImage` datatype, preflight callbacks), and the typed `LuaContext` server-scope dependency bundle. Renames `src/SipiLua.{cpp,hpp}`. |
| `src/permission/` | The typed `Permission` value (`std::variant` of seven per-type structs). Consumed by both `lua_bindings/` (produces) and `route_handlers/` (consumes). |

Future Bazel packages: `//src/lua_bindings:lua_bindings`, `//src/permission:permission`.

**2. Glossary terms.** Implements the three existing `Init script` / `Preflight script` / `Lua route handler` (umbrella sub-types). Sharpens `Permission` from a string-keyed map to a typed sum type. Surfaces three new terms — see register.

**3. Public interface (proposed `hdrs`).**

```
src/lua_bindings/lua_bindings.h — register_sipi_globals(lua_State*, shttps::Connection&, LuaContext&)
src/lua_bindings/lua_context.h — struct LuaContext (server-scope; parallels ServerContext)
src/lua_bindings/preflight.h — call_iiif_preflight(...) → expected<Permission, Error>;
 call_file_preflight(...) → expected<Permission, Error>

src/permission/permission.h — variant<AllowPermission, LoginPermission, ClickthroughPermission,
 KioskPermission, ExternalPermission, RestrictPermission,
 DenyPermission>
```

**4. Private surface.**

`lua_bindings/`:
- `helper.cpp` — `helper.filename_hash` binding (~40 lines).
- `image.cpp` — `SipiImage` Lua datatype + 12 method bindings (~1,470 lines). Wraps `Image` value-type by-value in the userdata buffer (post-Probe-4). Each binding body calls the corresponding `image_processing::method(...)` free function.
- `registrar.cpp` — `register_sipi_globals` implementation (~30 lines), composing the binding clusters.
- `preflight.cpp` — Lua-specific parsing of preflight return values into typed `Permission`.

`permission/`:
- `permission.cpp` — `permission_type_name(Permission&)`, `parse_permission_type(string&) → expected<PermissionType, Error>`.

**5. Depth signal.** Three clusters in one ~1,950-line `SipiLua.cpp` today; one cluster (`cache.*`) is deleted entirely in this probe; the remaining two retain their depth profile:

| Cluster | Today | Post-Probe 6 | Verdict |
| --- | --- | --- | --- |
| `cache.*` | 8 functions, 263 lines | **gone** (deleted in this session) | — |
| `helper.*` | 1 function, 40 lines | `lua_bindings/helper.cpp` | small, bounded |
| `SipiImage.*` | 12 methods, ~1,470 lines | `lua_bindings/image.cpp` | **deep** (codec-side knowledge stays in `image_processing/`; bindings are thin pass-through) |

**Specific code smells eliminated:**

- **`sipiserver` Lua global god-pointer.** Every binding does `(SipiHttpServer*)lua_touserdata(L, -1)` to reach into the full server. Replaced by typed `LuaContext` carrying only `Cache&` (initially); Bazel `--strict_deps` enforces against accidental widening.
- **`SImage { Image*; std::string*; }` raw-pointer wrapper** with manual `delete` in `__gc`. Replaced by by-value `SImage { Image; std::string; }` post-Probe-4. RAII via placement-destructor.
- **Stringly-typed `Permission`** (`unordered_map<string, string>`). Replaced by per-type-struct variant in `permission/`. Compile-time exhaustiveness via `std::visit`; "DenyPermission with infile" anti-state is unrepresentable.
- **Two-source-of-truth for cache state.** `cache.size` / `cache.nfiles` etc. duplicated `sipi_cache_size_bytes` / `sipi_cache_files`. Resolved by deletion of the `cache.*` cluster: Prometheus is the canonical inspection surface.
- **Inline error returns** (`lua_pushboolean(false), lua_pushstring(msg)`). Paired with `std::expected` per ADR-0006; bindings use `SIPI_TRY` style or explicit dispatch.
- **Lua as server-state mutation backdoor.** `cache.lua` deleted; the architectural principle is locked: **Lua is for request-shaping (preflight, custom content routes); server-state mutation is C++ in `route_handlers/`.** Audit follow-up for Probe 8 (`exit.lua` and similar candidates).

**6. Verdict.** **Deep** at the module level — ~1,500 lines of FFI binding code behind a small ~4-header public surface, with significant Lua-side and C++-side complexity hidden. The decision to keep all binding clusters in one Bazel package (Q1 option (a)) is consistent with `metadata/`, `format_handlers/`, `route_handlers/`.

**7. Action.** Eight staged sub-PRs (one of which is already done):

a. **`cache.*` Lua cluster + `cache.lua` removed** ✅ **DONE in this session.** 8 bindings deleted from `SipiLua.cpp`; `scripts/cache.lua` removed; `/api/cache` route registrations dropped from `config/sipi.{config,test-config,localdev-config}.lua`. SipiLua.cpp size: 1,949 → 1,686 lines (-263).

b. **Introduce `LuaContext` typed dependency bundle.** Replaces `(SipiHttpServer*)lua_touserdata(L, -1)` god-pointer. Lives in `lua_bindings/lua_context.h`. Initial fields: `Cache& cache;`.

c. **Introduce `Permission` typed variant** in new `permission/` package. Replaces `unordered_map<string, string>`. Per-type structs for compile-time exhaustiveness. Used by `lua_bindings/` (producer) and `route_handlers/` (consumer).

d. **Move preflight Lua-parsing** to `lua_bindings/preflight.{h,cpp}`. Returns `std::expected<Permission, Error>`. Route handlers no longer see `LuaValstruct`.

e. **Cascade Probe 4 method-to-free-function moves** into SipiImage Lua bindings. ~12 method-call sites change from `img->image->method(...)` to `image_processing::method(*img->image, ...)`. Lua scripts unchanged.

f. **Refactor `SImage` userdata layout to by-value.** Gated on Probe 4's `Image` value-type changes . RAII via placement-destructor in `__gc`.

g. **Promote `lua_bindings/` and `permission/` to Bazel packages** per ADR-0003 (Y+8h, Y+8i — extends 's currently enumerated scope).

h. **Add per-cluster unit tests during package promotion** — today: zero unit-test coverage for Lua bindings; covered only by integration tests through HTTP.

**8. Glossary delta.** Three new entries + one sharpening + two notes — see [Glossary delta register](#glossary-delta-register).

**9. Open questions for later probes.**

- **`exit.lua`** — Probe 8 candidate for the same deletion-replacement-with-C++-route pattern (server-state mutation: terminates the server). Different auth posture from cache, but same architectural principle.
- **`admin_upload.lua` / `upload.lua`** — content-shaping (legitimate Lua), but worth auditing for SipiImage binding patterns post-refactor. Probe 8.
- **`debug.lua`, `clean_temp_dir.lua`, `orientation.lua`, `send_response.lua`, `test1.lua`, `test2.lua`, `test_sqlite.lua`, `token.lua`** — most are likely test scaffolding or example scripts. Probe 8 audit.
- **C++ replacement route for cache management** — if external cache control is ever needed, a dedicated `serve_cache_admin` in `route_handlers/` (e.g. `POST /admin/cache/purge` with basic-auth + structured body). Not blocking; create on demand.
- **`Permission` field set per type.** Initial structs are minimal (mostly just `infile` + `RestrictPermission`'s extras). When Probe 8 audits real Lua scripts, additional per-type fields (auth challenge URLs, login redirect targets, external gateway URLs) may surface. Add per-real-caller, not speculatively.

## Probe 7 — `throttling/` (renamed from the `backpressure/` cluster)

**1. Module name.** `src/throttling/` — one new Bazel package consolidating the three rejection-policy classes + their helpers. Future Bazel package `//src/throttling:throttling`.

| Today | Post-Probe-7 |
|---|---|
| `include/SipiRateLimiter.h`, `src/SipiRateLimiter.cpp` | `src/throttling/rate_limiter.{h,cpp}` |
| `include/SipiMemoryBudget.h`, `src/SipiMemoryBudget.cpp` | `src/throttling/memory_budget.{h,cpp}` |
| `include/SipiPeakMemory.h` (header-only pure helper) | `src/throttling/internal/peak_memory.h` (Test seam visibility) |
| Inline `max_pixel_limit` guard at `SipiHttpServer.cpp:1631-1644` | `src/throttling/output_size_guard.{h,cpp}` (new class) |
| Static `resolve_client_id` at `SipiHttpServer.cpp:324` | `src/throttling/client_id.{h,cpp}` (free fn) |
| `include/iiifparser/SipiDecodeDims.h` (used only by memory-budget gate today) | **stays in `iiif_parser/`** — pure IIIF semantics, not policy; the dependency `throttling/ → iiif_parser/` is one-way |

**2. Glossary terms.** Implements the renamed umbrella **Throttling** (was *Backpressure*). Three sub-terms: **Decode memory budget** (existing, sharpened), **Rate limiter** (existing, sharpened), **Output size guard** (new). Surfaces one rename + two sharpenings + one new term + one cleanup of the `UBIQUITOUS_LANGUAGE.md` example dialogue — see register.

**3. Public interface (proposed `hdrs`).**

```
src/throttling/output_size_guard.h — class OutputSizeGuard + check(w, h) → optional<RejectReason>
src/throttling/rate_limiter.h — class RateLimiter + check_and_record(client_id, pixels)
src/throttling/memory_budget.h — class MemoryBudget + try_acquire(bytes) + RAII Guard
src/throttling/client_id.h — free fn client_id_from(Connection&) → string
```

No orchestrator entry point. Three independent entries called sequentially at one post-cache gate site in `route_handlers/serve_iiif.cpp`. A unified `throttling::admit(AdmissionRequest)` orchestrator was rejected as premature abstraction (one production caller, three policies, no plans for runtime composition; CLAUDE.md scope-discipline rule "prefer three similar lines over one parameterized helper").

**4. Private surface.**
- `output_size_guard.cpp` — small, mostly metric-emission glue around the `requested_w * requested_h > max_pixel_limit` check.
- `rate_limiter.cpp` — sliding-window deque + per-client map (existing content lifted unchanged from `SipiRateLimiter.cpp`).
- `memory_budget.cpp` — atomic counter + RAII guard impl (existing content lifted unchanged from `SipiMemoryBudget.cpp`).
- `client_id.cpp` — XFF-rightmost / peer_ip resolution (lifted from `SipiHttpServer.cpp:324-339`).
- `internal/peak_memory.h` — pure `estimate_peak_memory(decode_w, decode_h, out_w, out_h, nc, bps, rotation, needs_icc) → size_t` helper, restricted visibility to `//src/throttling:__pkg__` + the Throttling unit-test target. Test seam pattern from Probe 2.

**5. Depth signal.** Pre-Probe-7 the cluster is `scattered, extract`:

| Component | Today's location | Note |
|---|---|---|
| `SipiRateLimiter` | `include/` + `src/` top-level | sliding-window per-client; gate at `SipiHttpServer.cpp:1647` (pre-cache) |
| `SipiMemoryBudget` | `include/` + `src/` top-level | atomic counter + RAII guard; gate at `SipiHttpServer.cpp:1811` (post-cache) |
| `SipiPeakMemory` | `include/` only (header-only) | pure helper; only consumer is the budget gate |
| `max_pixel_limit` | inline at `SipiHttpServer.cpp:1633` | no class; `> 0` to enable, no MONITOR mode |
| `resolve_client_id` | static fn at `SipiHttpServer.cpp:324` | only consumer is the rate limiter |

The three policies share: the OFF/MONITOR/ENFORCE mode shape (mostly — output-size-guard has only OFF/ENFORCE today; alignable), the Result-struct shape, and (post-relocation) the gate-site location. Pulling them into one package + one gate-site call sequence is a textbook deep-module move — small public surface, real complexity hidden (sliding-window cleanup with periodic sweep, lock-free atomics with memory-ordering discipline, peak-memory pipeline modeling that walks decode → scale → rotate → ICC stages).

Per-package shape: 4 public headers / 5 .cpp files / ~600 lines hidden (rate-limiter cleanup logic + memory-budget atomics + peak-memory math).

**6. Verdict.** `scattered, extract` today; `deep` post-package promotion.

**7. Action.** Six staged sub-PRs (each independently reversible):

a. **Move rate-limit gate post-cache** — behaviour change. The rate-limit check fires after the cache-hit short-circuit, alongside the memory-budget gate, instead of pre-cache at line 1647. Cache-hit responses are no longer rate-limited. Per [ADR-0008](../adr/0008-rate-limit-post-cache.md). Justified because the rate limiter exists to mitigate harvest-bot impact, harvest bots sweep unique URLs (cache-miss-dominant workload), so the pre-cache placement was protecting against a non-existent attack model.

b. **Promote `OutputSizeGuard` to a class** in `throttling/output_size_guard.{h,cpp}`. Replaces inline `max_pixel_limit` check at `SipiHttpServer.cpp:1631-1644`. Same OFF/ENFORCE shape as today; `MONITOR` mode added if metrics later show value. Joins the post-cache gate site as the third Throttling sub-policy.

c. **Move existing files into `src/throttling/`** — file-rename PR.
- `include/SipiRateLimiter.h` + `src/SipiRateLimiter.cpp` → `src/throttling/rate_limiter.{h,cpp}`.
- `include/SipiMemoryBudget.h` + `src/SipiMemoryBudget.cpp` → `src/throttling/memory_budget.{h,cpp}`.
- `include/SipiPeakMemory.h` → `src/throttling/internal/peak_memory.h` (Test seam visibility).
- `Sipi::SipiRateLimiter` → `Sipi::throttling::RateLimiter` (drop `Sipi`-prefix, namespace nested); same for `SipiMemoryBudget`.

d. **Move `resolve_client_id`** from `SipiHttpServer.cpp:324` to `throttling/client_id.{h,cpp}` as a free function `Sipi::throttling::client_id_from(shttps::Connection&) → std::string`.

e. **Promote to Bazel package** `//src/throttling:throttling` per ADR-0003. Co-locate `*_test.cpp` from current `test/unit/{memory_budget,ratelimiter}/`. Existing `test/unit/decode_dims/` stays with `iiif_parser/` (its source-of-truth).

f. **Defer**: per-policy unit-test expansion if existing tests are insufficient post-relocation. Post-promotion, evaluate whether `output_size_guard` needs its own unit tests (likely yes — currently covered only by integration tests through HTTP).

Bazel package promotion (step e) gated on [tracked separately] reaching Y+8. Steps a, b, c, d can land in CMake era.

**8. Glossary delta.** See [Glossary delta register](#glossary-delta-register). Five entries: rename `Backpressure` → `Throttling`, add `Output size guard`, sharpen `Rate limiter` (post-cache placement), sharpen `Decode memory budget` (code-level placement), sharpen `Cache` (cache-hit short-circuits all Throttling gates).

**9. Open questions for later probes.**
- **Mode harmonization across the three policies.** Today: budget has OFF/MONITOR/ENFORCE; rate limiter has OFF/MONITOR/ENFORCE; output-size-guard has only "0=off, otherwise enforce." Worth aligning all three to OFF/MONITOR/ENFORCE for consistency. Defer to implementation time.
- **Linear issue cutting** for the six sub-PRs — defer to Linear-issue-cutting pass when decomposes.
- **Probe 9 (Operational surface):** the three Throttling metrics families (`decode_memory_*`, `rate_limit_*`, `image_too_large_total`) live in `SipiMetrics`. When that probe lands, decide whether throttling-specific metrics get their own header / namespace inside the metrics module, or stay with the global registry.
- **Should `compute_decode_dims` move to `image_processing/`?** Today it lives in `iiif_parser/` — pure IIIF semantics. Probe 4 (`image_processing/`) might pull it in if codec-side decode wrappers also start consuming it (likely, when reduce-level ROI-decode integrates with the format-handler `read` path). Defer to Probe 4 follow-up.

## Probe 8 — Curiosities and Lua-script audit

A **triage probe**, not a decomposition probe. Walks the unnamed C++ classes (`PhpSession`, `Salsah`, `Template`, `SipiReport`, `Logger`) and the loose `scripts/*.lua` files. Classifies each as live / vestigial / dead and applies the [Probe 6 mutation-→-C++-route principle](#probe-6--lua_bindings-renamed-from-sipilua-decomposition) systematically. The output is mostly **deletions**, plus one small package extraction (`logging/`).

**1. Outcomes.** Five concrete actions:

| # | Outcome | Action |
|---|---|---|
| 1 | **Salsah cluster deletion** | Delete `Salsah.{h,cpp}`, `PhpSession.{h,cpp}`, `Template.{h,cpp}`, `--salsah` CLI flag (`sipi.cpp:649-650,:1230`), `_salsah_prefix` field + accessors (`SipiHttpServer.{hpp,cpp}` lines 47/107/109/2218/2254), `BUILD.bazel:143` "unwired" comment block, and the `mysql` link dependency. ~700 lines deleted. |
| 2 | **Lua mutation / orphan deletion** | Delete `exit.lua`, `clean_temp_dir.lua`, `admin_upload.lua`, `debug.lua` + their config bindings. Per Probe 6 principle: server-state mutation does not live in Lua; lifecycle uses SIGTERM not HTTP. |
| 3 | **Production-config cleanup** | Strip `test1.lua`, `test2.lua`, `test_sqlite.lua`, `test_functions.lua` bindings from `sipi.config.lua`. Keep them only in `sipi.test-config.lua`. Move the scripts themselves from `scripts/` → `test/scripts/`. |
| 4 | **`Logger` promoted to `src/logging/`** | New Bazel package `//src/logging:logging`. `include/Logger.h` + `src/Logger.cpp` → `src/logging/logger.{h,cpp}`. The shttps consumption of Logger is documented as the second known layering leak in `CONTEXT.md` (alongside the existing `SipiMetrics::instance` leak). |
| 5 | **`SipiReport` deferred** | Tightly CLI-mode-coupled (`--json` flag, schema-mirrors `ImageContext`). Defer placement to Probe 10 (`sipi.cpp` entry-point decomposition); SipiReport rides along when `src/cli/` settles. |

Complementary work already in flight: [PR #619](https://github.com/dasch-swiss/sipi/pull/619) deletes `cache.lua` + the 8 `cache.*` Lua bindings (per Probe 6); same architectural principle.

**2. Glossary terms.** Surfaces:
- **Logger** (new) — basic logging primitives + level / mode control. SIPI-side utility consumed across both contexts.
- **Mutation script** (new anti-pattern term) — formalizes the Probe 6 principle. A *Lua route handler* that mutates server state (cache eviction, server lifecycle, filesystem cleanup, …) is a Mutation script and is forbidden; the canonical surface is a *C++ route handler* (or a signal handler for lifecycle).
- **CLI report** (deferred to Probe 10) — the structured JSON document SipiReport emits when `--json` is set.

See [Glossary delta register](#glossary-delta-register).

**3. Inventory of classifications.** Two tables — what's deleted and why, what's kept and where it goes.

*C++ classes:*

| Class | hdr / cpp lines | Verdict | Note |
|---|---|---|---|
| `Salsah` | 83 / 234 | **dead, delete** | Hardcoded MySQL creds (`mysql_real_connect("localhost", "salsah", "imago", "salsah", ...)`). `BUILD.bazel:143` comment confirms class is unwired at runtime. |
| `PhpSession` | 64 / 254 | **dead with Salsah** | PHP-serialization parser; only consumer is `Salsah.cpp`. |
| `Template` | 38 / 56 | **dead with Salsah** | Only consumer is `Salsah.cpp`. |
| `Logger` | 32 / 197 | **live, foundational** | 18+ consumers including 5+ in `shttps/`. Cross-context layering leak (documented). |
| `SipiReport` | 57 / 136 | **live, narrow** | `--json` CLI report; consumed only by `sipi.cpp`. Placement deferred to Probe 10. |
| `--salsah` CLI flag | 2 lines | **dead leaf** | Prints `nx ny` to stdout; no other behaviour. |
| `_salsah_prefix` field | 5 lines | **vestigial** | Set to `"imgrep"`, only consumer is its own `log_debug` statement. |

*Lua scripts (`scripts/`):*

| Script | Bound in | Verdict | Action |
|---|---|---|---|
| `cache.lua` | all 3 configs | being-removed | Out of Probe 8 scope — [PR #619](https://github.com/dasch-swiss/sipi/pull/619). |
| `exit.lua` | all 3 configs | mutation script | **Delete** (no replacement; SIGTERM is canonical lifecycle surface). |
| `clean_temp_dir.lua` | none (orphan) | mutation script | **Delete** (orphan + filesystem mutation). |
| `admin_upload.lua` | none (orphan) | dead | **Delete** (orphan; admin route never bound). |
| `debug.lua` | none (orphan) | dead | **Delete** (orphan). |
| `test1.lua`, `test2.lua` | `sipi.config.lua` + `sipi.test-config.lua` | test scaffolding | **Strip from production config**; move to `test/scripts/`. |
| `test_sqlite.lua`, `test_functions.lua` | `sipi.config.lua` only | test scaffolding | **Strip from production config**; move to `test/scripts/`. |
| `upload.lua` | `sipi.config.lua` | request-shaping | **Keep** — legitimate Lua use (image upload, content-shaping). |
| `send_response.lua` | required by 5 sibling scripts | utility | **Keep** — helper module supporting `upload.lua`. |
| `token.lua` | `sipi.config.lua` | request-shaping (JWT) | **Keep** (audit pending). |
| `orientation.lua` | `sipi.config.lua` | request-shaping | **Keep** (audit pending). |

**4. The Logger cross-boundary leak.** Logger is consumed by 18+ files including `shttps/Server.{cpp,h}`, `shttps/LuaServer.cpp`, `shttps/Shttp.cpp`, `shttps/ThreadControl.cpp`. This violates the one-way `SIPI → shttps` direction documented in `CONTEXT-MAP.md`. Same shape as the existing `shttps/Server.cpp → SipiMetrics::instance` leak — `CONTEXT.md` already documents that one with the prescribed fix (callback-hook on `shttps::Server`). The Logger leak is the **second** instance and is documented in `CONTEXT.md` with the same disposition: known transitional leak, awaits the strangler-fig migration to Rust (Rust shttps will use `tracing` crate; SIPI will use whatever its Rust replacement adopts).

The four SIPI-only symbols on Logger today (`set_cli_mode`, `is_cli_mode`, `set_json_mode`, `is_json_mode`) stay co-located in `src/logging/logger.h` despite being SIPI-domain — splitting a 32-line header into two files for a 4-symbol concern is diminishing returns. They're guarded by `std::atomic<bool>` already; thread-safety unaffected.

**5. Verdict.** This is a **`triage`** probe; no single-class verdict applies. Per-component verdicts in the inventory tables above. Net code reduction: ~700 lines of dead C++ + 4 dead Lua scripts + production-config cleanup; one new `logging/` package extracted.

**6. Action.** Five staged sub-PRs (each independently reversible):

a. **Delete Salsah cluster** — single PR. Removes `Salsah.{h,cpp}`, `PhpSession.{h,cpp}`, `Template.{h,cpp}`, `--salsah` flag, `_salsah_prefix` field + accessors, `BUILD.bazel:143` block, `mysql` link dep. Removes hardcoded MySQL credentials from the binary.

b. **Delete 4 server-state-mutation / orphan Lua scripts** — single PR. Removes `exit.lua`, `clean_temp_dir.lua`, `admin_upload.lua`, `debug.lua` + their config bindings. Pattern matches PR #619 (cache.lua deletion).

c. **Strip test_*.lua from production config; relocate scripts** — single PR. Removes 4 test bindings from `sipi.config.lua`, moves `test1.lua`, `test2.lua`, `test_sqlite.lua`, `test_functions.lua` from `scripts/` → `test/scripts/`. `sipi.test-config.lua` keeps its bindings (with updated paths).

d. **Promote `Logger` to `src/logging/`** — file rename PR. `include/Logger.h` → `src/logging/logger.h`; `src/Logger.cpp` → `src/logging/logger.cpp`. Update 23+ `#include "Logger.h"` sites (SIPI + shttps). No namespace flip in this PR — Logger is currently a global-namespace API; namespace migration deferred (or done as part of the Bazel-promotion PR).

e. **Promote `//src/logging:logging` Bazel package** — Y+8k. Blocked on (d) + [tracked separately] reaching Y+8. Visibility: `["//src/...:__subpackages__", "//shttps:__pkg__"]` (the shttps allowlist documents the known leak in the build graph).

**7. Glossary delta.** See [Glossary delta register](#glossary-delta-register). Two adds + one note: add **Logger**, add **Mutation script** (anti-pattern term), note **CLI report** as deferred-to-Probe-10.

**8. CONTEXT.md update.** Add the Logger layering leak to the `## Known layering leak` section in `CONTEXT.md`, alongside the existing `SipiMetrics::instance` entry. Same disposition: tracked bug, transitional, fixed by Rust port.

**9. Open questions for later probes.**
- **`token.lua` / `orientation.lua` content audit.** Listed as keep but content not deeply read. Probe 9 or Probe 10 may surface follow-ups if either touches state mutation.
- **`SipiReport` placement** — deferred to [Probe 10](#probe-order) (`sipi.cpp` entry-point decomposition).
- **Logger leak fix via callback hook** — the prescribed pattern matches the SipiMetrics leak fix. Either both leaks get fixed pre-Rust-port (one PR each on `shttps::Server`) or both wait for the Rust strangler-fig swap. Decide together; not a Probe 8 deliverable.
- **`scripts/` directory final shape** — after Probes 6 + 8 + PR #619, the surviving scripts are `upload.lua`, `send_response.lua`, `token.lua`, `orientation.lua`. That's a content-shaping cluster; Probe 6 already named the role (Lua route handler — request-shaping). No further reorganization needed.

## Probe 9 — Operational surface (`observability/` + `config/`)

Splits the operational surface into **two new packages**: `src/observability/` (operational telemetry — metrics, Sentry error capture, shttps connection-metrics adapter) and `src/config/` (startup parameter parsing — `SipiConf` Lua parser + the new `ServerConfig` immutable value type that Probe 5 left pending).

Why two packages, not one: telemetry is runtime instrumentation with many consumers; config is startup-only parsing with one producer (Lua) and one consumer (`Server::Server(...)`). They have different lifecycles, different consumer profiles, and different Rust-port targets (telemetry → `tracing` + `prometheus` + `sentry-rust`; config → `serde` + the `config` crate).

**1. Outcomes.**

| Outcome | Action |
|---|---|
| New package `src/observability/` | Lifts `SipiMetrics`, `SipiConnectionMetricsAdapter`, `SipiSentry` (with header surgery — see below). Adds new `sentry_init.{h,cpp}` extracted from `sipi.cpp:442-484`. |
| New package `src/config/` | `SipiConf` (Lua parser, kept) + new `ServerConfig` immutable value type + free fn `to_server_config(const SipiConf&)`. Unblocks [tracked separately] / [tracked separately] (Probe 5 ServerConfig work). |
| `SipiSentry` heavyweight-header surgery | Move all `inline` impls (`get_file_size`, `predefined_profile_to_string`, `populate_from_image`, `capture_image_error`, etc.) to `sentry.cpp`. Drop `#include "SipiImage.hpp"` from the public header. Eliminates a heavy transitive include from every Sentry consumer (today: `SipiImage.hpp` + 5 metadata headers + exiv2 + lcms2 in every TU that calls `capture_image_error`). |
| Sentry init extraction | `sipi.cpp:442-484` (sentry_options_new + sentry_init + close) → `observability/sentry_init.{h,cpp}` with a `SentryConfig` struct. Env-var reading (`SIPI_SENTRY_DSN/ENVIRONMENT/RELEASE`) stays at the `sipi.cpp` call site (CLI-mode policy). |
| `SipiMetrics` stays a singleton (for now) | Dependency injection is a tempting Probe-5/6-pattern extension but would touch 18+ call sites across SIPI + shttps for arguable correctness benefit. Singleton is the prevailing prometheus-cpp idiom. Probe 9 just **relocates** the singleton into `observability/metrics.{h,cpp}`. Defer DI to a separate cleanup if ever needed. |

**2. Glossary terms.** Surfaces five new entries — see register: **Observability** (umbrella), **Metrics**, **Sentry context**, **Connection metrics adapter**, **Server config**. Plus a sharpening of the existing CLI/Server `SipiMode` distinction.

**3. Public interface (proposed `hdrs`).**

```
src/observability/metrics.h — class Metrics + instance (singleton, relocated)
src/observability/connection_metrics_adapter.h — class ConnectionMetricsAdapter : shttps::ConnectionMetrics
src/observability/sentry.h — ImageContext, capture_image_error(...), enum SipiMode
src/observability/sentry_init.h — struct SentryConfig + init_sentry / close_sentry

src/config/sipi_conf.h — class SipiConf (Lua-binding parser; existing surface)
src/config/server_config.h — struct ServerConfig (immutable value type, 38 fields, 7 logical groups)
 + free fn to_server_config(const SipiConf&) → ServerConfig
```

**4. Private surface.**
- `observability/metrics.cpp` (~172 lines lifted from `SipiMetrics.cpp`).
- `observability/connection_metrics_adapter.cpp` (~26 lines lifted from `SipiConnectionMetricsAdapter.cpp`).
- `observability/sentry.cpp` (new — receives ~200 lines moved from the 244-line `SipiSentry.h`).
- `observability/sentry_init.cpp` (~50 lines lifted from `sipi.cpp:442-484`).
- `config/sipi_conf.cpp` (~164 lines lifted from `SipiConf.cpp`).
- `config/server_config.cpp` (new — `to_server_config(...)` body, ~80 lines mechanical assignment).

**5. `ServerConfig` field list (7 logical groups, 38 fields).**

| Group | Fields |
|---|---|
| Network & TLS | `hostname`, `port`, `ssl_port`, `ssl_certificate`, `ssl_key` |
| Image storage | `img_root`, `tmp_dir`, `max_temp_file_age`, `subdir_levels`, `subdir_excludes`, `prefix_as_path` |
| Encoding | `jpeg_quality`, `scaling_quality` (map) |
| Cache | `cache_dir`, `thumb_size`, `cache_size`, `cache_n_files` |
| Lua | `init_script`, `scriptdir`, `routes` |
| Webserver / Auth / Logging | `docroot`, `wwwroute`, `knora_path`, `knora_port`, `logfile`, `loglevel`, `userid_str`, `jwt_secret`, `adminuser`, `password`, `keep_alive`, `max_post_size` |
| Concurrency | `n_threads`, `max_waiting_connections`, `queue_timeout`, `drain_timeout` |
| Throttling (per Probe 7) | `max_pixel_limit` (→ `OutputSizeGuard`), `rate_limit_max_pixels`, `rate_limit_window`, `rate_limit_mode_str`, `rate_limit_pixel_threshold` (→ `RateLimiter`), `max_decode_memory`, `decode_memory_mode_str` (→ `MemoryBudget`) |

Knora field names (`knora_path`, `knora_port`) stay matching the Lua keys for traceability; the deprecated-but-shipping aliases stay per the glossary's *Deprecated / legacy* section.

**6. Depth signal.**

- `observability/`: 4 public headers / 4 .cpp files / ~450 lines hidden. Deep — Prometheus instrumentation + Sentry-protocol packet construction + shttps strategy adapter. Small public surface (one `Metrics::instance`, one `capture_image_error(...)`, one `ConnectionMetricsAdapter` ctor, one `init_sentry(SentryConfig)`).
- `config/`: 2 public headers / 2 .cpp files / ~250 lines. Reasonable depth — Lua-binding parsing logic on the SipiConf side; mechanical translation on the ServerConfig side.

**Specific code smells eliminated:**

- **`SipiSentry`'s 244-line header with all `inline` impls** — every TU calling `capture_image_error` re-instantiates the function body and pulls in `SipiImage.hpp` + 5 metadata headers + exiv2 + lcms2. After the .cpp move + forward declaration, the consumer-side cost drops to ~30 lines + `<sentry.h>` + a forward declaration of `Sipi::SipiImage`.
- **`SipiHttpServer`'s 30-setter pattern** in `sipi.cpp:1581-1700` (Probe 5 finding). Replaced by aggregate ctor on `ServerConfig`.
- **Sentry init buried in `sipi.cpp`** at lines 442-484. Hard to test, hard to swap out for tests. Now an explicit `init_sentry(SentryConfig)` entry point.
- **`SipiSentry`'s free helpers in global namespace** (`get_file_size`, `predefined_profile_to_string`, `orientation_to_string`, `format_type_to_string`) — moved into `Sipi::observability::` namespace; conflicts impossible.

**7. Verdict.** **`scattered, extract`** for both outcomes today. The five top-level files (`SipiMetrics`, `SipiConnectionMetricsAdapter`, `SipiSentry`, `SipiConf`) are each their own concern; pulling them into two coherent packages by responsibility is the textbook deep-module move.

**8. Action.** Eight staged sub-PRs split across two parents. Each independently reversible.

**Observability parent — four sub-PRs:**

a. **`SipiSentry` header surgery** — move all `inline` bodies to `sentry.cpp`; drop `SipiImage.hpp` include from public header (forward-declare `Sipi::SipiImage` instead). Function signatures unchanged. No behaviour change.

b. **Move `SipiMetrics` + `SipiConnectionMetricsAdapter` into `src/observability/`** — file-rename PR. `include/SipiMetrics.h` + `src/SipiMetrics.cpp` → `src/observability/metrics.{h,cpp}`. `include/SipiConnectionMetricsAdapter.h` + `src/SipiConnectionMetricsAdapter.cpp` → `src/observability/connection_metrics_adapter.{h,cpp}`. Class names: `SipiMetrics` → `Sipi::observability::Metrics`; `Sipi::SipiConnectionMetricsAdapter` → `Sipi::observability::ConnectionMetricsAdapter`. Update consumers.

c. **Extract Sentry init** from `sipi.cpp:442-484` → `src/observability/sentry_init.{h,cpp}`. Define `Sipi::observability::SentryConfig` struct + free fns `init_sentry(const SentryConfig&)` / `close_sentry`. Env-var reads stay at the `sipi.cpp` call site.

d. **Promote `//src/observability:observability` Bazel package** (Y+8L) — gated on a + b + c + [tracked separately].

**Config parent — four sub-PRs:**

a. **Define `ServerConfig` value type** in new files `src/server_config.{h,cpp}` (top-level for now; moves into `config/` in step (c)). 38 fields, 7 logical groups. Add free fn `to_server_config(const SipiConf&) → ServerConfig`. No consumer changes yet.

b. **Convert `SipiHttpServer` to consume `ServerConfig`** at construction. Drop 30 setters from `SipiHttpServer.hpp`. New ctor: `Server(const ServerConfig&)`. The 30-setter call cluster in `sipi.cpp:1581-1700` collapses to one aggregate construction. **Unblocks [tracked separately] and [tracked separately]** (Probe 5 deferred field list).

c. **Move `SipiConf` + `ServerConfig` into `src/config/`** — file-rename PR. `include/SipiConf.h` + `src/SipiConf.cpp` → `src/config/sipi_conf.{h,cpp}`. `src/server_config.{h,cpp}` (from step (a)) → `src/config/server_config.{h,cpp}`. Update consumers.

d. **Promote `//src/config:config` Bazel package** (Y+8M) — gated on a + b + c + [tracked separately].

**9. Open questions for later probes.**

- **`SipiMetrics` dependency injection.** Defer indefinitely. Singleton is the prevailing prometheus-cpp idiom; the documented `shttps/Server.cpp → SipiMetrics::instance` leak is the same shape as the Logger leak (Probe 8) — both await the strangler-fig Rust port. If a new SIPI consumer ever justifies typed access, revisit.
- **Knora field rename.** `knora_path` / `knora_port` are deprecated names per the glossary. Field names stay matching Lua keys for back-compat traceability. Open question whether to rename internally to `dsp_path` / `dsp_port` post-Rust-port. Defer.
- **`max_temp_file_age` vs cache.** The field's home (cache module vs server config) is currently in `ServerConfig`'s "Image storage" group; semantically it's a cache concern. Decide during implementation if it makes more sense to thread it through `Cache::Cache(...)` directly. Cosmetic.
- **Shape of `scaling_quality`.** Currently `std::map<std::string, std::string>`. Probe 4 noted scaling-quality is a rendering concern; possibly worth a typed enum. Defer to Probe 4 follow-up; ServerConfig matches today's shape.

## Probe 10 — `cli/` (entry-point decomposition) + closing sweep

The final probe. `sipi.cpp` is **1,712 lines** dominated by a ~1,280-line `int main` that interleaves: CLI11 argument declarations (~400 lines), CLI-mode dispatchers (query / compare / convert), server-mode bootstrap, library initialisation, Sentry init, signal handling. After all prior probes' moves, the file is the last god-function in the codebase.

This probe also closes the deep-modules exercise with a directory sweep — every loose file under `src/` and `include/` gets a final verdict.

**1. Outcomes.** Two new packages (or rather, one new package + one final cleanup):

| Outcome | Action |
|---|---|
| New package `src/cli/` | Houses CLI-mode dispatchers, server-mode bootstrap, CLI11 options, library init, system-resource detection (cgroup + sysctl), `SipiReport` (Probe 8 deferred decision settled), and `my_terminate_handler`. |
| New top-level `src/main.cpp` | The `cc_binary`'s `int main` ONLY — ~50 lines. The 1,280-line main collapses to: version check → terminate handler → Sentry init → library init → CLI11 parse → mode dispatch. Each branch hands off to a `Sipi::cli::run_*(opts)` entry point. |
| `SipiReport` placement (Probe 8 deferred) | Confirmed: `src/cli/report.{h,cpp}` (CLI-only consumer; tightly coupled to `--json` flag). |
| Closing sweep | Six loose files / directories classified — see §3 below. |

**2. Glossary terms.** Confirms two existing delta-register entries (`CLI mode`, `Server mode`) — the boundary is now a *code* boundary at the `src/cli/` package. Adds **CLI report** (Probe 8 deferred). No new umbrella terms — `cli/` is a topic name, not a vocabulary item.

**3. Closing sweep — six loose files / directories.**

After all 9 prior probes' moves, the surviving loose files under `src/` + `include/`:

| File | Today | Verdict | Action |
|---|---|---|---|
| `include/SipiCommon.h` (14 lines) + `src/SipiCommon.cpp` (12 lines) | empty namespace declarations only (verified — no symbols inside) | **dead** | Delete outright. |
| `include/SipiFilenameHash.h` (85 lines) + `src/SipiFilenameHash.cpp` (229 lines) | cache filename hashing helper | **misplaced** | Move into `src/cache/filename_hash.{h,cpp}` per Probe 1 ([tracked separately]). |
| `src/SipiError.{hpp,cpp}` (69 + 33 lines) | SIPI-wide exception base type | **cross-cutting** | New small package `src/errors/sipi_error.{h,cpp}`. Every domain throws `SipiError`; small inheritance hierarchy; co-locating in any one domain would require dependency reverse-direction. Recommend new package. |
| `src/SipiImageError.hpp` (129 lines) | image-specific exception thrown from image processing | **per-domain** | Move into `src/image/image_error.h` per Probe 4 ([tracked separately]). Inherits from `Sipi::errors::SipiError`. |
| `include/favicon.h` | static favicon byte data | **co-locate with consumer** | Move into `src/route_handlers/favicon.h` per Probe 5 ([tracked separately]). Consumed only by `favicon_handler`. |
| `include/CLI11.hpp` (~9 KLOC vendored) | vendored single-header library | **vendored dep** | Move out of `include/` into `ext/CLI11/`. Matches pattern of other vendored libs. The `include/` directory disappears entirely per [tracked separately] Y+8d. |
| `include/ICC-Profiles/` + `include/VideoHD.icm` | static ICC profile binary data | **resource asset** | Move into `src/metadata/profiles/` (consumer-co-located with `metadata/`) per Probe 2 ([tracked separately]). |

The `include/` directory disappears entirely (per the Y+8d plan in ) once all of these moves complete. No new headers ever land in `include/`; ADR-0003's co-located shape is the only direction going forward.

**4. Public interface (proposed `hdrs`).**

```
src/cli/options.h — Options struct + CLI11 declare_options(App&, Options&)
src/cli/cli_mode.h — run_query(opts), run_compare(opts), run_convert(opts) → int
src/cli/server_mode.h — run_server(opts) → int
src/cli/library_initialiser.h — class LibraryInitialiser (RAII singleton)
src/cli/system_resources.h — detect_available_cores, detect_available_memory
src/cli/report.h — emit_json_report(...), emit_json_cli_arg_error(...) (lifted from SipiReport.h)
src/cli/terminate_handler.h — my_terminate_handler declaration
src/cli/sentry_env.h — sentry_config_from_env → optional<SentryConfig>
```

**5. Private surface.** ~7 .cpp files totaling ~1,400 lines lifted from `sipi.cpp`. The ~50-line `src/main.cpp` is the only top-level binary source after the move.

**6. Depth signal.** Pre-Probe-10 `sipi.cpp` is **`god-function` / `god-file`** — 1,712 lines, one main function dominating, six unrelated responsibility groups (option declarations, library init, Sentry init, CLI dispatchers, server bootstrap, signal handling). Post-Probe-10:

| Package | hdrs / cpp | Verdict |
|---|---|---|
| `src/cli/` | 8 / 7 / ~1,400 lines | **deep** (each sub-module has its own focused responsibility behind a small public surface) |
| `src/main.cpp` | 0 / 1 / ~50 lines | **thin** (cc_binary entry point; mode dispatch only) |

**Specific code smells eliminated:**
- **1,280-line `int main`** → 50-line dispatch.
- **Sentry init duplicated in main + close paths** (the `sentry_close` calls scattered across CLI-mode error branches at `:1060`, `:1067`, `:1174`, `:1181`, etc.) — replaced by RAII or by a single `Sipi::observability::close_sentry` call at the right point. Probe 9a's `sentry_init.{h,cpp}` makes this clean.
- **`detect_available_cores` / `detect_available_memory`** as static functions in `sipi.cpp` — promoted to a tested module (`system_resources.{h,cpp}`), unit-testable in isolation.
- **`my_terminate_handler`** at `sipi.cpp:400` — moved into its own small file; lifecycle / signal-handling concerns separated from arg-parsing concerns.
- **`SipiReport`'s top-level placement** — finally settled (Probe 8 deferred decision): lives in `cli/report.{h,cpp}` since it's purely CLI-mode (`--json` flag).

**7. Verdict.** **`god-function`** today; **deep package + thin entry binary** post-Probe-10.

**8. Action.** Two parents.

**`cli/` extraction parent — seven sub-PRs:**

a. **Extract `LibraryInitialiser`** to `src/cli/library_initialiser.{h,cpp}`. Pure RAII singleton; no dependencies on the rest of sipi.cpp.

b. **Extract `system_resources`** (`detect_available_cores`, `detect_available_memory`) to `src/cli/system_resources.{h,cpp}`. Pure utilities; unit-testable in isolation (cgroup parsing, sysctl reading).

c. **Extract `my_terminate_handler`** + Sentry-env reading to `src/cli/terminate_handler.{h,cpp}` + `src/cli/sentry_env.{h,cpp}`. Small.

d. **Lift `SipiReport`** (Probe 8 deferred) — `src/SipiReport.{h,cpp}` → `src/cli/report.{h,cpp}`. File rename; namespace `Sipi::` → `Sipi::cli::`.

e. **Extract CLI11 `Options` struct + `declare_options`** to `src/cli/options.{h,cpp}`. ~400 lines of `sipiopt.add_option(...)` calls + the `Options` struct that holds parsed values.

f. **Extract CLI-mode dispatchers** (`run_query`, `run_compare`, `run_convert`) to `src/cli/cli_mode.{h,cpp}`. ~600 lines covering the three CLI modes plus the `--json` integration.

g. **Extract server-mode bootstrap** to `src/cli/server_mode.{h,cpp}`. Lua config loading + `to_server_config(SipiConf)` (gated on Probe 9b [tracked separately]) + `Server::Server(ServerConfig)` (gated on Probe 9b [tracked separately]) + `server.run`.

h. **Slim `main`** — replace `sipi.cpp` with a ~50-line `src/main.cpp` calling the per-mode dispatchers. Original `sipi.cpp` deleted.

i. **Bazel package promotion** `//src/cli:cli` (Y+8N) — gated on a..h + [tracked separately].

**Closing sweep parent — six sub-PRs:**

j. Delete `SipiCommon.{h,cpp}` (empty namespace).

k. Move `SipiFilenameHash.{h,cpp}` into `src/cache/filename_hash.{h,cpp}` — folds into Probe 1 cache parent ([tracked separately]).

l. Extract `SipiError` to new package `src/errors/sipi_error.{h,cpp}` — small new Bazel package or fold into a small "common" package alongside.

m. Move `SipiImageError.hpp` into `src/image/image_error.h` — folds into Probe 4 image parent ([tracked separately]).

n. Move `favicon.h` into `src/route_handlers/favicon.h` — folds into Probe 5 route_handlers parent ([tracked separately]).

o. Move `CLI11.hpp` from `include/` to `ext/CLI11/` — matches pattern of other vendored single-header libs.

p. Move `ICC-Profiles/` + `VideoHD.icm` into `src/metadata/profiles/` — folds into Probe 2 metadata parent ([tracked separately]).

After (j)..(p): `include/` directory is empty (or contains only the `*.h.in` CMake-generated headers). The Y+8d step in [tracked separately] deletes `include/` entirely.

**9. Open questions for later (post-exercise).**

- **CLI11 namespace migration** — `Sipi::cli::Options` aggregates ~50 fields parsed from CLI11. Could later tighten with `std::optional<T>` for never-set values (currently uses sentinel values). Defer.
- **Mode-dispatch shape** — today's pattern (`if (opts.has_X) return run_X(opts)`) is fine. A more idiomatic C++ shape would be `std::variant<QueryMode, CompareMode, ConvertMode, ServerMode>` constructed from `Options` and visited. Defer; current shape is KISS.
- **`SipiError` vs domain-error split** — Probe 10 recommends extracting `SipiError` to `src/errors/`, leaving `SipiImageError` in `src/image/`. The `cache/`, `iiif_parser/`, `format_handlers/`, `metadata/` modules each likely have their own error subclasses today (or should — audit during sweep step (l)). Each error type lives with its domain; `SipiError` (the root) lives in `errors/`.

## Closing directory sweep — final state

Post-Probes-1-10 + Y+8 layout flip, `src/` contains exactly these packages:

```
src/cache/ (Probe 1)
src/metadata/ (Probe 2)
src/format_handlers/ (Probe 3)
src/image/ (Probe 4) + image_processing/ (Probe 4)
src/iiif_parser/ (Probe 4 follow-up + )
src/route_handlers/ (Probe 5) + server/ (Probe 5)
src/lua_bindings/ (Probe 6) + permission/ (Probe 6)
src/throttling/ (Probe 7)
src/logging/ (Probe 8)
src/observability/ (Probe 9a)
src/config/ (Probe 9b)
src/cli/ (Probe 10)
src/errors/ (Probe 10 closing sweep)
src/main.cpp (cc_binary entry, ~50 lines)
src/BUILD.bazel (top-level cc_binary declaration)
```

`include/` is deleted entirely (per Y+8d). `shttps/` is a separate context (not in this exercise's scope).

13 SIPI-side packages + 1 binary. The exercise's hypothesis (every glossary term corresponds to either a deep module, a scattered concept to extract, or a language-only term) is confirmed: the 13 packages map cleanly to the language, with the umbrella terms (Throttling, Observability, Image processing, Permission) showing up as both vocabulary umbrellas and code seams.

<!-- END PROBE ROWS -->

## Glossary delta register

Pending edits to [`UBIQUITOUS_LANGUAGE.md`](../../UBIQUITOUS_LANGUAGE.md), accumulated from probe side-effects. Applied in a single editing pass at the end (or at natural batching points), so the glossary changes once with full context, not once per probe.

| Term | Action | Source probe | Note |
| --- | --- | --- | --- |
| **Image shape** | add | Probe 1 | Intrinsic shape of a source image: `(img_w, img_h, tile_w, tile_h, clevels, numpages, nc, bps)`. Read by a format handler from the master file. Replaces the parasitic `SipiCache::SizeRecord`. Per ADR-0004. |
| **Operating mode** | add (umbrella) | Probe 1 | Two sub-terms — `Server mode` and `CLI mode`. The asymmetry between which format handler dominates read vs. write is architecturally load-bearing for the service-master-format fast path. |
| **Server mode** | add | Probe 1, refined Probe 3 | Long-running HTTP server. Reads service masters in service master format from `image root`; writes IIIF representations to the cache. The hot path for service-master-format shape reads. |
| **CLI mode** | add | Probe 1, refined Probe 3 | One-shot invocation. **Today**: reads arbitrary source format; writes a service master in service master format with full Essentials packet. **Future direction**: writes archival masters for OAIS-compliant external archive storage; the archival → service-master conversion is a separate workflow step (actor TBD; surfaces only when a later probe hits it). |
| **Service master** | add | Probe 1 (renamed from `Master file`, Probe 3) | The on-disk file under `image root` that SIPI's server reads to fulfill IIIF requests. Codebase variables `infile` / `origpath`. Currently produced by SIPI CLI mode; future workflow has it derived from an `archival master` by a separate conversion step. |
| **Service master format** | add | Probe 1 (renamed from `Master format`, Probe 3) | The format of service masters. Optimized for fast random-access IIIF serving. Currently JP2; pyramidal TIFF is the planned successor. |
| **Archival master** | add | Probe 3 | The format-stable, lossless preservation copy of an image, stored in the OAIS-compliant external archive. Distinct from a `service master` by *purpose*: archival masters prioritize preservation properties (format stability, lossless or near-lossless compression, no service-side optimizations); service masters prioritize fast random access. SIPI CLI mode produces archival masters in the future workflow; SIPI server mode does not read them. |
| **Archival master format** | add | Probe 3 | The format of archival masters. Per user direction, plain (non-pyramidal) TIFF for format-stability reasons — pyramids are a service-side optimization, not a preservation property, and archival policy explicitly rejects them. |
| **Pyramidal TIFF** | add (Probe 1), refined Probe 3 | Probe 1, refined Probe 3 | Multi-resolution TIFF variant storing the same image at multiple decode levels in a single file. Supports efficient decode-level selection without full-resolution decoding. **A service-master-format only** — pyramids are a service-side optimization rejected by the archival master format. Planned successor to JP2 as the sole service master format. |
| **Cache pin** | add (provisional) | Probe 1 | Per-file in-use refcount that prevents eviction while a representation is being served. Currently `SipiCache::blocked_files` + `check(block_file=true)` + `deblock`; refactor to RAII `BlockedScope`. Confirm name during `SipiHttpServer` probe. |
| **Essentials packet** | sharpen | Probe 1 | Existing definition lists original filename, mimetype, hash type, pixel checksum, optional ICC. Extend schema with **image-shape fields** so server-mode shape lookup can read them at known offset rather than parsing the codestream / TIFF tags. Per ADR-0004. The schema is format-agnostic; the embedding mechanism (JP2 box vs TIFF tag) is handler-specific. |
| **Route handler** | sharpen → umbrella | naming discussion (Probe 1 follow-up) | Promote to umbrella term: URL-pattern-bound request logic. Two sub-types depending on implementation language (`C++ route handler`, `Lua route handler`). Resolves the term overload between the existing Lua-only definition and the planned `route_handlers/` C++ directory. |
| **C++ route handler** | add | naming discussion (Probe 1 follow-up) | A `shttps::RequestHandler` callback registered at server startup. Examples: `iiif_handler`, `file_handler`. Compiled in. Lives in `src/route_handlers/` post-Probe 5 refactor. Note: `shttps::RequestHandler` remains the framework type; *C++ route handler* is the SIPI-side domain term for instances of it. |
| **Lua route handler** | add | naming discussion (Probe 1 follow-up) | A Lua script bound to a URL pattern, loaded dynamically. Examples: upload, admin endpoints in `scripts/`. (Existing "Route handler" glossary entry redirects here.) |
| **Format handler** | sharpen | Probe 1 follow-up | Existing definition is correct; note the directory rename `formats/` → `format_handlers/` for self-documentation, matching the glossary term directly. |
| **Essentials packet** | sharpen further | Probe 2 | Current wire format is pipe-delimited text without escaping or schema versioning — brittle (any `\|` in `origname` corrupts parse) and not forward-evolvable. Per ADR-0005 the wire format migrates to versioned CBOR; the in-memory schema is the same. ADR-0004's image-shape additions land in the new wire format. |
| **Test seam** | add | Probe 2 | A header deliberately kept in a module's `internal/` subdirectory with `visibility` restricted to that module + that module's tests. Used to expose pure helpers for explicit testing without broadening production coupling. Canonical example: `metadata/internal/icc_normalization.h` (formerly `SipiIccDetail.h`). The pattern replaces comment-as-policy ("No production code outside X should include this header") with a build-graph invariant. |
| **Codec** | sharpen | Probe 3 | Existing definition is correct. Sharpen with the canonical four-codec list: Kakadu (JP2), libtiff (TIFF), libpng (PNG), libjpeg (JPEG). Note `webp` is in the project's external-deps set but no `SipiIOWebp` class exists today. |
| **`read_shape`** (Format handler API) | rename | Probe 3 | The existing `SipiIO::getDim(filepath) → SipiImgInfo` virtual is renamed to `read_shape` for self-documentation: `getDim` implies just dimensions, but the return type carries full image shape (dimensions + tile + clevels + numpages + nc + bps + orientation + mimetype + sub-image resolutions). Per ADR-0004 (amended). |
| **Object storage** | add | Probe 3 follow-up | The production access model for service masters in the 3-6 month direction: SIPI server reads service masters via S3 range GETs over HTTP. Today's transitional state is NFS-mounted ZFS spinning disk (still network-accessed; round-trip costs already exist). Local-filesystem `image root` becomes the dev/test scenario only. The `cache/` module stays local in both states (performance optimization; cached representations on the hot path can't pay remote-access cost). |
| **Range GET** | add | Probe 3 follow-up | An HTTP `GET` on an S3 object with a `Range:` header bounding the byte range to fetch. The unit of S3 access. Each range GET is a network round-trip (~1-10ms typical); minimizing them is the load-bearing perf goal once SIPI moves to S3. Per ADR-0004: pre-decode reads aim for *one* range GET to fetch the Essentials packet (with shape + file-structure offsets), then *one* targeted range GET for the data SIPI actually needs. |
| **Essentials packet** | sharpen further | Probe 3 | Per ADR-0004 (expanded scope): the packet's role broadens from "shape cache" to "shape + S3-access file-structure index." The load-bearing rationale shifts to remote storage (S3 in 3-6 months, NFS today). Contents: image shape (8 fields), per-format file-structure offsets (TIFF: per-IFD offset/size; JP2: codestream + per-resolution offsets), ICC profile, original filename / mimetype / hash / pixel checksum. Wire format CBOR (ADR-0005). Position: a known fixed prefix offset in the file (TIFF tag in the first IFD; JP2 UUID box near the start) so SIPI can fetch with one range GET of the prefix. |
| **Image processing** | add | Probe 4 | Umbrella term for the free-function module (`src/image_processing/`) over `const Image&`: crop, scale, rotate, colour conversion, channel ops, bit-depth reduction, dithering, watermark application, comparison, arithmetic. Replaces the ~12 image-processing methods on the `SipiImage` god-object per ADR-0007. Free-function-over-value-type maps cleanly to Rust traits in the eventual port. |
| **Image** | sharpen | Probe 4 | The domain-level term ("a pixel-bearing artefact processed through the IIIF pipeline") stays correct conceptually. The *code-level* `Image` class becomes a narrow value type post-refactor (geometry + photometric + RAII pixel buffer + metadata composite; ~15 public methods) per ADR-0007. Image-processing behaviour moves to free functions in `image_processing/`. |
| **Tee sink** | add (provisional) | Probe 4 | Composition primitive in the `OutputSink` variant per ADR-0006: `TeeSink { std::vector<OutputSink> outputs; }` broadcasts each output chunk to multiple sub-sinks. Preserves SIPI's existing dual-write optimization (encoder writes simultaneously to HTTP socket + cache file). Generalises to write-through to S3 / other sinks. Provisional naming — confirm during implementation. **Confirmed Probe 5** as a variant alternative in `format_handlers/output_sink.h`. |
| **Server (Sipi side)** | sharpen | Probe 5 | Today: `class SipiHttpServer : public shttps::Server`. Post-refactor: `Sipi::Server` in `src/server/` *composing* (not inheriting) `shttps::Server`. Composition aligns with the strangler-fig direction in [ADR-0001](../adr/0001-shttps-as-strangler-fig-target.md): when shttps moves to Rust, only the composition target changes; SIPI's server class stays. The class becomes paper-thin (~180 lines): constructor, `run` (realpath imgroot validation + `register_routes` + delegation), runtime-resource owners. |
| **Server config** | add | Probe 5 | Immutable C++ value type aggregating SIPI server configuration: imgroot, docroot, prefix-as-path, scaling-quality, jpeg-quality, j2k-compression-profiles, max-pixel-limit, salsah-prefix, dirs-to-exclude, plus shttps-side fields (port, threads, ssl, jwt). Replaces the 30-setter half-built-state pattern on `SipiHttpServer`. The C++ counterpart of `SipiConf` (Lua-bound config object). Detailed field list deferred to Probe 9. |
| **Server context** | add | Probe 5 (renamed from `Route context` per Probe 6 follow-up) | Typed, server-scope dependency bundle passed to `register_routes` and stored as the `user_data` argument of shttps's `add_route`. Contains the const-references and pointers each route handler actually needs (`Cache&`, `RateLimiter*`, `MemoryBudget*`, `const ServerConfig&`, `start_time`, `resolved_imgroot`). Replaces today's `static_cast<SipiHttpServer*>(user_data)` god-pointer that reaches into ~30 fields when only ~5 are actually used. Bazel `--strict_deps` enforces against accidental widening. **Server-scope** (set once at registration, shared across requests); request-scope state stays in function arguments. Parallels [Lua context](#glossary-delta-register) (the server-scope bundle for Lua FFI). |
| **Output sink** | add | Probe 5 | Typed sum type for write-path I/O destinations: `std::variant<FilePath, StdoutSink, HttpSink, TeeSink>`. Format-handler `write` API takes one, replacing magic-string sentinels (`"-"` for stdout, `"HTTP"` for HTTP server). `HttpSink` carries opaque write/finalize callbacks, so `format_handlers/` does not depend on `shttps/`. `TeeSink` composes outputs for the dual-write-to-HTTP-and-cache optimization. Per ADR-0006. Lives in `format_handlers/output_sink.h`. |
| **Input source** | add | Probe 5 | Typed sum type for read-path I/O sources: `std::variant<FilePath, RangeSource>`. Symmetric to `Output sink`. Format-handler `read` and `read_shape` API takes one, enabling the S3 transition per ADR-0004 without changing handler signatures. `RangeSource` carries an opaque byte-range-GET callback + total size; format handlers wrap it in codec-specific source-manager adapters (libtiff `TIFFClientOpen`, kakadu `kdu_compressed_source` subclass, libjpeg `jpeg_source_mgr`, libpng `png_set_read_fn`). Per ADR-0006 (amended in this session). Lives in `format_handlers/input_source.h`. |
| **Range source** | add | Probe 5 | Variant alternative of `Input source` covering any backend that supports byte-range reads via callback: S3, Azure Blob, GCS, in-memory buffers. Names the *capability* (range reads), not the location (remote). Production target post-3-6-month S3 migration per ADR-0004. |
| **Watermark** | confirm (was Probe 1 candidate) | Probe 1 candidate → confirmed Probe 5 | Overlay image applied to an `Image` before serving when `Permission.watermark` is set. The path on `Permission.watermark` is loaded into a regular `Image` via `format_handlers/SipiIOTiff::read` (or its successor), then applied via free function `apply_watermark(Image& target, const Image& watermark)` in `image_processing/watermark.{h,cpp}`. Watermark presence extends the `Canonical URL` into the `Cache key` (`/0` or `/1` suffix). Replaces the misplaced `read_watermark` free function (deleted) and `SipiImage::add_watermark` method (deleted) — load + apply are decoupled. |
| **Cache pin** | confirm (was provisional) | Probe 1 → confirmed Probe 5 | Per-cachefile in-use refcount preventing eviction while a representation is being served. RAII type `BlockedScope` (per Probe 1) replaces today's manual `cache->check(infile, canonical, true)` paired with three explicit `cache->deblock(cachefile)` calls per `serve_iiif` branch (try-success, two catch-failures). Confirmed by reading `SipiHttpServer.cpp:1758-1804`. |
| **C++ route handler** | sharpen | Probe 5 | Existing definition (Probe 1 follow-up): a `shttps::RequestHandler` callback registered at server startup. Probe 5 sharpens the registration mechanism: routes are registered via the `register_routes(shttps::Server&, const ServerContext&)` free function in `route_handlers/route_handlers.h`, not by code inside the server lifecycle. Adding a new C++ route is a code change inside `route_handlers/`, not inside `server/`. |
| **Lua context** | add | Probe 6 | Server-scope typed dependency bundle passed to Lua-binding C functions via shttps's `add_lua_globals_func(func, user_data=&lua_context)`. Replaces today's `sipiserver` Lua lightuserdata global pointing at `SipiHttpServer*` (god-pointer). Carries the typed slice of server state Lua bindings actually use (initially `Cache&`; future entries when Lua admin scripts grow). Per-request data flows through Lua function arguments (e.g. `pre_flight(prefix, identifier, cookie)`), not through this bundle. **Server-scope** (set once at registration). Parallels `Server context` (the server-scope bundle for C++ route handlers, [Probe 5](#glossary-delta-register)). Lives in `lua_bindings/lua_context.h`. |
| **Permission** | sharpen | Probe 6 | Existing definition (verdict and shaping output returned by a preflight script, with permission type + optional sub-fields like `infile`, `watermark`, size cap). Probe 6 sharpens the C++ representation: `Permission = std::variant<AllowPermission, LoginPermission, ClickthroughPermission, KioskPermission, ExternalPermission, RestrictPermission, DenyPermission>` — per-type structs for compile-time exhaustiveness. Maps 1:1 to a Rust enum at port time. The "DenyPermission with infile" anti-state is unrepresentable. Lives in own package `permission/permission.h`. Lua-side parsing (LuaValstruct → Permission) lives in `lua_bindings/preflight.cpp`; route handlers consume only the typed value, never `LuaValstruct`. |
| **Lua bindings** | add (umbrella) | Probe 6 | Umbrella term for SIPI's FFI clusters exposing C++ to Lua: `helper.*` (utility — `filename_hash`), `SipiImage` (datatype + 12 image-processing methods), and the preflight callbacks (`pre_flight` / `file_pre_flight`). The historical `cache.*` cluster was removed in Probe 6 (server-state mutations move to dedicated C++ routes; cache state inspection moves to Prometheus). Lives in `lua_bindings/`. |
| **Cache** | sharpen | Probe 6 | Existing definition (file-based LRU + dual-limit eviction + crash recovery). Probe 6 sharpens the inspection surface: cache state is exposed exclusively through Prometheus metrics (`sipi_cache_size_bytes`, `sipi_cache_size_limit_bytes`, `sipi_cache_files`, `sipi_cache_files_limit`, `sipi_cache_hits_total`, `sipi_cache_misses_total`, `sipi_cache_evictions_total`, `sipi_cache_skips_total`), **not** through Lua bindings. The historical `cache.*` Lua cluster (8 bindings: `size`, `max_size`, `nfiles`, `max_nfiles`, `path`, `filelist`, `delete`, `purge`) was removed in Probe 6 along with `scripts/cache.lua`. If external cache control is needed in the future, a dedicated C++ route in `route_handlers/` is the canonical surface, not a Lua script. |
| **Lua route handler** | sharpen | Probe 6 | Existing definition (Probe 1 follow-up): a Lua script bound to a URL pattern. Probe 6 sharpens the *role*: Lua route handlers are for **request-shaping** (preflight permission decisions, custom content endpoints like `upload.lua`). **Server-state mutation** (cache management, server lifecycle, config reload) is implemented as a dedicated `C++ route handler`, not a Lua script. The deletion of `scripts/cache.lua` in Probe 6 is the canonical example. |
| **Throttling** | rename (was `Backpressure`) | Probe 7 | Umbrella term for SIPI's load-driven request-rejection policies. Renamed from `Backpressure` because backpressure technically denotes upstream feedback flow control (TCP windows, Reactive Streams, bounded channels) — the consumer signals the producer to slow down. SIPI does not do this: it rejects under load with HTTP 429/503/400, which is *load shedding* / *throttling*. The new name was chosen specifically to (a) describe the rejection-style mechanism accurately (HTTP 429 = throttling response, common AWS / Azure / Kubernetes terminology), and (b) avoid colliding with `Permission` (the identity-driven authorization decision, also a form of admission control). Comprises three sub-policies: `Decode memory budget` (process-wide instantaneous decode RAM), `Rate limiter` (per-client sliding-window pixel rate), `Output size guard` (intrinsic max-output-pixels ceiling). All three fire at one post-cache gate site per Probe 7 + ADR-0008. Aliases to avoid: backpressure, flow control, admission control. Lives in `src/throttling/` post-Probe-7. |
| **Output size guard** | add | Probe 7 | Third Throttling sub-policy. Stateless rejection of requests whose IIIF output dimensions exceed `max_pixel_limit` (i.e. `requested_w * requested_h > max_pixel_limit`). Returns HTTP 400 Bad Request. Currently inline at `SipiHttpServer.cpp:1631-1644`; promoted to a small class in `throttling/output_size_guard.{h,cpp}` post-Probe-7. Distinct in *kind* from the other two Throttling sub-policies — its trigger is intrinsic (the request's output is too big), not load-dependent (the server is stressed) — but shares the gate-site location, the OFF/ENFORCE shape, and the protection-against-oversized-work purpose. The slightly loose fit under "Throttling" was accepted in Probe 7 in preference to splitting the umbrella for one stateless policy. |
| **Rate limiter** | sharpen | Probe 7 | Existing definition correct (per-client request-rate ceiling enforced before decode admission). Probe 7 sharpens placement: fires **post-cache** per [ADR-0008](../adr/0008-rate-limit-post-cache.md). Cache-hit responses are not rate-limited. The historical pre-cache placement was an over-fit to a non-existent attack model — harvest bots sweep unique URLs (cache-miss-dominant workload), so cache-hit rate-limiting was protecting against an attack scenario that doesn't appear in production. Code-level: lives in `throttling/rate_limiter.{h,cpp}` post-package promotion; class renamed `Sipi::SipiRateLimiter` → `Sipi::throttling::RateLimiter`. The stranded helper `resolve_client_id` (today at `SipiHttpServer.cpp:324`) becomes `Sipi::throttling::client_id_from(...)` in `throttling/client_id.{h,cpp}`. |
| **Decode memory budget** | sharpen | Probe 7 | Existing definition correct. Probe 7 sharpens placement: lives in `throttling/memory_budget.{h,cpp}` post-package promotion; class renamed `Sipi::SipiMemoryBudget` → `Sipi::throttling::MemoryBudget`; helper `estimate_peak_memory` moves to `throttling/internal/peak_memory.h` as a Test seam (Probe 2 pattern). Gate-site location unchanged — already post-cache today. |
| **Cache** | sharpen further | Probe 7 | Existing definition (Probe 6 sharpening on inspection-via-Prometheus) carries forward. Probe 7 adds a placement note: cache-hit responses **bypass all three Throttling policies entirely** (per ADR-0008). The cache-hit short-circuit returns before any throttling gate fires. Cache hits are genuinely free at the response layer — no rate-limit accounting, no memory-budget acquire, no output-size check. This is a load-bearing fact for the throttling threat model: the rate limiter exists to mitigate harvest bots, which are by construction cache-miss-dominant. |
| **Example dialogue** (UBIQUITOUS_LANGUAGE.md) | cleanup | Probe 7 | The line in the example dialogue ("we reject with backpressure — the request never touches the codec") rewords during the batched glossary edit pass to use the new umbrella name and the now-three-sub-policy framing. Cosmetic cleanup; tracked here so it isn't missed when the delta register flushes to `UBIQUITOUS_LANGUAGE.md`. |
| **Logger** | add | Probe 8 | Basic logging primitives + level / mode control, used across both SIPI and shttps. Public API: `log_debug` / `log_info` / `log_warn` / `log_err`, `set_log_level` / `get_log_level`, plus four SIPI-only mode flags (`set_cli_mode`, `is_cli_mode`, `set_json_mode`, `is_json_mode`) that route logs to stderr when CLI mode emits a JSON document on stdout. Lives in `src/logging/` post-Probe-8. The shttps consumption of Logger is documented as a known layering leak in `CONTEXT.md` (the second leak alongside `SipiMetrics::instance`); both leaks await the strangler-fig Rust port. |
| **Mutation script** | add (anti-pattern) | Probe 8 | Formalizes the [Probe 6 principle](#probe-6--lua_bindings-renamed-from-sipilua-decomposition). A *Lua route handler* that mutates **server state** (cache eviction, server lifecycle, filesystem cleanup, config reload, …) is a Mutation script and is forbidden in SIPI. The canonical surface for server-state mutation is a *C++ route handler* (or a signal handler for lifecycle). Probe 6 deleted the `cache.*` cluster + `cache.lua` ([PR #619](https://github.com/dasch-swiss/sipi/pull/619)); Probe 8 deletes `exit.lua`, `clean_temp_dir.lua`, `admin_upload.lua`, `debug.lua`. Lua route handlers are kept only for **request-shaping** (preflight permission decisions, content-shaping endpoints like `upload.lua`). |
| **CLI report** | add (deferred) | Probe 8 → Probe 10 | The structured JSON document `SipiReport::emit_json_report` writes to stdout when the `--json` CLI flag is set. Schema mirrors `ImageContext` (the Sentry context struct). Used so environments without a Sentry DSN still get the full diagnostic payload. Glossary entry deferred to Probe 10 (`sipi.cpp` entry-point decomposition); name + final placement settle there. |
| **Lua route handler** | sharpen further | Probe 8 | The Probe 6 sharpening (request-shaping vs server-state-mutation) carries forward. Probe 8 enumerates the surviving content-shaping cluster after PR #619 + Probe 8 deletions: `upload.lua`, `send_response.lua` (utility module), `token.lua`, `orientation.lua`. Test-only scripts (`test1.lua`, `test2.lua`, `test_sqlite.lua`, `test_functions.lua`) are not Lua route handlers — they're test scaffolding and move to `test/scripts/` per Probe 8. |
| **Observability** | add (umbrella) | Probe 9 | Umbrella term for the operational telemetry surface. Comprises three sub-concerns: **Metrics** (Prometheus instrumentation), **Sentry context** (the `ImageContext` + `capture_image_error` pattern for per-image-error capture), and **Connection metrics adapter** (the canonical inversion-of-control bridge between `shttps::ConnectionMetrics` and SIPI's metrics registry). Lives in `src/observability/` post-Probe-9. Distinct from `src/logging/` (which handles SIPI's structured-log primitives) — observability is the umbrella for what crosses the SIPI/shttps boundary as telemetry; logging is a SIPI-side utility. Both await the strangler-fig Rust port for the boundary-leak fixes (per `CONTEXT.md`). |
| **Metrics** | add | Probe 9 | The Prometheus instrumentation surface. ~25 metrics today: counters (cache hits/misses/evictions/skips, image-too-large, client-disconnects, memory-alloc-failures, rate-limit decisions, decode-memory decisions, rejected-connections), gauges (waiting-connections, cache size/files, decode-memory-budget/used, rate-limit-clients-tracked, build-info), histograms (request duration, decode-memory estimate). Exposed at `GET /metrics` (text-format `text/plain; version=0.0.4`). Singleton today (relocated, not redesigned, in Probe 9); the `shttps/Server.cpp → SipiMetrics::instance` consumption is documented as the first known layering leak in `CONTEXT.md`. Code-level: lives in `observability/metrics.{h,cpp}` post-Probe-9; class renamed `SipiMetrics` → `Sipi::observability::Metrics`. |
| **Sentry context** | add | Probe 9 | The error-capture payload sent to Sentry from SIPI. Comprises an `ImageContext` struct (12 fields: `input_file`, `output_file`, `output_format`, `width`, `height`, `channels`, `bps`, `colorspace`, `icc_profile_type`, `orientation`, `file_size_bytes`, `request_uri`) plus the `capture_image_error(error_message, phase, ctx, mode)` entry point. `phase` is `"read" / "convert" / "write" / "cli_args"`. `mode` is `SipiMode::CLI` (blocking flush, 2s) or `SipiMode::Server` (non-blocking flush). Lives in `observability/sentry.{h,cpp}` post-Probe-9; thread-safe (tags attached to event, not global scope). |
| **Connection metrics adapter** | add | Probe 9 | The canonical inversion-of-control bridge for cross-context telemetry. shttps owns the `shttps::ConnectionMetrics` strategy interface (3 methods: `onConnectionsRejected`, `onWaitingConnectionsChanged`, `onRequestComplete`); SIPI installs a `Sipi::observability::ConnectionMetricsAdapter` instance on `shttps::Server` at startup that translates events into Prometheus updates on the `Metrics` singleton. shttps holds no reverse dependency on `Sipi::` symbols. **This is the prescribed pattern for resolving cross-boundary observability without violating one-way SIPI → shttps direction**; the existing `SipiMetrics::instance` leak from `shttps/Server.cpp` (CONTEXT.md known-layering-leak entry #1) and the Logger leak (Probe 8, leak #2) both await analogous fixes. |
| **Server config** | add | Probe 9 | Immutable C++ value type aggregating SIPI runtime configuration: 38 fields in 7 logical groups (Network/TLS, Image storage, Encoding, Cache, Lua, Webserver/Auth/Logging, Concurrency, Throttling). Constructed by `Sipi::config::to_server_config(const SipiConf&) → ServerConfig` after Lua parsing. Replaces the 30-setter half-built-state pattern on `SipiHttpServer` (`sipi.cpp:1581-1700`). The C++ counterpart of `SipiConf` (Lua-bound parser); the runtime sees only `ServerConfig`, never `SipiConf`. **Unblocks Probe 5's deferred field list .** Lives in `config/server_config.{h,cpp}` post-Probe-9. |
| **`SipiMode`** | sharpen (provisional → confirmed) | Probe 9 | Existing CLI vs Server distinction confirmed and elevated. Used by Sentry flush behaviour (CLI: blocking 2s flush before exit; Server: non-blocking flush to avoid stalling request threads), by error-reporting paths (`capture_image_error(..., SipiMode mode)`), and by the route-handler / format-handler asymmetries documented earlier (Server reads service masters; CLI writes them). Promoted from informal usage to glossary term. |
| **CLI report** | add (was deferred from Probe 8) | Probe 10 | The structured JSON document that `emit_json_report` writes to stdout when the `--json` CLI flag is set. Schema mirrors `ImageContext` (the Sentry context — see [`Sentry context`](#glossary-delta-register)) so environments without a Sentry DSN still get the full diagnostic payload. Top-level keys: `status` (`"ok"` / `"error"`), `phase` (`"cli_args"` / `"read"` / `"convert"` / `"write"`), `error_message`, and an `image` object populated from `ImageContext`. On `phase == "cli_args"` the `image` object is omitted (no image was loaded yet). Lives in `cli/report.{h,cpp}` post-Probe-10 (Probe 8 deferred decision settled here — placement is CLI-only, not observability). |
| **CLI mode** | confirm + elevate | Probe 10 | Existing entry from Probe 1 confirmed and elevated. Post-Probe-10 the CLI/server boundary is a *code* boundary: `src/cli/cli_mode.{h,cpp}` (`run_query`, `run_compare`, `run_convert`) is exclusively CLI mode; `src/cli/server_mode.{h,cpp}` (`run_server`) is exclusively server mode. Each maps to one branch of the slim `int main` dispatch. **CLI mode characteristics:** one-shot invocation, blocking Sentry flush, `--json` report on stdout, `set_cli_mode(true)` redirects logs to stderr. |
| **Server mode** | confirm + elevate | Probe 10 | Existing entry from Probe 1 confirmed and elevated to code-boundary status (see CLI mode entry above). **Server mode characteristics:** long-running HTTP server, `ServerConfig` value type at construction (Probe 9b), three Throttling sub-policies at the post-cache gate (Probe 7), the Connection metrics adapter installed on shttps at startup (Probe 9a), non-blocking Sentry flush. |

## Candidate gaps already spotted (pre-probe)

Concepts I expect we'll need to add or sharpen, surfaced from re-reading existing materials before any code archaeology. These are *predictions* — confirm or discard during probes.

- **Watermark** — appears as a sub-field of `Permission`, never elevated. Has cache-key implications.
- **Page** — embedded inside `Identifier`'s definition ("page ordinal for multi-page resources"). Multi-page TIFF / PDF handling is likely its own concern.
- **Path resolution / sandboxing** — "with traversal validation" appears inside `Image root`'s definition. The validation is a real act, likely a deep module.
- **Configuration** — `Config` not a domain term; `SipiConf` exists in code.
- **Conversion** — the project overview's verb of record ("efficient image format conversions while preserving metadata"); not in the glossary.
- **Mime detection** — libmagic is a dep; mime-typing has security implications. Unnamed.
- **Sentry / error reporting** — operational; mentioned in code, absent from language.
- **Tile** — IIIF tiles are a derived form of `Region` with strong cache implications. Unnamed.
- **CLI mode vs server mode** — the binary has two operating modes; in project overview, not in glossary.

## Cross-references

- [`UBIQUITOUS_LANGUAGE.md`](../../UBIQUITOUS_LANGUAGE.md) — canonical SIPI glossary. The probe input.
- [`CONTEXT-MAP.md`](../../CONTEXT-MAP.md) — SIPI ↔ shttps boundary. Bounds the scope of this exercise to SIPI-side modules; shttps is treated as one external module.
- [`CONTEXT.md`](../../CONTEXT.md) — SIPI-side seam types. Names the four primary seam types (`Server`, `Connection`, `RequestHandler`, `LuaServer`).
- [ADR-0001 — shttps as strangler-fig target](../adr/0001-shttps-as-strangler-fig-target.md) — long-term direction; constrains how we reshape SIPI ↔ shttps seams.
- [ADR-0003 — Module-co-located source and tests](../adr/0003-module-co-located-source-and-tests.md) — defines the unit ("Bazel package"), the file layout, and the `--strict_deps` enforcement model. Read first.
- [`shttps/CONTEXT.md`](../../shttps/CONTEXT.md) — out of scope for this exercise *except* to confirm that the SIPI-side modules respect the documented seam.

## Resume protocol (historical — exercise complete)

This section described how to cold-start re-entry into the live exercise. The exercise is complete; the protocol no longer applies. Retained as historical context for any future similar exercise that adapts this method.

## Method invariants (don't drift on these across sessions)

- **Bazel package = the unit of module.** Don't slip into class-level analysis except as a follow-up *inside* a package.
- **Probe-and-extend, not sweep-first.** Don't start cataloguing concepts that haven't surfaced from a probe.
- **Append, don't rewrite.** Probe rows are history. If a verdict revises, add a `**Revised:**` line; don't edit the original.
- **Batch glossary edits.** Add rows to the delta register per probe; apply to `UBIQUITOUS_LANGUAGE.md` in one editing pass.
- **ADRs are sparing.** Only when all three of (hard to reverse, surprising without context, real trade-off) hold.
- **Bound by [ADR-0001](../adr/0001-shttps-as-strangler-fig-target.md).** Reshaping SIPI ↔ shttps seams is bounded by the strangler-fig direction. SIPI-side modules can absorb work re-homed *from* shttps (see `CONTEXT.md` "Re-homing schedule"), but new SIPI → shttps coupling is out of scope.
- **Aligned with the Bazel migration's Y+8 tracker ([tracked separately]).** The per-module Bazel-package promotions surfaced by these probes (one per module: `cache/`, `metadata/`, `format_handlers/`, `iiif_parser/`, …) are exactly Y+8a..Y+8e in the migration plan — five sequential mechanical PRs, gated on Y+7 cleanup ([tracked separately]) merging. Probe outcomes that promote existing directories to Bazel packages should be considered Y+8 work and dependency-modelled against . Probe outcomes that *create new packages* (e.g. `image/` and `image_processing/` from Probe 4) are also Y+8-positioned but extend the plan's currently enumerated scope; they may land as additional sub-PRs (Y+8f, etc.) when is decomposed (per its note: "Decompose when is in QA").
- **Module directory naming.** `snake_case` for compound words (`iiif_parser/`, `format_handlers/`, `route_handlers/`, `image_shape/`); single word otherwise (`cache/`, `metadata/`, `throttling/`). Plural for collections of sibling types (`format_handlers/` — four format-handler classes; `route_handlers/` — multiple route-callback functions); singular for topics, mass nouns, or single concepts (`cache/`, `metadata/`, `throttling/`, `iiif_parser/`). The `name` answers *what kind of thing this directory is about*, not *how many things are inside*. Aligns with Rust target, Google C++ Style Guide, Abseil / Chromium / BDE conventions. The `.cpp` / `.h` *file*-naming convention (PascalCase `SipiCache.cpp` vs snake_case `cache.cpp` vs BDE-style `sipi_cache.cpp`) is a separate, deferred decision — out of scope for the deep-modules exercise; will get its own ADR if and when it lands.
- **Disambiguate overloaded terms with umbrella + sub-types.** When a glossary term naturally covers multiple variants (different implementation language, different layer, different lifecycle), promote it to an umbrella in `UBIQUITOUS_LANGUAGE.md` and define each variant as a sub-type. Example: `Route handler` umbrella with `C++ route handler` + `Lua route handler` sub-types. This is preferred over silent overload (the source of `CONTEXT.md`'s mid-paragraph clarifications about `RequestHandler` vs `Route handler` and the two `file_handler`s).
- **Rust-aligned, transitional C++.** SIPI's C++ codebase is transitional ahead of the strangler-fig migration to Rust ([ADR-0001](../adr/0001-shttps-as-strangler-fig-target.md)). When choosing between a more-ergonomic C++ pattern that won't survive the Rust port and a less-ergonomic one that will, prefer the latter. Examples: `std::expected<T, E>` over `absl::StatusOr<T>` (the former maps directly to Rust's `Result<T, E>`; the latter implies adopting Abseil, a C++-only commitment); `std::variant<A, B>` over inheritance hierarchies (maps to Rust enums / sum types); RAII + `unique_ptr` over exception-based ownership (maps to Rust's move semantics). Cosmetic ergonomic gaps (e.g. `std::expected`'s lack of a `?` operator) are addressed by small SIPI-local helpers (e.g. a `SIPI_TRY` macro), not by adopting upstream libraries that don't outlive the C++ codebase.
- **Remote-access discipline.** Service masters are accessed remotely — NFS-mounted ZFS today, S3 in 3-6 months. Format-handler implementations and pre-decode logic minimize I/O operations: ideally one fixed-offset prefix read to fetch the Essentials packet (shape + file-structure offsets), then one targeted read for the data needed. Walking IFD chains, parsing box hierarchies one box at a time, or doing repeated small reads to discover offsets are anti-patterns — each is a network round trip. Local cache stays local (performance layer); only service-master-source reads pay remote-access cost. Per ADR-0004's expanded scope.
