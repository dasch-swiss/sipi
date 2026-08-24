---
title: "Fix info.json: scaleFactors/sizes misalignment (ordinals instead of 2^i)"
type: fix
date: 2026-08-17
author: "Ivan Subotic"
status: reviewed
repository: dasch-swiss/sipi
linear: DEV-6981
project: "Moving towards pyramidal TIFF"
project_url: https://linear.app/dasch/project/moving-towards-pyramidal-tiff-402cf7f500bf
---

# Fix info.json: scaleFactors/sizes misalignment (ordinals instead of 2^i)

## Overview

SIPI's IIIF `info.json` advertises a `scaleFactors` array of consecutive ordinals
(`[1,2,3,4]`) instead of powers of two (`[1,2,4,8]`), and a `sizes` array that
describes a *different* pyramid than `scaleFactors`. Both arrays are built in
`src/server-rs/src/info.rs` by different arithmetic. The result: OpenLayers blacks
out at deep zoom (root cause of DEV-6933), and OpenSeadragon silently discards our
`sizes` and rebuilds its own pyramid.

This is a **descriptor-only** fix — old assets are not re-encoded, the server
already serves every level correctly (all HTTP 200, `SipiSize::REDUCE` resamples on
the fly), so correcting the advertised arrays is the whole remedy for the existing
corpus. The descriptor change is confined to `src/server-rs/src/info.rs` (the
generator and its unit tests) plus the e2e golden snapshot; the two fixtures that
currently *pin the defect* are the inline lena512 assertion and that snapshot. The
work also removes the `clevels` field from the FFI seam (`ffi.rs`, `sipi_ffi.h/.cpp`),
which becomes dead once the descriptor stops consulting it.

**Scope decisions (agreed at planning):**
- **Descriptor fix only.** The `0801`-is-it-tiled filesystem investigation and the
  optional C++ write-path change (explicit `512×512` stored tiles) are carved out as
  separate follow-up issues — different blast radius (FFI/C++ hot paths, infra
  access), different risk profile.
- **Untiled path** (`tile_width == 0`, no `tiles` block): derive the `sizes` ladder
  from a default reference tile size of **512**, so there is one pyramid-generation
  rule and `sizes` stays a useful "preferred sizes" downscale ladder.

## Problem Statement / Motivation

`info.rs:96` emits the JP2/pyramidal resolution-level **count** as a list of
ordinals:

```rust
let cnt = if dims.clevels > 0 { dims.clevels } else { 5 };
let scale_factors: Vec<u32> = (1..cnt).collect();   // 1,2,3,...,cnt-1  ← WRONG
```

`clevels` is a *count*, not a set of scale factors. Emitting it directly yields
`1,2,3,4…`, which coincides with powers of two only when `n <= 2` — which is why the
defect went unnoticed.

Twelve lines above, `size_pyramid()` (`info.rs:57-73`) builds a *correct* ÷2 ladder
(`sf = 1u64 << level`) but:
- starts at `i = 1`, so it **omits the native size** (`2^0`);
- `break`s once both dimensions are `< 128`, so its depth is unrelated to the tile
  grid and does not line up with `scaleFactors`;
- is emitted **descending** while `scaleFactors` is ascending.

**Impact (from the DEV-6976 research report):**
- **OpenLayers** derives `maxZoom = round(log2(max(scaleFactors)))` but keeps all
  resolutions, so any level beyond `maxZoom` gets no tile URL → black screen. Sample
  `0803` (`[1,2,3,4]`) loses its native level; sample `0801` (`[1,2,3,4,5,6,7]`)
  loses its **three most detailed levels**. The deeper the pyramid, the more is
  lost — images with `n >= 5` lose most of their zoom levels today.
- **OpenSeadragon** masks it: it uses `sizes` as its level list only when
  `sizes.length ∈ {maxLevel, maxLevel+1}` (verified against OSD v5.0.1
  `iiiftilesource.js`; `maxLevel = round(log2(max(scaleFactors)))`); ours mismatches,
  so OSD discards our pyramid and rebuilds a power-of-two one. **OSD rendering
  correctly is not evidence the descriptor is sound.** Use **OpenLayers as the
  conformance canary.**

The spec (Image API 3.0 §5.4) permits *any* positive integer scale factor —
`[1,2,3,4]` is valid IIIF — but both viewers hard-code `log2`/`pow(2)` pyramid math,
so power-of-two is a **binding de-facto convention**. Ordering is spec-silent; we fix
our own canonical order because we don't control every client.

**On coherence: this is an interop defect, not a spec violation.** The spec does
*not* require `sizes` and `scaleFactors` to describe the same pyramid — its own
complete example (§5.9) deliberately ships two different ladders. We couple them
anyway because it is what makes OpenSeadragon *use* our `sizes` at all: OSD keeps
`sizes` only when `sizes.length ∈ {maxLevel, maxLevel+1}` (verified against OSD
v5.0.1 `iiiftilesource.js`), and `maxLevel = round(log2(max(scaleFactors)))`.
Including the native size makes our count `n+1 = maxLevel+1`, which satisfies that
check. Independently, §5.3 requires the server to actually serve any `w,h` listed in
`sizes` — the report verified all advertised levels return HTTP 200, so coupling adds
no capability the server lacks.

## Proposed Solution

Replace the two divergent generators with a **single pyramid derived from the tile
grid**, feeding both `sizes` and `scaleFactors`:

1. **Scale factors** = contiguous powers of two `[2^0 … 2^n]` where
   `n = ceil(log2(max(w, h) / tile))`, so the deepest factor puts the whole image
   inside a single tile. Robust form for non-square tiles:
   `n = max(ceil(log2(w / tile_w)), ceil(log2(h / tile_h)))`.
2. **Sizes** = the full-image dimensions at each of those factors,
   `(ceil(w / sf), ceil(h / sf))`, **including the native size** — so
   `sizes.length == scaleFactors.length` and the two describe the same pyramid.
3. **Both arrays ascending.** `scaleFactors` ascending is `[1,2,4,8]`; `sizes`
   ascending (smallest → native) is the same pyramid iterated by descending scale
   factor. This matches the spec's own examples.
4. **`clevels` is no longer consulted** for the descriptor — it was the source of the
   bug. Depth now comes from the tile grid. (Advertising more levels than are
   physically stored is safe: the server resamples on request, verified HTTP-200 in
   the report.)
5. **Untiled images** (`tile_width == 0`): still emit `sizes`, derived from a
   reference tile size of `512`; `tiles` stays omitted.

### Reference implementation (illustrative)

```rust
/// Reference tile size for the sizes ladder when the image is untiled.
const DEFAULT_TILE_SIZE: u32 = 512;

/// Contiguous powers of two `[2^0 .. 2^n]` (ascending), where `n` is the smallest
/// level count that puts the whole image inside one tile on both axes. The shift is
/// done in `u64` (matching the existing `SipiSize::REDUCE` code) so a deep pyramid
/// can never overflow the shift.
fn pyramid_scale_factors(width: u32, height: u32, tile_w: u32, tile_h: u32) -> Vec<u32> {
    let levels = |dim: u32, tile: u32| -> u32 {
        let mut n = 0u32;
        while (u64::from(tile) << n) < u64::from(dim) {
            n += 1;
        }
        n
    };
    let n = levels(width, tile_w).max(levels(height, tile_h));
    (0..=n).map(|i| (1u64 << i) as u32).collect()
}

/// `sizes` for a given ascending scale-factor list, ascending (smallest → native),
/// native included. Kept as a pure transform of the factor list so `sizes` and
/// `scaleFactors` cannot describe different pyramids.
fn sizes_for(width: u32, height: u32, scale_factors: &[u32]) -> Vec<Value> {
    scale_factors
        .iter()
        .rev() // largest scale factor first → smallest size first (ascending sizes)
        .map(|&sf| json!({ "width": width.div_ceil(sf), "height": height.div_ceil(sf) }))
        .collect()
}
```

Caller in `image_info_json` — compute the factor list **once** and derive both
arrays from it, so there is a single source of truth:

```rust
let (tw, th) = if dims.tile_width > 0 && dims.tile_height > 0 {
    (dims.tile_width, dims.tile_height)
} else {
    (DEFAULT_TILE_SIZE, DEFAULT_TILE_SIZE)
};
let scale_factors = pyramid_scale_factors(dims.width, dims.height, tw, th);
root.insert(
    "sizes".into(),
    json!(sizes_for(dims.width, dims.height, &scale_factors)),
);
if dims.tile_width > 0 && dims.tile_height > 0 {
    root.insert(
        "tiles".into(),
        json!([{ "width": dims.tile_width, "height": dims.tile_height, "scaleFactors": scale_factors }]),
    );
}
```

Worked example — `0803`, 3505×5156, stored tile 1024: `n = 3` → `scaleFactors
[1,2,4,8]`, `sizes [{439,645},{877,1289},{1753,2578},{3505,5156}]`. Deepest factor
`8` puts 439×645 inside one 1024 tile. Matches the corrected descriptor in the issue
exactly.

## Technical Considerations

- **Single source of truth.** `sizes` and `scaleFactors` must be generated from one
  function so they cannot drift again. The plan couples them through
  `pyramid_scale_factors`.
- **Advertised tile size is unchanged** (finding 1 of the report): `tiles[].width` /
  `height` keep passing `dims.tile_width` / `tile_height` through verbatim from the
  FFI read of the actual file (`SipiIOJ2k.cpp` `Stiles`, `SipiIOTiff.cpp`
  `TIFFTAG_TILEWIDTH`). No derive rule, no cap, no "smallest multiple >= 512". Do not
  regress this.
- **Non-square tiles.** SIPI's writers produce square tiles, but the robust
  `max(levels_w, levels_h)` form guarantees single-tile coverage on both axes if they
  ever differ. Cheap insurance, no downside.
- **`clevels` is removed from the FFI struct** (`SipiImageDims`). Once *info.rs*
  stops consulting it, nothing on the Rust side reads the marshalled field: the
  C++ decode/cache paths (`serve_image.cpp` `compute_decode_dims` and the cache
  key) use the engine's own codec-read `info.clevels`, not the value crossed over
  the seam. Carrying a dead FFI field invites the next reader to assume it is
  load-bearing, so the field is dropped from both `SipiImageDims` definitions, its
  population site, and the ABI assertions. It is the last field (offset 20), so no
  other offset shifts — the struct simply shrinks from 24 to 20 bytes.
- **Snapshot churn is expected and correct.** The lena512 golden and the inline
  assertion currently encode the defect. They must be updated to the corrected
  values, not "fixed" back.
- **Tile-object uniqueness (§5.4).** We emit a single `tiles` object, so both the
  SHOULD (each scale factor appears once) and the MUST (unique `width`/`height` per
  tile object) are trivially satisfied. No action needed, but the conformance test
  can assert it cheaply.
- **OpenLayers-behaviour provenance.** The claim that OL derives
  `maxZoom = round(log2(max(scaleFactors)))` and orphans levels beyond it comes from
  the DEV-6976 report, which read `ol/source/IIIF.js` + `ol/format/IIIFInfo.js` (OL
  `main`) directly and quotes the code. An independent best-practices pass could not
  re-verify OL source this session (GitHub/CDN rate-limiting) and flagged it as
  possibly conflated with OSD. The OSD path is fully source-verified; treat the OL
  specifics as high-confidence-from-the-report and **spot-check against our pinned OL
  version** before relying on the exact orphaning count. The fix (power-of-two,
  coherent, ascending) is correct under either viewer's behaviour regardless.
- **Defence-in-depth note (out of scope here):** keep the OpenLayers client-side
  sanitiser from DEV-6933 (option 2) for third-party IIIF sources; our own images no
  longer need it after this lands.

## Implementation Approach

- [x] Add `pyramid_scale_factors(width, height, tile_w, tile_h)` to `info.rs` —
      contiguous powers of two `[2^0..2^n]`, `n = max(levels_w, levels_h)`, ascending,
      shifting in `u64` (as `info.rs:60` already does)
- [x] Replace `size_pyramid()` (`info.rs:57-73`) with `sizes_for(width, height,
      &scale_factors)` — a pure transform of the shared factor list that includes the
      native size and emits **ascending** (smallest → native)
- [x] Update the `size_pyramid` rustdoc (`info.rs:53-56`) — it still describes the old
      `1..clevels` / `<128` / clevels-fallback-5 algorithm; keep the `SipiSize::REDUCE`
      reference but describe the tile-grid derivation
- [x] In `image_info_json`, compute `scale_factors` **once** via
      `pyramid_scale_factors` and derive both `sizes` (via `sizes_for`) and the `tiles`
      block from it; drop the `clevels` read and the ordinal
      `(1..cnt).collect()` at `info.rs:96` and the old `size_pyramid` call at `:90-92`
- [x] Wire the untiled path: when `tile_width == 0`, derive `sizes` from
      `DEFAULT_TILE_SIZE = 512`; keep `tiles` omitted
- [x] Update the inline unit test `info_json_matches_lena512_golden` (`info.rs:327`)
      — `scaleFactors` becomes `[1]`, `sizes` becomes `[{512,512}]`, and fix the
      stale comment describing the old `[1..7]` behaviour
- [x] Update the `untiled_image_omits_tiles` unit test (`info.rs:334`) for the new
      512-reference `sizes` ladder (tiles still absent)
- [x] Regenerate the e2e golden snapshot
      `test/e2e/tests/snapshots/iiif_compliance__info-json-lena512.snap` — it is a
      checked-in `insta` file run under Bazel with `INSTA_WORKSPACE_ROOT="."`
      (`sipi_e2e_test.bzl:143`), **not** `cargo insta`. Update it by hand-editing the
      `.snap` (the expected output is deterministic and known) or by running the crate
      with `INSTA_UPDATE=always` outside the Bazel sandbox, then verify via
      `bazel test //test/e2e:iiif_compliance`; it should show `scaleFactors [1]` /
      `sizes [{512,512}]`
- [x] Add a descriptor conformance unit test in `info.rs` covering acceptance
      criteria 1–5 plus §5.4 tile uniqueness, across several dimension/tile
      combinations (see Acceptance Criteria)
- [x] Remove the now-dead `clevels` field from the FFI seam — it is no longer read
      on the Rust side once *info.rs* stops consulting it (the C++ decode/cache paths
      use the engine's own codec-read `info.clevels`, not the marshalled value).
      Dropped from `SipiImageDims` in `src/server-rs/src/ffi.rs` and
      `src/ffi/sipi_ffi.h`, its population at `src/ffi/sipi_ffi.cpp` (`out->clevels =`),
      the ABI `static_assert(offsetof(... clevels) == 20)` (`sipi_ffi.h`) and the
      matching offset test in `ffi.rs`, plus the `clevels` param of the `dims()` unit
      test helper in `info.rs`. It was the last field, so no other offset shifts (the
      struct drops from 24 to 20 bytes). Lands as its own commit in sipi PR #785,
      separate from the descriptor fix because it is an ABI-narrowing beyond the
      descriptor-only change.
- [x] Run `just bazel-test-unit`, `just bazel-test-e2e`, `just bazel-rustfmt-check`,
      `just bazel-clippy-check` — all green before commit (rustfmt + clippy are CI
      gates not covered by the test recipes). All four run green locally: unit 26/26,
      e2e 28/28, rustfmt and clippy clean. The C++ `static_assert(sizeof == 20)` and
      the Rust `offset_of!` layout test both compile/pass, pinning the narrowed ABI on
      both sides of the seam.

## Acceptance Criteria

- [ ] `scaleFactors` are contiguous powers of two starting at 1: `2^i` for
      `i = 0..n`, where `n = ceil(log2(max(w,h) / tile))` for square tiles, generalized
      to `n = max(ceil(log2(w / tile_w)), ceil(log2(h / tile_h)))` (see criterion 3)
- [ ] `sizes` and `scaleFactors` describe the same pyramid:
      `sizes.length == scaleFactors.length` and each `sizes` entry equals
      `(ceil(w / sf), ceil(h / sf))` for exactly one scale factor
- [ ] The deepest scale factor puts the whole image within a single tile on both axes
- [ ] `tiles[].width` / `height` continue to be the stored tile size, passed through
      unchanged (no regression of finding 1)
- [ ] Both `sizes` and `scaleFactors` are emitted **ascending**
- [ ] lena512 (512×512, tile 512) advertises `scaleFactors [1]` and
      `sizes [{512,512}]` (single tile, no pyramid needed)
- [ ] Untiled image (`tile_width == 0`) omits `tiles` and emits a 512-derived `sizes`
      ladder
- [ ] Conformance test asserts criteria 1–5 for at least: a deep pyramid (e.g.
      3505×5156 / 1024 → `[1,2,4,8]`), a single-tile image (512×512 / 512 → `[1]`),
      and a non-square case
- [ ] Conformance test asserts §5.4 tile uniqueness (each scale factor once; unique
      `width`/`height` per tile object) — trivially held by our single tile object
- [ ] Full unit + e2e suites green; rustfmt and clippy gates green

## Dependencies & Risks

- **Verification against OpenLayers requires a running SIPI + a real asset.** OSD is
  not sufficient as a canary (it masks the bug). This can be exercised locally
  (`just run` + the OL viewer) against a tiled test asset; a stage corpus sweep is a
  post-merge validation, tracked below, not a merge blocker.
- **Corpus sweep needs stage/filesystem access (operator territory).** Grouping stage
  assets by `len(scaleFactors)` to size the blast radius is a read-only validation the
  maintainer runs after the fix; it is not part of the code change and does not gate
  the PR.
- **Snapshot review discipline.** The two updated fixtures encode the defect; a
  reviewer must confirm the new values are the corrected pyramid, not a re-blessed
  bug (⚠️ called out in the issue).

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Snapshot "fixed" back to the defective `[1..n]` values | M | H | Conformance test pins `2^i`; PR description flags the two fixtures as defect-encoding; reviewer checklist item |
| `sizes`/`scaleFactors` drift apart again in future edits | L | M | Both generated from one `pyramid_scale_factors` function; conformance test asserts `sizes.length == scaleFactors.length` |
| Untiled `sizes` ladder differs from expectations | L | L | Decision recorded (512 reference); unit test pins the untiled shape |
| Advertising more levels than physically stored fails to serve | L | H | Report verified all levels return HTTP 200 (server resamples via `SipiSize::REDUCE`); re-check one deep asset against OL locally |
| Non-square tile edge case | L | M | `max(levels_w, levels_h)` guarantees single-tile coverage on both axes |

## Success Metrics

- OpenLayers reaches native zoom on a previously-blacking-out asset (e.g. an `0803`-
  class image) with no orphaned levels.
- Every advertised `sizes` entry corresponds 1:1 to an advertised `scaleFactor`
  (OSD stops discarding our pyramid).
- Post-merge stage corpus sweep: no asset advertises non-power-of-two `scaleFactors`
  or a `sizes`/`scaleFactors` length mismatch.

## References

- Linear issue: DEV-6981 (fix) · DEV-6976 (research, Done) · DEV-6933 (OL black-out)
- Generator to fix: `src/server-rs/src/info.rs:57-73` (`size_pyramid`), `:94-101`
  (`tiles` block), `:96` (ordinal bug)
- FFI struct (unchanged): `src/server-rs/src/ffi.rs:325-331` (`SipiImageDims`)
- Tests to update: `src/server-rs/src/info.rs:327` (inline golden), `:334`
  (untiled), `test/e2e/tests/snapshots/iiif_compliance__info-json-lena512.snap`
- E2e snapshot test: `test/e2e/tests/iiif_compliance.rs:329` (`info_json_golden_snapshot`)
- IIIF Image API 3.0 — [§5.3 Sizes](https://iiif.io/api/image/3.0/#53-sizes),
  [§5.4 Tiles](https://iiif.io/api/image/3.0/#54-tiles),
  [§5.9 complete example](https://iiif.io/api/image/3.0/#59-complete-response) (ships
  two intentionally-different pyramids — coherence is convention, not a MUST),
  [Implementation Notes §3](https://iiif.io/api/image/3.0/implementation/)
- [IIIF/api#447](https://github.com/IIIF/api/issues/447) — non-integer scale factors, wontfix ·
  [#225](https://github.com/IIIF/api/issues/225) — scale factors vs sizes (historical)
- [OpenSeadragon `iiiftilesource.js`](https://github.com/openseadragon/openseadragon/blob/master/src/iiiftilesource.js) ·
  [OSD#1394](https://github.com/openseadragon/openseadragon/issues/1394) — scaleFactors/maxLevel
- [openlayers#16924](https://github.com/openlayers/openlayers/issues/16924) — OL IIIF edge-URL bug
- [IIIF roadmap Aug 2025](https://iiif.io/news/2025/08/11/roadmap/) — Presentation v4 only; no Image API 4.0 / tiling change in flight
- Full research report: attached to DEV-6976 ("IIIF tiling, tile sizes & scale factors")
- Pyramidal-TIFF handoff: `ptiff-service-file-handoff.md` (attached to the "Moving towards pyramidal TIFF" project)

## Relationship to the "Moving towards pyramidal TIFF" project

This fix is **task T1** — the top implementation priority — of the
[Moving towards pyramidal TIFF](https://linear.app/dasch/project/moving-towards-pyramidal-tiff-402cf7f500bf)
project (Ivan, status: Next). That project's goal is to drop Kakadu and switch
Service Files from JP2 to pyramidal TIFF sourced from originals, with OpenJPEG for
on-demand `jp2` output. Its handoff document lists T1 as *"Fix scaleFactors bug —
applicable to existing JP2 corpus, no re-conversion needed"* — i.e. exactly DEV-6981.

The pyramid rules in this plan match the project's **settled rules** exactly:

| Rule | ptiff handoff (settled) | This plan |
|------|-------------------------|-----------|
| Scale factors | `[1, 2, 4, …, 2^(levels-1)]` | `[2^0 … 2^n]` |
| Pyramid depth | halve until `max(w,h) ≤ tile_size` | `n = ceil(log2(max(w,h) / tile))` |

Both formulas produce identical pyramids (e.g. `0803`/1024 → `[1,2,4,8]`). Shipping
this fix now, decoupled from the encoding switch, is deliberate: it repairs the
**existing corpus** with no re-conversion, and lands the pyramid contract the ptiff
writer will later produce by construction.

**Tile-size decision belongs to the project, not this fix.** The carved-out write-path
tile-size item maps onto the project's open research item **R1 (tile size)**, and the
two current sources disagree:

- DEV-6976 report recommends **512** (measured byte/overfetch trade-off).
- ptiff handoff R1 hypothesizes **1024**, pending Loki usage analysis + decode benchmarks.

Routing that call to R1 (informed by the Loki data) rather than baking it into a
descriptor bug fix is why the write-path change is a separate follow-up. Both sources
agree on the ÷16 constraint (JPEG-in-TIFF).

## Carved-out follow-ups (separate issues)

- **Is `0801` actually tiled?** Check the codestream on disk (`kdu_jp2info`,
  `jpylyzer`, or a `SIZ`/`Stiles` dump). If untiled, decide whether untiled JP2s
  should **omit** `tiles` rather than advertise a whole-image tile, and count how many
  assets are in that class. Needs filesystem access; indeterminable over HTTP.
- **Explicit `512×512` stored tiles on the write paths** (new assets only): JP2
  `Stiles={512,512}` + explicit `Clevels`; TIFF `TIFFTAG_TILEWIDTH/TILELENGTH = 512`
  instead of the `TIFFDefaultTileSize()` fallback (`SipiIOTiff.cpp:1967`), with a
  `% 16 == 0` assertion. C++ hot path — benchmark-gated per CLAUDE.md.

## Session Handoff (2026-08-18)

The descriptor fix **and** the FFI narrowing are both implemented, tested, and
pushed. This plan's Implementation Approach is fully checked off. The next session
is for **discussion, not implementation** — specifically the write-path defaults
findings below and the tile-size question they raise. Nothing is blocked.

### State — what is shipped

| Work | Commit | Where |
|------|--------|-------|
| Descriptor fix (one tile-grid pyramid feeds `sizes` + `scaleFactors`) | `ec0ed00` `fix(server-rs)` | sipi PR #785, pushed |
| FFI narrowing (drop dead `clevels` from `SipiImageDims`, 24→20 bytes) | `91a1a73` `refactor(ffi)` | sipi PR #785, pushed |
| Plan checkbox updates | `357941d` `docs` | dasch-specs `specs/info-json-scalefactors`, pushed |

Gates ran green locally this session: `bazel-test-unit` 26/26, `bazel-test-e2e`
28/28, `bazel-rustfmt-check`, `bazel-clippy-check`. The narrowed ABI is pinned on
both sides — the C++ `static_assert(sizeof(SipiImageDims) == 20)` compiles and the
Rust `offset_of!` layout test passes. PR #785 is ready for review; not yet merged.

### Findings to discuss — default write-path pyramid (no parameters)

Investigated in this session. The output format is chosen by the file extension;
the two service-file formats differ. All JP2 defaults key off
`mindim = min(width, height)` (`SipiIOJ2k.cpp:1068-1218`):

**JP2 stored tile size (`Stiles`):**

| `mindim` | Stored tiling |
|----------|---------------|
| ≤ 1024 | untiled (no `Stiles`) |
| 1025–2047 | 256 × 256 |
| ≥ 2048 | 1024 × 1024 |

**JP2 stored resolution levels (`Clevels` — DWT depth; scale factors `2^0 … 2^Clevels`):**

| `mindim` | `Clevels` | `Clayers` |
|----------|-----------|-----------|
| ≤ 1024 | not set → Kakadu default (5) | default |
| 1025–2048 | 3 | 3 |
| 2049–4096 | 5 | 5 |
| > 4096 | 8 | 8 |

Always lossless (`Creversible=yes`), `Corder=RPCL`, `Cprecincts={256,256}`,
`Cblk={64,64}`, SOP+EPH, `Sprofile=PART2`.

**TIFF** (`SipiIOTiff.cpp:1841`): a pyramid is written **only** when the caller
passes `Pyramid=yes`. With no parameters the output is a plain, single-resolution,
**untiled scanline TIFF** — no pyramid. When requested, levels `reduce = 0..5`
(stop at `min(w,h) ≤ 32`), each tiled via libtiff `TIFFDefaultTileSize()`. Pyramidal
TIFF is the Service File format per ADR-0009.

**Key consequence (the reason this is worth discussing):** after this fix, the
advertised `info.json` pyramid depth is **decoupled** from the stored depth — it is
derived from the *tile grid*, not stored `Clevels`. So the default JP2 tile size
(256 or 1024) now drives the *advertised* `scaleFactors`, where before the stored
`Clevels` did. A JP2 stored with `Clevels=8` but a 1024 tile advertises
`scaleFactors [1,2,4,8]`; the extra stored levels are simply resampled on request
(all HTTP 200 via `SipiSize::REDUCE`). Correct, but it moves the lever.

### Open questions for the next session

1. **Is the default JP2 tile-size step function (256/1024) still what we want**, now
   that it — not stored `Clevels` — sets the advertised pyramid depth?
2. **R1 (tile size): 512 vs 1024.** DEV-6976 report recommends 512 (measured
   byte/overfetch trade-off); the ptiff handoff hypothesizes 1024 pending Loki usage
   + decode benchmarks. Project-level call (R1), deliberately not baked into this bug
   fix. Both agree on the ÷16 (JPEG-in-TIFF) constraint.
3. **Boundary inconsistency**: JP2 tiling uses `>=` while `Clevels` uses `>`, and the
   two ladders were tuned independently. Worth a deliberate re-derivation, or leave?
4. **TIFF default is untiled/no-pyramid unless `Pyramid=yes`** — confirm the ptiff
   migration path always sets it for Service Files, or the descriptor's untiled
   fallback (512-reference `sizes`, no `tiles`) is what gets served.

None of the above blocks merging PR #785 — they are follow-up scope (the write-path
tile-size item + R1), captured under "Carved-out follow-ups" above.
