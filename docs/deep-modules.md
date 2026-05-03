---
status: in-progress
---

# Deep Modules — working design log

A multi-session exercise to identify Deep Modules (Ousterhout, *A Philosophy of Software Design*) in the SIPI codebase, using the ubiquitous language as a probe and extending the language as gaps surface. The output is the input list and seam shape for the Bazel package layout introduced by [ADR-0003](./adr/0003-module-co-located-source-and-tests.md).

This file is the durable artifact of the conversation. It is intended to be resumed cold in a later session — every decision and convention needed to continue is written down here.

## Goal

Decide which Bazel packages SIPI should ship as, by treating each candidate package as a Deep Module: a directory whose public-header surface is small relative to the complexity hidden behind it. The deliverables, accumulated across sessions, are:

1. A list of Deep Module candidates with verdicts (deep / shallow / scattered / mis-bounded / god-object) and concrete actions (keep / split / merge / extract / rename).
2. A patch list against [`UBIQUITOUS_LANGUAGE.md`](../UBIQUITOUS_LANGUAGE.md) for terms to add, sharpen, or retire — surfaced as side-effects of the module probes.
3. A short list of follow-up ADRs for decisions that meet the bar (hard to reverse, surprising without context, real trade-off).

## Scope

**Unit of "module" = Bazel package.** Per ADR-0003, a SIPI module is a directory under `src/` with co-located `.cpp` / `.h` / `*_test.cpp` and its own `BUILD.bazel` declaring a `cc_library` with explicit `hdrs` and `visibility`. The module's interface is the headers in `hdrs`; its depth is what sits behind them.

This unit was chosen over two alternatives:

- **Class-level** (e.g. is `SipiCache` itself deep?). Useful follow-up *inside* a package; not the load-bearing decision. Falls out from getting package boundaries right.
- **Context-level** (split or merge bounded contexts). Already operated on via [`CONTEXT-MAP.md`](../CONTEXT-MAP.md) (SIPI ↔ shttps); a third context is a separate decision tracked by [ADR-0001](./adr/0001-shttps-as-strangler-fig-target.md).

**Why this matters now.** ADR-0003's reframing: AI-throughput coding has shifted the human's role from writing code to defining and policing architecture. Bazel's `cc_library` + `--strict_deps` + `package_group` + `visibility` turns the package list into build-graph invariants — a forbidden `#include` fails analysis, not code review. Picking the right package list is therefore a leverage point: every package boundary we draw is a rule the build will then enforce automatically.

## Hypothesis

> Every term in [`UBIQUITOUS_LANGUAGE.md`](../UBIQUITOUS_LANGUAGE.md) should correspond to either:
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
- `purge() → int`
- `getNewCacheFileName() → string`
- `loop(worker, userdata, sort_method)` — admin/Lua iteration
- `stats() → Stats` — collapses the five getters
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
- `tcompare()` is public (pure internal utility).
- `check(block_file=true)` paired with `deblock(name)` — manual pairing, locking concern leaks (RAII `BlockedScope` fixes this).
- `getCacheDir()` exposes filesystem layout to Lua admin scripts (audit during `SipiLua` probe; either remove or fold into `stats()`).
- Five separate getters (`getCacheUsedBytes`, `getMaxCacheSize`, `getNfiles`, `getMaxNfiles`, `getCacheDir`) → one `Stats stats()` struct.
- `FileCacheRecord` (on-disk `char[256]` format) is in the public header but only `SipiCache` itself uses it.

Plus one larger finding (see #6/#7) — the cache held two responsibilities, and the second is being amputated entirely rather than extracted.

**6. Verdict.** `deep` post-refactor; today, `deep with shallow leaks and a co-located non-cache responsibility`.

**7. Action.**

a. **Promote to Bazel package** per ADR-0003. Co-locate `cache_test.cpp` from current `test/unit/cache/cache.cpp`.

b. **Tighten public interface** per #3: RAII `BlockedScope`, collapse getters to `Stats stats()`, privatize `FileCacheRecord` and `tcompare`. Mechanical; updates `SipiHttpServer.cpp` and `SipiLua.cpp` call sites.

c. **Amputate the image-shape responsibility** — delete `SizeRecord`, `sizetable`, and `getSize()` from `SipiCache`. The two structs (`SizeRecord` and `CacheRecord`) overlap because they hold the same data with different keys; `sizetable` was a parasitic side-effect index populated only by `add()`, never independently persisted, surviving eviction (`purge` and `remove` don't clean it up — bug, made moot by the deletion), and never populated for un-cached origpaths. It was barely a cache. Image shape lookup moves to format handlers — see [ADR-0004](./adr/0004-image-shape-ownership.md).

d. **Audit the Lua admin surface** during the `SipiLua` probe: `SipiLua.cpp` currently uses `getCacheDir`, `loop`, `remove`, `purge`, `add`. Decide whether the Lua-facing API is the same as the C++ public API or a thin facade. The `getCacheDir` exposure is the clearest case for separation.

**8. Glossary delta.** See [Glossary delta register](#glossary-delta-register) for the full set. Summary:
- Add **Image shape** — intrinsic shape of a source image; read by format handlers.
- Add **Operating mode** with **Server mode** + **CLI mode** sub-terms — the asymmetry between which format handler reads vs. writes is architecturally load-bearing.
- Add **Cache pin** (provisional) — confirm during `SipiHttpServer` probe.
- Sharpen **Essentials packet** — extend schema with image-shape fields per ADR-0004.

**9. Open questions for later probes.**
- `getCacheDir`'s removal or retention depends on the Lua admin surface (Probe `SipiLua`).
- The exact set of fields in `Stats` depends on what the `/metrics` endpoint and `SipiLua` consumers actually need (Probes `Operational surface` and `SipiLua`).
- `BlockedScope`'s API — pure RAII, or does it expose `blocked()` for the caller to detect "all-blocked, can't add"? — depends on Probe `SipiHttpServer` flow analysis.

## Probe 2 — `metadata/`

**1. Module name.** `src/metadata/` (existing). Future Bazel package `//src/metadata:metadata`. One package, not split per glossary umbrella — the language distinguishes Embedded metadata vs. Essentials packet, but in code they share consumers (every format handler + `SipiImage`) and a real layering boundary would not pay back the visibility/dep multiplication.

**2. Glossary terms.** Implements the **Preservation metadata** umbrella with both subordinate parts: **Embedded metadata** (EXIF, IPTC, XMP, ICC) via `SipiExif`/`SipiIptc`/`SipiXmp`/`SipiIcc`, and **Essentials packet** via `SipiEssentials`. Surfaces two glossary deltas — one sharpening, one new term — see register.

**3. Public interface (proposed `hdrs`).**

| Header | Class | Concern |
| --- | --- | --- |
| `metadata/essentials.h` | `SipiEssentials` | SIPI-owned preservation packet schema |
| `metadata/exif.h` | `SipiExif` | EXIF wrapper (exiv2) |
| `metadata/icc.h` | `SipiIcc` | ICC profile wrapper (lcms2) — `iccBytes()` chokepoint per ADR-0002 |
| `metadata/iptc.h` | `SipiIptc` | IPTC wrapper (exiv2) |
| `metadata/xmp.h` | `SipiXmp` | XMP wrapper (exiv2) |

The umbrella structure shows up as header organisation, not as separate Bazel packages.

**4. Private surface.** `metadata/internal/icc_normalization.h` (formerly `SipiIccDetail.h`) with `visibility = ["//src/metadata:__pkg__", "//test/unit/sipiicc:__pkg__"]`. Plus the `.cpp` files. Test seam pattern documented (see glossary delta).

**5. Depth signal.** Mixed at the per-class level:

| Class | hdr / cpp lines | Verdict | Reason |
| --- | --- | --- | --- |
| `SipiIcc` | 175 / 664 | **deep** | 1:4 ratio; `iccBytes()` chokepoint is the textbook Ousterhout hidden-complexity case (per ADR-0002) |
| `SipiXmp` | 93 / 166 | reasonable | depth in exiv2 RDF/XML wrapping |
| `SipiIptc` | 66 / 59 | reasonable | small but bounded |
| `SipiEssentials` | 209 / 213 | **shallow** | 1:1 ratio; inline `operator<<`, inline `operator std::string()`, 17 getter/setter pairs, pipe-delimited serialization in header |
| `SipiExif` | **321 / 150** | **shallow** | header > cpp; 22 inline `assign_val` template overloads + `typeid`-dispatched template `addKeyVal` pull the entire exiv2 type universe into every consumer's TU |

Plus one module-level leak: **third-party headers (`<exiv2/...>`, `<lcms2.h>`) appear in every public metadata header**. Every format handler, `SipiImage.hpp`, and `SipiSentry.h` get exiv2 + lcms2 in their compilation graph and see raw `Exiv2::`/`cms*` types in method signatures. Bazel `--strict_deps` will require either re-exporting these as transitive deps (anti-pattern) or hiding them via pImpl. One fix at the metadata boundary pays back at every consumer.

**6. Verdict.** Module-level `deep` (foundation layer of image processing — many consumers, real third-party-binding depth), with `shallow` leaks at the per-class level (Exif, Essentials) and a third-party-type leakage at the public-header boundary.

**7. Action.**

a. **Promote to Bazel package** `//src/metadata:metadata` per ADR-0003. Co-locate `*_test.cpp` from current `test/unit/sipiicc/`.

b. **Move `SipiIccDetail.h` → `metadata/internal/icc_normalization.h`** with `visibility` restricted to the package itself + the ICC unit test target. Use as the canonical **Test seam** reference in the glossary.

c. **Refactor `SipiExif` header**: move all inline `assign_val` and template `addKeyVal` definitions to `.cpp` with explicit instantiations for the types we use (`std::string`, `int`/`long`/`float`/`double` and their vectors, `Exiv2::Rational`/`URational`). Replace `typeid` dispatch with C++20 concepts or explicit specialization (matches the Rust-alignment direction discussed earlier). Hide `Exiv2::Rational` behind a SIPI-defined `Rational = std::pair<int32_t, int32_t>` if it doesn't break call sites materially.

d. **Refactor `SipiEssentials`**: tighten public interface to one `parse(bytes) → SipiEssentials` (free function or static factory) + one `serialize() → std::vector<unsigned char>` + a small struct of accessors. Drop the inline string-conversion operators. Adopt versioned binary wire format per ADR-0005. Add image-shape fields per ADR-0004 (which lands inside the new format, not the legacy one).

e. **Modernize C-pointer ownership across the module**: drop the dual-overload pattern (`unsigned char* xxxBytes(unsigned int& len)` + `std::vector<unsigned char> xxxBytes()`); keep only the vector form. Matches `cpp-style-guide.md` "no raw owning new/delete" and aligns with Rust's no-raw-pointer-ownership rule.

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
src/image/image.h              — class Image (geometry, pixel access, metadata accessors, RAII pixel buffer)
src/image/image_metadata.h     — ImageMetadata composite (5 standards bundle; possibly inlined into Image)

src/image_processing/crop.h
src/image_processing/scale.h            — one scale() taking ScalingQuality; replaces 3 named methods
src/image_processing/rotate.h
src/image_processing/color_convert.h    — convertYCC2RGB, convert_to_icc
src/image_processing/channel_ops.h      — remove_channel, remove_extra_samples
src/image_processing/bit_depth.h        — to_8bps, to_bitonal
src/image_processing/watermark.h        — DEFERRED to Probe 5 / Watermark glossary entry
src/image_processing/arithmetic.h       — operator-, operator+, operator==, compare (or test-only)
```

**4. Private surface.** `.cpp` files implementing the above. The IO map / format dispatch moves to `format_handlers/`. The 5 friend classes (4 format handlers + `SipiIcc`) all go away — replaced by public `pixels_writable()` API + metadata setters + 1-2 new accessors for `iccFormatter`. The raw `byte *pixels` becomes `std::vector<byte>` (or `unique_ptr<byte[]>` if benchmarking shows any regression — the audit PR decides).

**5. Depth signal.** Six distinct responsibility groups in one class — textbook god-object:

| Group | Surface |
| --- | --- |
| Image data container | `nx, ny, nc, bps, pixels (raw!), es, orientation, photo` |
| Metadata holder | 4 `shared_ptr<Sipi{Exif,Icc,Iptc,Xmp}>` + `SipiEssentials` value |
| Format I/O facade | static `io` map + `read()` / `readOriginal()` / `write()` / `getDim(filepath)` |
| Image processing | `crop`, `scale`/`scaleFast`/`scaleMedium`, `rotate`, `convertYCC2RGB`, `convertToIcc`, `removeChannel`, `removeExtraSamples`, `to8bps`, `toBitonal`, `add_watermark`, `set_topleft` (12 methods) |
| HTTP integration | `conobj` raw `shttps::Connection*` + `connection()` accessors |
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

**7. Action.** Per [ADR-0007](./adr/0007-sipiimage-decomposition.md). Eight staged sub-PRs (each independently reversible), tracked under a new Linear parent issue:

a. **Audit** every internal use of `pixels` (catalog read / write / pointer-pass / arithmetic / `new[]`/`delete[]` cases). Output is a doc + benchmark; no code change. Gates the rest.

b. **Replace `byte *pixels` with `std::vector<byte>`**. RAII; eliminates the explicit copy/move/dtor dance. Performance verified at parity in (a)'s benchmark.

c. **Remove `app14_transform` field**. JPEG handler inverts CMYK/YCCK at decode time; downstream sees standard CMYK.

d. **Move static `io` map** to `format_handlers/`. SipiImage stops being a registry of format handlers.

e. **Remove `conobj` field + `connection()` accessors**. Coupled with [DEV-6382](https://linear.app/dasch/issue/DEV-6382) (OutputSink with TeeSink for dual-write — see ADR-0006 / ADR-0007).

f. **Add public `pixels_writable()` API + metadata setters**; remove the 4 format-handler friend declarations.

g. **Extract image-processing methods** to `src/image_processing/` free functions over `const Image&`. ~12 method-to-free-function rewrites at every call site.

h. **Remove `SipiIcc` friend** (probably needs 1-2 new public Image accessors so `iccFormatter` works without internal access). Move arithmetic operators (`operator-`, `operator+`, `operator==`, `compare`) to free functions in `image_processing/arithmetic.{h,cpp}` or test-only.

Bazel package promotion (steps d, f, g specifically) gated on [DEV-6341](https://linear.app/dasch/issue/DEV-6341) reaching Y+6. Steps a, b, c can land in CMake era.

**8. Glossary delta.** Add **Image processing** (umbrella for the free-function module). Sharpen **Image** (the code-level class becomes a narrow value type post-refactor; domain term stays correct).

**9. Open questions for later probes.**

- **Lua-binding surface** ([SipiLua.cpp](../src/SipiLua.cpp), Probe 6): the Lua API likely exposes `image:crop(...)` style method-call syntax; if so, the Lua binding layer absorbs the C++-method-to-free-function translation transparently. May require keeping thin facade methods on `Image` purely for binding ergonomics. Probe 6 resolves.
- **Watermark module** (Probe 5): the watermark loading + applying logic might live entirely in route handlers (close to where `Permission` decides to apply it) rather than in `image_processing/`. Defer placement.
- **`ImageMetadata` composite** — own type or just members of `Image`? Decide during implementation; tightly bundled either way.
- **TeeSink composition** preserves the dual-write optimization (encoder writes to HTTP socket *and* cache file simultaneously). Documented in ADR-0006 + ADR-0007; implemented in DEV-6382.

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

1. Five `read()` overloads for default arguments → C++ default args.
2. `bool read()` returns → `std::expected<void, IoError>`.
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

d. **Implement ADR-0004** inside the existing `getDim()` virtual, then rename `getDim` → `read_shape` for self-documentation.

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

<!-- END PROBE ROWS -->

## Glossary delta register

Pending edits to [`UBIQUITOUS_LANGUAGE.md`](../UBIQUITOUS_LANGUAGE.md), accumulated from probe side-effects. Applied in a single editing pass at the end (or at natural batching points), so the glossary changes once with full context, not once per probe.

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
| **Tee sink** | add (provisional) | Probe 4 | Composition primitive in the `OutputSink` variant per ADR-0006: `TeeSink { std::vector<OutputSink> outputs; }` broadcasts each output chunk to multiple sub-sinks. Preserves SIPI's existing dual-write optimization (encoder writes simultaneously to HTTP socket + cache file). Generalises to write-through to S3 / other sinks. Provisional naming — confirm during DEV-6382 implementation. |

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

- [`UBIQUITOUS_LANGUAGE.md`](../UBIQUITOUS_LANGUAGE.md) — canonical SIPI glossary. The probe input.
- [`CONTEXT-MAP.md`](../CONTEXT-MAP.md) — SIPI ↔ shttps boundary. Bounds the scope of this exercise to SIPI-side modules; shttps is treated as one external module.
- [`CONTEXT.md`](../CONTEXT.md) — SIPI-side seam types. Names the four primary seam types (`Server`, `Connection`, `RequestHandler`, `LuaServer`).
- [ADR-0001 — shttps as strangler-fig target](./adr/0001-shttps-as-strangler-fig-target.md) — long-term direction; constrains how we reshape SIPI ↔ shttps seams.
- [ADR-0003 — Module-co-located source and tests](./adr/0003-module-co-located-source-and-tests.md) — defines the unit ("Bazel package"), the file layout, and the `--strict_deps` enforcement model. Read first.
- [`shttps/CONTEXT.md`](../shttps/CONTEXT.md) — out of scope for this exercise *except* to confirm that the SIPI-side modules respect the documented seam.

## Resume protocol

To continue this exercise in a later session:

1. Read this file end-to-end. Then re-read [`UBIQUITOUS_LANGUAGE.md`](../UBIQUITOUS_LANGUAGE.md) and [ADR-0003](./adr/0003-module-co-located-source-and-tests.md). That is the minimum cold-start context.
2. Find the next un-probed item in [Probe order](#probe-order). If a probe is partially complete, its row in the [Probe register](#probe-register) will say so.
3. Run the probe by reading the relevant headers and `.cpp` files, then fill in the 8 template fields. Append `## Probe N — <module name>` between the `<!-- BEGIN PROBE ROWS -->` / `<!-- END PROBE ROWS -->` markers.
4. If the probe surfaces a glossary gap, add a row to the [Glossary delta register](#glossary-delta-register) (do *not* edit `UBIQUITOUS_LANGUAGE.md` in the same pass — batch language edits).
5. If the probe surfaces a decision that meets the ADR bar (hard to reverse, surprising without context, real trade-off), draft the ADR under `docs/adr/`. The probe row should reference it.
6. Continue until [Probe order](#probe-order) is exhausted, then run the closing directory sweep.
7. Apply the [Glossary delta register](#glossary-delta-register) to `UBIQUITOUS_LANGUAGE.md` in one batched edit.
8. The output of the exercise is: the populated probe register, the patched glossary, and a short list of new ADRs. The Bazel migration plan referenced in ADR-0003 (Y+8a..Y+8e) consumes this output to choose its per-PR scope.

## Method invariants (don't drift on these across sessions)

- **Bazel package = the unit of module.** Don't slip into class-level analysis except as a follow-up *inside* a package.
- **Probe-and-extend, not sweep-first.** Don't start cataloguing concepts that haven't surfaced from a probe.
- **Append, don't rewrite.** Probe rows are history. If a verdict revises, add a `**Revised:**` line; don't edit the original.
- **Batch glossary edits.** Add rows to the delta register per probe; apply to `UBIQUITOUS_LANGUAGE.md` in one editing pass.
- **ADRs are sparing.** Only when all three of (hard to reverse, surprising without context, real trade-off) hold.
- **Bound by [ADR-0001](./adr/0001-shttps-as-strangler-fig-target.md).** Reshaping SIPI ↔ shttps seams is bounded by the strangler-fig direction. SIPI-side modules can absorb work re-homed *from* shttps (see `CONTEXT.md` "Re-homing schedule"), but new SIPI → shttps coupling is out of scope.
- **Module directory naming.** `snake_case` for compound words (`iiif_parser/`, `format_handlers/`, `route_handlers/`, `image_shape/`); single word otherwise (`cache/`, `metadata/`, `backpressure/`). Plural for collections of sibling types (`format_handlers/` — four format-handler classes; `route_handlers/` — multiple route-callback functions); singular for topics, mass nouns, or single concepts (`cache/`, `metadata/`, `backpressure/`, `iiif_parser/`). The `name` answers *what kind of thing this directory is about*, not *how many things are inside*. Aligns with Rust target, Google C++ Style Guide, Abseil / Chromium / BDE conventions. The `.cpp` / `.h` *file*-naming convention (PascalCase `SipiCache.cpp` vs snake_case `cache.cpp` vs BDE-style `sipi_cache.cpp`) is a separate, deferred decision — out of scope for the deep-modules exercise; will get its own ADR if and when it lands.
- **Disambiguate overloaded terms with umbrella + sub-types.** When a glossary term naturally covers multiple variants (different implementation language, different layer, different lifecycle), promote it to an umbrella in `UBIQUITOUS_LANGUAGE.md` and define each variant as a sub-type. Example: `Route handler` umbrella with `C++ route handler` + `Lua route handler` sub-types. This is preferred over silent overload (the source of `CONTEXT.md`'s mid-paragraph clarifications about `RequestHandler` vs `Route handler` and the two `file_handler`s).
- **Rust-aligned, transitional C++.** SIPI's C++ codebase is transitional ahead of the strangler-fig migration to Rust ([ADR-0001](./adr/0001-shttps-as-strangler-fig-target.md)). When choosing between a more-ergonomic C++ pattern that won't survive the Rust port and a less-ergonomic one that will, prefer the latter. Examples: `std::expected<T, E>` over `absl::StatusOr<T>` (the former maps directly to Rust's `Result<T, E>`; the latter implies adopting Abseil, a C++-only commitment); `std::variant<A, B>` over inheritance hierarchies (maps to Rust enums / sum types); RAII + `unique_ptr` over exception-based ownership (maps to Rust's move semantics). Cosmetic ergonomic gaps (e.g. `std::expected`'s lack of a `?` operator) are addressed by small SIPI-local helpers (e.g. a `SIPI_TRY` macro), not by adopting upstream libraries that don't outlive the C++ codebase.
- **Remote-access discipline.** Service masters are accessed remotely — NFS-mounted ZFS today, S3 in 3-6 months. Format-handler implementations and pre-decode logic minimize I/O operations: ideally one fixed-offset prefix read to fetch the Essentials packet (shape + file-structure offsets), then one targeted read for the data needed. Walking IFD chains, parsing box hierarchies one box at a time, or doing repeated small reads to discover offsets are anti-patterns — each is a network round trip. Local cache stays local (performance layer); only service-master-source reads pay remote-access cost. Per ADR-0004's expanded scope.
