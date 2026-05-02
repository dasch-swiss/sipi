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

<!-- END PROBE ROWS -->

## Glossary delta register

Pending edits to [`UBIQUITOUS_LANGUAGE.md`](../UBIQUITOUS_LANGUAGE.md), accumulated from probe side-effects. Applied in a single editing pass at the end (or at natural batching points), so the glossary changes once with full context, not once per probe.

| Term | Action | Source probe | Note |
| --- | --- | --- | --- |
| **Image shape** | add | Probe 1 | Intrinsic shape of a source image: `(img_w, img_h, tile_w, tile_h, clevels, numpages, nc, bps)`. Read by a format handler from the master file. Replaces the parasitic `SipiCache::SizeRecord`. Per ADR-0004. |
| **Operating mode** | add (umbrella) | Probe 1 | Two sub-terms — `Server mode` and `CLI mode`. The asymmetry between which format handler dominates read vs. write is architecturally load-bearing for the master-format fast path. |
| **Server mode** | add | Probe 1 | Long-running HTTP server. Reads master files in master format from `image root`; writes IIIF representations to the cache. The hot path for master-format shape reads. |
| **CLI mode** | add | Probe 1 | One-shot invocation. Reads arbitrary source format; writes a master file in master format with full Essentials packet. The path that populates Essentials-packet shape fields. |
| **Master file** | add | Probe 1 | The on-disk source artefact under `image root` that an `identifier` resolves to. The authoritative copy SIPI's IIIF pipeline reads from. Codebase variables `infile` / `origpath`. |
| **Master format** | add | Probe 1 | The format of master files. SIPI prefers this format for its own preservation guarantees. Currently JP2; pyramidal TIFF planned to supersede. CLI mode converts ingested files to the master format; server mode reads master files in this format. |
| **Pyramidal TIFF** | add | Probe 1 | Multi-resolution TIFF variant storing the same image at multiple decode levels in a single file. Supports efficient decode-level selection without full-resolution decoding. Currently a supported master format alongside JP2; planned to supersede JP2 as the sole master format. |
| **Cache pin** | add (provisional) | Probe 1 | Per-file in-use refcount that prevents eviction while a representation is being served. Currently `SipiCache::blocked_files` + `check(block_file=true)` + `deblock`; refactor to RAII `BlockedScope`. Confirm name during `SipiHttpServer` probe. |
| **Essentials packet** | sharpen | Probe 1 | Existing definition lists original filename, mimetype, hash type, pixel checksum, optional ICC. Extend schema with **image-shape fields** so server-mode shape lookup can read them at known offset rather than parsing the codestream / TIFF tags. Per ADR-0004. The schema is format-agnostic; the embedding mechanism (JP2 box vs TIFF tag) is handler-specific. |
| **Route handler** | sharpen → umbrella | naming discussion (Probe 1 follow-up) | Promote to umbrella term: URL-pattern-bound request logic. Two sub-types depending on implementation language (`C++ route handler`, `Lua route handler`). Resolves the term overload between the existing Lua-only definition and the planned `route_handlers/` C++ directory. |
| **C++ route handler** | add | naming discussion (Probe 1 follow-up) | A `shttps::RequestHandler` callback registered at server startup. Examples: `iiif_handler`, `file_handler`. Compiled in. Lives in `src/route_handlers/` post-Probe 5 refactor. Note: `shttps::RequestHandler` remains the framework type; *C++ route handler* is the SIPI-side domain term for instances of it. |
| **Lua route handler** | add | naming discussion (Probe 1 follow-up) | A Lua script bound to a URL pattern, loaded dynamically. Examples: upload, admin endpoints in `scripts/`. (Existing "Route handler" glossary entry redirects here.) |
| **Format handler** | sharpen | Probe 1 follow-up | Existing definition is correct; note the directory rename `formats/` → `format_handlers/` for self-documentation, matching the glossary term directly. |
| **Essentials packet** | sharpen further | Probe 2 | Current wire format is pipe-delimited text without escaping or schema versioning — brittle (any `\|` in `origname` corrupts parse) and not forward-evolvable. Per ADR-0005 the wire format migrates to versioned CBOR; the in-memory schema is the same. ADR-0004's image-shape additions land in the new wire format. |
| **Test seam** | add | Probe 2 | A header deliberately kept in a module's `internal/` subdirectory with `visibility` restricted to that module + that module's tests. Used to expose pure helpers for explicit testing without broadening production coupling. Canonical example: `metadata/internal/icc_normalization.h` (formerly `SipiIccDetail.h`). The pattern replaces comment-as-policy ("No production code outside X should include this header") with a build-graph invariant. |

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
