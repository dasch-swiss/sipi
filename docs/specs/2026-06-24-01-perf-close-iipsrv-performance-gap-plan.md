---
title: "SIPI: Close the iipsrv performance gap (TIFF pyramid levels, in-memory decode cache, Highway SIMD)"
type: perf
date: 2026-06-24
author: "Ivan Subotic"
status: draft
linear:   # sibling of DEV-6613 under Linear project "Sipi Performance Improvements" — create on adoption
repositories:
  - sipi
adrs: [0002, 0003, 0006, 0014, 0015]
future_adrs: []  # candidates: in-memory decode cache; Highway portable-SIMD adoption
---

# SIPI: Close the iipsrv performance gap

## Implementation Progress / Resume State

> Not started. This is the initial draft. When work begins, record landed
> phases, decisions that override the body, and remaining task order here.

---

## 1. Problem

At the IIIF Annual Conference 2026, Ruven Pillay (IIPImage maintainer) published
an end-to-end benchmark of IIIF servers
(`sipi/docs/benchmarks/01_Ruven_Pillay_Evaluating_IIIF_Server_Performance.pdf`).
SIPI was the slowest server tested for TIFF and JPEG sources:

| Source format | iipsrv | Cantaloupe | digilib | **SIPI** |
|---|---|---|---|---|
| TIFF (uncompressed pyramid) | ~1.0 | ~2.0 | — | **~5.7** |
| TIFF (JPEG-in-TIFF) | ~1.3 | ~2.8 | — | **~9.0** |
| JPEG (monolithic) | 1.0 | ~4.0 | ~6.8 | **~13** |
| JPEG2000 / HTJ2K | ~1.0–1.5 | ~1.5 | — | **~1.5 (on par)** |

(Relative response time, 5000-request pan-and-zoom simulation, JPEG Q75 output,
bare-metal localhost. Lower is better.)

The pattern is the whole diagnosis: on JPEG2000 every server delegates to the
same Kakadu codec, so per-server overhead is masked and SIPI is competitive. On
TIFF and JPEG, where the server's own decode/cache pipeline dominates, SIPI is
5–13× slower. The cause is two architectural decisions, verified against source
in this analysis, plus a scalar processing stage:

1. **TIFF: the image pyramid is ignored.** `src/formats/SipiIOTiff.cpp:1209-1236`
   hardcodes the resolution `level = 0` and the `TIFFSetDirectory(tif, level)`
   selection is commented out. Every tile/region request decodes native tiles
   from the **full-resolution** IFD and downscales in software, so coarse-zoom
   requests decode `reduce²` more pixels than necessary. `read_resolutions()`
   (`SipiIOTiff.cpp:843-863`) even computes the pyramid metadata and then resets
   to directory 0.
2. **No in-memory decoded cache.** `SipiCache` (`include/SipiCache.h`) caches only
   the final **encoded output file**, keyed by canonical IIIF URL. There is no
   decoded-tile/region cache. Over a 5000-request pan-and-zoom session against a
   monolithic JPEG, SIPI re-decodes the entire JPEG every request while iipsrv
   decodes once and serves from a RAM tile cache. This is the dominant cause of
   the 13× JPEG gap.
3. **Scalar processing stage.** `scale`/`rotate`/ICC loops in
   `src/SipiImage.cpp` are scalar. The `process`-tier microbenchmark (below)
   shows the high-quality `scale()` dominating at 100+ ms, partly from a wasteful
   algorithm and partly from no SIMD.

### Production caveat (read before prioritising)

DaSCH production stores derivatives as **JPEG2000 (Kakadu)**, the one format
where SIPI is already on par. The benchmark's worst numbers are on formats that
are not DaSCH's production hot path. The cost of this gap is therefore:
(a) **reputational** — a public "SIPI is slowest" result at the field's main
conference; and (b) **real** for any SIPI deployment (ours or downstream) that
feeds TIFF or JPEG sources, and for the zoomed-out / thumbnail case on any
format. Phases 1 and 2 also help JP2 indirectly (the decode cache is
format-agnostic). This plan is worth doing, but it competes with roadmap items
on genuine production-latency impact, not on the headline multiplier.

### Process-tier baseline (measured this session)

`just bench process`, `-c opt`, source `Leaves8.tif` 2591×2572 RGB8, 10 reps.
The machine was loaded (load avg ~143) so these are **relative magnitudes, not
production latencies**; CV was <1.3% so the ordering is trustworthy.

| Operator | Median | Bound | Note |
|---|---|---|---|
| `scale()` /256 (high quality) | **114 ms** | compute | dominates; algorithmically wasteful |
| `scale()` /1024 | **150 ms** | compute | larger intermediate |
| `rotate(45)` general | 129 ms | compute | rare in IIIF traffic |
| `scaleMedium` /1024 | 13.4 ms | compute | bilinear |
| `rotate(90)` | 12.5 ms | memory | transpose |
| `crop` 1024² | 1.97 ms | memory | memcpy |
| `scaleFast` /1024 | 1.97 ms | memory | NN gather |
| `scaleFast` /256 | 0.13 ms | memory (≈DRAM bw) | — |

`scale()` is 134× `scaleMedium()` for identical input/output dims because it
bilinearly **upsamples to an integer multiple ≥ source** then box-averages
(`SipiImage.cpp:1014-1134`). That is an algorithmic defect, not a SIMD gap.

## 2. Goals and non-goals

**Goals**
- G1: TIFF tile/region requests decode from the correct pyramid level, so decode
  work is proportional to the requested output, not the source resolution.
- G2: A thread-safe, memory-bounded in-memory cache of **raw decoded regions** so
  repeated/overlapping pan-and-zoom requests do not re-decode.
- G3: A portable, vectorized resampling path (Highway) after an algorithm-first
  rewrite of `scale()`, with a defensible determinism story.
- G4: Every hot-path change is justified by a `just bench` + `bench-compare`
  before/after on the same `-c opt` binary, per the project invariant.

**Non-goals**
- iipsrv-style "pass-through" of pre-compressed JPEG/WebP-in-TIFF tile bytes
  (highest engineering surface, least relevant to DaSCH's JP2 stack). Recorded
  as a future option, out of scope here.
- GPU acceleration (CUDA/NvJPEG2000).
- Changing the production preservation format or the Kakadu JP2 path beyond what
  the shared decode cache touches.
- New output formats (AVIF/JXL).

## 3. Success measurement (no CI gate)

Per project rules, microbenchmarks are **never a CI gate** (cross-machine
comparison is meaningless; shared runners are too noisy) and we do not invent
latency numbers. Success is therefore measured as:

- **Per-phase:** each PR body carries a `just bench <tier> --benchmark_repetitions=20`
  + `just bench-compare before.json after.json` table, produced on a single named
  quiesced machine, with the U-test green AND median shift exceeding baseline CV
  (trust ≥5% green shifts; treat sub-3% as noise).
- **End-to-end (one-off, local):** a local reproduction of Pillay's pan-and-zoom
  harness (5000 overlapping-region requests) against TIFF and JPEG sources,
  reported as a topology comparison vs. iipsrv on the same machine, in the
  initiative's Linear project. Target stated as a **range**: close the TIFF gap
  to within ~1.5× of iipsrv and the JPEG gap to within ~2× after Phases 1–2.
  Conservative phrasing; no committed absolute figure.
- **Production:** new Prometheus counters (Section 7) confirm the fixes actually
  fire (pyramid level used; region-cache hit rate).

## 4. Guiding constraints (invariants)

- **No benchmark, no hot-path change** (`docs/src/development/benchmarking.md`).
  The `decode` and `process` tiers already cover these operators; extend them
  before editing.
- **ICC determinism gate** (ADR-0002): `Icc::iccBytes()` is the single chokepoint;
  approval goldens are byte-exact under `SOURCE_DATE_EPOCH=946684800`. No decode
  or resampling change may route around `iccBytes()`, and the SIMD float flag must
  be scoped so it cannot touch ICC math.
- **Module co-location** (ADR-0003): new code lives in `src/<mod>/{*.cpp,*.h,*_test.cpp}`
  with co-located `*_benchmark.cpp` (manual-tagged).
- **std::expected over bool+outparam** (ADR-0006) for new fallible APIs.
- **libc++ 22.1.7 limits**: no `std::simd` / `std::experimental::simd` (forces a
  library for SIMD → Highway); no `std::move_only_function` (use
  `std::unique_ptr<AbstractBase>` for move-only type-erasure).
- **Three platforms** must build and pass: darwin-aarch64, linux-x86_64,
  linux-aarch64. CI runs the matrix on all three.
- **BCR drop-in or vendor** (ADR-0015): `highway` is a true BCR drop-in (it is a
  SIMD utility, not a codec) → `bazel_dep`, no vendoring.
- **clang-format only changed regions / new files**; never whole-file reflow.

## 5. Phased plan

The ordering is fixed by leverage and risk: pyramid fix (largest structural gap,
lowest dependency), then decode cache (unlocks the JPEG case and amortises
everything), then SIMD (constant-factor, only worthwhile after decode scope is
correct). Cross-phase dependencies are in Section 6.

---

### Phase 1 — TIFF pyramid-level selection (`fix`)

**Objective.** Decode TIFF tiles/regions from the pyramid level matching the
requested reduction, mirroring how the JP2 path already delegates region+reduce
to Kakadu (`SipiIOJ2k.cpp:370-403`). Unlike JP2, libtiff does **not** map a
full-res ROI to a decode level for us, so SIPI must do the coordinate conversion
itself. "Mirror J2K" means match the *contract*, not copy the call.

**Root cause to fix first (do not skip).** There are two incompatible `reduce`
number systems:
- `read_resolutions()` stores `reduce` as a **ratio**: `image_width / level_width`
  → 1, 2, 4, 8 (`SipiIOTiff.cpp:855-857`).
- `SipiSize::get_size()` returns `reduce` as a **log2 exponent**: 0, 1, 2, 3
  (`src/iiifparser/SipiSize.cpp:145-388`).

The commented-out loop compared `r.reduce > reduce` (ratio vs. exponent) and then
`region->set_reduce(static_cast<float>(reduce))` fed the **exponent** into
`SipiRegion::crop_coords`, which divides coordinates by it (`rx / reduce`,
`SipiRegion.cpp:74-77`). At `reduce == 0` (full size, and some `pct:` cases) that
is a division by zero; at `reduce == 1` it divides full-res coords by 1 against a
half-res level. This is the actual mechanism behind the "region + pct:50"
regression. The fix must:
- convert exponent → divisor: `divisor = 1u << reduce_exp`;
- select the level by exact match `resolutions[i].reduce == divisor`, falling back
  to the **largest level with `reduce <= divisor`** when no exact match exists
  (non-dyadic / missing levels), then finish with a residual `scale()` in level
  coordinate space;
- pass the **level-relative** ROI to `read_tiled_data`/`read_standard_data`
  (these read from the current TIFF directory, so `TIFFSetDirectory(tif, dir)`
  must precede them and the ROI must be in that level's coordinate space).

**The `crop_coords` asymmetry (must fix).** `SipiRegion::crop_coords`
(`SipiRegion.cpp:70-146`) applies the `reduce` divisor only in the COORDS branch;
the SQUARE and PERCENTS branches ignore it. With level selection on, a `pct:`
region or `square` region combined with a reduced level will be inconsistent with
a `x,y,w,h` region. Make `reduce` handling uniform across all region branches.

**The PERCENTS `redonly` bug (must fix or characterise).** `SipiSize.cpp:309` has
an in-source TODO: "this calculation seems broken. This will prevent the smallest
TIFF resolution level from being selected." This is part of why selection
mis-picks. Phase 1 fixes or explicitly documents it; it is on the critical path.

**Cache-key consistency (must fix).** `get_canonical_url`
(`SipiHttpServer.cpp:657-796`) builds the on-disk cache key by calling
`crop_coords` on **full-res** dims with the default `reduce = 1.F`. The decode
path will call `crop_coords` on **level** dims with `set_reduce(divisor)`. If
these diverge, the canonical URL (cache key + IIIF `Link` header) describes a
different crop than what is decoded → silently wrong bytes cached. Acceptance:
the canonical region values must be identical with level-selection on vs. off.

**Output-cache invalidation (must ship in this PR).** Once Phase 1 lands, the
same IIIF URL produces different intermediate bytes, but the on-disk output cache
keys on canonical-URL + mtime and will serve pre-fix bytes. Fold a
**cache-version token** into the key (or wipe on upgrade). Do not ship Phase 1
without this.

**Tasks.**
- [ ] Extend the `decode` benchmark tier with a tiled-pyramid-TIFF fixture and
      assert tile-at-level decode cost; capture before/after.
- [ ] Add an exponent↔ratio reconciliation helper + unit test
      (`read_resolutions` ratio vs. `get_size` exponent).
- [ ] Re-enable level selection in `SipiIOTiff.cpp:1209-1236`: pick level by
      `reduce == 1<<exp` with `<=` fallback; `TIFFSetDirectory` to that IFD;
      set `img->nx/ny` to `resolutions[level]`; pass level-relative ROI.
- [ ] Make `reduce` handling uniform across COORDS/SQUARE/PERCENTS in
      `SipiRegion::crop_coords`.
- [ ] Fix / characterise the PERCENTS `redonly` calc (`SipiSize.cpp:309`).
- [ ] Guarantee `get_canonical_url` coordinates == decode-path coordinates.
- [ ] Add output-cache version token + invalidation.
- [ ] `is_tiled` must read `resolutions[level]`, not `[0]` (tiledness can vary
      per level).

**Edge cases / acceptance criteria.**
- Unit: `level_for(exp)` exact-match and non-dyadic `<=` fallback; no div-by-zero
  at `exp==0`.
- Approval/e2e: canonical `Link`-header region identical with flag on vs. off for
  `region+pct:50`, `square+,256`, `pct:25 region+!512,512`, each `+rotate90` and
  `+mirror` (rotation is post-decode at `SipiHttpServer.cpp:1899-1910`, so it is
  level-agnostic *if* crop+scale are correct first).
- Approval: between-levels request (`pct:37` on a 5-level pyramid) byte-compared
  to the level-0 path within tolerance (`redonly==false` residual scale).
- Fixture: pyramid where level 0 is tiled and a deeper level is stripped.
- Root-cause the "JPEG auto-conversion" regression (likely the exponent/divisor
  confusion plus YCbCr→RGB at a level with different tile boundaries) before
  guarding it.
- `bench-compare` table in the PR body.

---

### Phase 2 — In-memory decoded-region cache (`feat`)

**Objective.** Cache **raw decoded regions** (post-crop, post-level, pre-process)
so repeated/overlapping pan-and-zoom requests skip decode. New module
`src/cache/decoded_region_cache.{h,cpp,_test.cpp}` + co-located
`decoded_region_cache_benchmark.cpp` (ADR-0003).

**Cache only raw pixels (key decision).** Key on
`(file, mtime, level, roi_x, roi_y, roi_w, roi_h)` and store the RAW decoded
buffer **before** scale/rotate/quality/ICC/watermark. This sidesteps the
collision and authorization hazards of caching processed output:
- it avoids needing quality/format/colorspace in the key;
- scale/rotate/ICC/watermark run downstream every time (cheap relative to decode,
  and Phase 3 makes them cheaper);
- watermarked and permission-gated variants are never cached as servable bytes
  (watermark + permissions are request-time concerns; a RAM cache must not serve a
  region a requester is not authorized for).

**Concurrency model.** Mirror existing patterns:
- LRU + dual limit (bytes + entry count) like `SipiCache`'s eviction.
- **Single-flight** is mandatory. Without per-key in-flight dedup, N concurrent
  pan requests for the same just-evicted tile all decode simultaneously — the
  exact pathology we are fixing, amplified. Reuse the existing
  `blocked_files`/`deblock` blocking precedent in `SipiCache.h` (~`:100,163`).
- Move-only entries via `std::unique_ptr<AbstractBase>` (no
  `std::move_only_function` on libc++ 22).

**Memory-budget integration.** A retained region cache is long-lived memory the
existing `SipiMemoryBudget` (in-flight decode peak, atomic + RAII guard,
`include/SipiMemoryBudget.h`) does not model. Decide and document: cached-region
residency counts against a **separate** bounded budget, and the existing
`decode_memory_used_bytes` gauge is extended to include it so the OOM guard sees
total RAM. Eviction releases budget atomically.

**Invalidation.** Reuse `SipiCache`'s `tcompare` mtime comparator so the two
caches invalidate identically (avoids a stale decoded region feeding a fresh
output-cache miss). Document the 1s-mtime-granularity window as accepted risk,
consistent with the existing cache.

**Config / disable.** `region_cache_size_bytes = 0` disables (mirror
`max_cache_size==0` semantics). Touches the Lua config files
(`config/sipi.config.lua`, `.test-config.lua`, `.localdev-config.lua`) and
generated config.

**Tasks.**
- [ ] `decoded_region_cache` module: LRU, dual-limit, single-flight, atomic byte
      budget, RAII release.
- [ ] Wire into the decode path after level+crop, before processing.
- [ ] Prometheus metrics mirroring the `cache_*` family (`metrics.h:33-64`):
      `region_cache_hits_total`, `_misses_total`, `_evictions_total`, `_bytes`,
      `_singleflight_waits_total`.
- [ ] Extend `decode_memory_used_bytes` to include region-cache residency.
- [ ] Config key + Lua config plumbing.
- [ ] Co-located benchmark: hit/miss on 256²/512² regions.

**Acceptance criteria.**
- e2e: pan-and-zoom session shows `region_cache_hits_total` rising and decode
  count flat after warmup.
- e2e: concurrent identical-miss requests trigger exactly one decode
  (`region_cache_singleflight_waits_total > 0`).
- e2e: file overwrite invalidates both output and region caches consistently.
- Unit: eviction respects both byte and count limits; budget releases on evict.
- `region_cache_size_bytes=0` fully disables (no residency, no overhead).

---

### Phase 3 — Algorithm-first resampling + Highway SIMD (`perf`)

**Objective.** Make the resampling stage cheap and portable. Two sub-steps in
order, because the dominant cost is algorithmic, not SIMD:

**3a. Scalar algorithm rewrite (own commit).** Replace `scale()`'s
upsample-to-integer-multiple + box-average (`SipiImage.cpp:1014-1134`) with a
separable two-pass resampler: precompute per-output-coordinate source index +
fractional weight once per row/column, then a horizontal pass and a vertical
pass. This alone should remove most of the 114–150 ms (it eliminates the ~120×
extra `bilinn` calls). Cache only post-crop raw pixels feed this, so inputs are
already small after Phase 1.

> **This changes output bytes**, so existing byte-exact goldens for any
> `scale()`-touching output break on the **scalar** rewrite, before any SIMD.
> Regenerate those goldens with a documented visual-equivalence rationale in this
> commit. Two distinct gates follow: (i) scalar-rewrite → new goldens;
> (ii) SIMD-vs-scalar → ≤1 LSB tolerance against the new scalar reference.

**3b. Highway vectorization (own commit).** Vectorize the separable kernel with
`HWY_DYNAMIC_DISPATCH` so one source runs SSE4/AVX2/AVX-512 on x86 and NEON on
ARM. `bazel_dep(name = "highway", version = "1.3.0.bcr.1")`, target
`@highway//:hwy` (verified on BCR; the library libvips 8.15 and libjxl use).
- Vector code in `resample-inl.h` (`HWY_NAMESPACE`); dispatch glue + `HWY_EXPORT`
  in a `.cc` via `foreach_target.h`; the seam takes scalar pointers only.
- Process N output pixels per vector; interleaved RGB is fine for the separable
  passes (strided, not scatter-gather); FMA the weights; float intermediates;
  scalar remainder loop for edges.
- Cover both 8-bit and 16-bit (`bilinn` has a `word` overload at
  `SipiImage.cpp:871-906`; DaSCH has 16-bit TIFFs) or fall back to scalar for
  16-bit explicitly.
- Bound the x86 fan-out with `HWY_DISABLED_TARGETS` to {SSE4, AVX2, AVX3}.

**Determinism strategy (1-3-1).**
- *Problem:* the approval gate is byte-exact, but a SIMD float resampler is not
  byte-identical to the scalar `double` path, and can differ across SIMD targets
  (FMA on/off).
- *Outcome:* a vectorized resampler that is correct, fast, and has a stable,
  defensible test gate.
- *Three paths:* (1) keep byte-exact goldens on scalar/codec/ICC, gate the SIMD
  path with a separate ≤1 LSB tolerance test, compile the resampler TU with
  `-ffp-contract=off` for cross-target reproducibility; (2) re-baseline resampler
  goldens to SIMD output + `-ffp-contract=off`; (3) force SIMD to match scalar
  bit-for-bit (kills the perf win, still risks cross-target drift).
- *Recommendation:* **Path 1.** It is what libvips does (treats vector-vs-C as a
  tolerance relationship) and keeps the ICC gate untouched (`-ffp-contract=off`
  is scoped to the resampler TU only, never `SipiImage.cpp` wholesale, so
  `Icc::iccBytes()` math is unaffected).

**Tasks.**
- [ ] Ensure `process` benchmark covers the dominant case (high-quality downscale
      at 2591×2572) and the between-levels residual scale; capture before.
- [ ] 3a: separable scalar rewrite of `scale()`; regenerate affected goldens with
      rationale.
- [ ] 3b: add `highway` BCR dep; `resample-inl.h` + dispatch `.cc`;
      `-ffp-contract=off` scoped to the resampler TU in BUILD.bazel.
- [ ] ≤1 LSB tolerance unit test vs. same-run scalar reference, 8- and 16-bit,
      sample points covering integer-aligned and sub-`Threshold` coordinates,
      runs on all three arches (it computes the reference in-run, no checked-in
      golden).
- [ ] `bench-compare` tables for 3a and 3b separately.

**Acceptance criteria.**
- `bench-compare` green (≥5% median, U-test p<0.05) for 3a and again for 3b.
- Tolerance test passes on darwin-aarch64, linux-x86_64, linux-aarch64.
- ICC approval goldens unchanged (proves the float flag is scoped).
- 16-bit path covered or explicit scalar fallback documented.

## 6. Ordering and cross-phase dependencies

- **1 → 2 → 3** as numbered. Rationale in Section 5 intro.
- **Phase 1 must ship output-cache invalidation** (Section 5, Phase 1) or
  resequence a cache-versioning mechanism to land first. Non-negotiable: without
  it, the on-disk cache serves pre-fix bytes.
- **Phase 3 goldens regenerate after Phase 1**, because Phase 1 changes the
  residual-scale inputs; regenerating 3a goldens before Phase 1 would bake in
  level-0 assumptions.
- Phase 2's cache sits **after** Phase 1's level+crop, so it caches correct
  level-relative pixels from day one.

## 7. Observability

- `tiff_pyramid_level_used_total{level}` — confirms the Phase 1 fix actually
  selects levels in production (and, since prod is JP2, how much TIFF pyramid
  usage even occurs, which bears on how hard to push this).
- `region_cache_*` family (Phase 2), mirroring `metrics.h:33-64`.
- `decode_memory_used_bytes` extended to include region-cache residency.

## 8. Risk matrix

| Risk | Phase | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| Reduce exponent/ratio confusion reintroduces div-by-zero / wrong crop | 1 | High if untyped | High | Explicit `1<<exp` conversion + unit test; consider a typed wrapper |
| Cache key ≠ decode coords → wrong bytes cached | 1/2 | Medium | High | Canonical-vs-decode coord equality test |
| Stale on-disk cache after Phase 1 | 1 | Certain | High | Cache-version token in this PR |
| Cache stampede on concurrent miss | 2 | Medium | High | Single-flight via `blocked_files`/`deblock` |
| Region cache invisible to OOM guard | 2 | Medium | High | Separate bounded budget folded into `decode_memory_used_bytes` |
| Scalar `scale()` rewrite breaks goldens unexpectedly | 3a | Certain | Medium | Regenerate with documented rationale; isolate commit |
| SIMD non-determinism across targets | 3b | High | Medium | `-ffp-contract=off` + ≤1 LSB tolerance test on all arches |
| Highway build fan-out inflates one TU's build time | 3b | Low | Low | `HWY_DISABLED_TARGETS` to {SSE4,AVX2,AVX3} |
| Effort spent on non-production-hot-path (JP2 is prod) | all | — | Medium | Phase ordering front-loads the format-agnostic cache; success measured locally, not over-promised |

## 9. Alternatives considered

- **iipsrv pass-through of pre-compressed tile bytes.** Highest ceiling, largest
  surface, mostly benefits JPEG/WebP-in-TIFF which is not DaSCH's stack. Deferred.
- **GPU (NvJPEG2000 / CUDA).** Hardware-dependent, proprietary, out of scope.
- **Intel IPP (what iipsrv used for bilinear).** Intel-only, proprietary; fails
  SIPI's ARM targets. Highway is the portable answer.
- **stdlib SIMD (`std::simd` / `std::experimental::simd`).** Not shipped in
  libc++ 22; no runtime dispatch even when it lands. Rejected.
- **Vendor Highway as a native `cc_library`.** Unnecessary; it is a true BCR
  drop-in (ADR-0015). Keep the vendor path in reserve only for a pin BCR lacks.

## 10. Open questions

- Should Phase 1 be guarded behind a config flag (`tiff_pyramid_levels=on|off`)
  defaulted off until goldens are proven, then flipped? (Recommended: yes.)
- Region cache: separate budget ceiling value, and does it share an eviction
  policy with the output cache or stay independent? (Recommended: independent,
  separate budget.)
- Do we run the local end-to-end Pillay-style harness as a checked-in tool in
  SIPI, or as a one-off? (Affects whether the 5000-request comparison is
  reproducible by others.)
- Create the Linear issue as a sibling of DEV-6613 under "Sipi Performance
  Improvements"?
