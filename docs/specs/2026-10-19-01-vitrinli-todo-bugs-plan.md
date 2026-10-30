---
title: "fix: Vitrinli Todo bug backlog (restricted view, CORS, EXIF, JPEG write, JP2 hang)"
type: fix
date: 2026-10-19
author: Ivan Subotic
status: implemented
repositories: []
---

# fix: Vitrinli Todo bug backlog

## Overview

Fixes every Vitrinli bug in Linear state **Todo**, in six phases on one branch and one PR, plus a seventh phase for editor tooling (a compile database for clangd and clang-tidy).
The source is the DEV team's `vitrinli` label, merged with a title/description text search
for "sipi" and "vitrinli". The text search added nothing new. Three of the bugs are sub-issues
(DEV-6211, DEV-6361, DEV-6118), so Linear views that hide sub-issues do not show them.

| Issue | Title (short) | Phase | Outcome |
|---|---|---|---|
| DEV-6211 | JPEG `write()` opens without `O_TRUNC` | 1 | fix |
| DEV-7431 | `Exif::addKeyVal(tag, group, …)` ignores a value for an existing key | 2 | fix |
| DEV-6361 | `EXIFTAG_FLASHENERGY` declared as a scalar rational | 2 | verify against the pinned libtiff, add a guard test; expected not-a-bug |
| DEV-7520 | `info.json` answers credentialed requests with `ACAO: *` | 3 | fix |
| DEV-7432 | ISO 15444-4 `file3.jp2` hangs | 4 | fix (root cause reproduced: subsampled chroma) |
| DEV-7485 | Size-restricted `info.json` dims disagree with the renderer and with region coordinates | 5, 6 | fix |
| DEV-7456 | Seams between restricted-view tiles | 6 | fix |
| DEV-6118 | `full/max` returns the largest `sizes` entry | 6 | regression test; expected not reproducible |
| — | clangd/clang-tidy have no compile database (session request 2026-10-20) | 7 | build tooling |

**Out of scope (not bugs):**
- DEV-7075 (XMP parse/validate) is an enhancement and gets its own plan.
- DEV-5937 (e2e against the release image) and DEV-5938 (Docker build caching) are
  maintenance. DEV-5938 is obsolete: no Dockerfile exists, and `rules_oci` + RBE already
  cache dependency compilation.

## Problem Statement / Motivation

- **Restricted view is broken for every tiling IIIF viewer.**
  - Since wave 2, each restricted tile is scaled on its own with `ceilf` rounding, so tiles
    do not line up (DEV-7456).
  - Size-restricted `info.json` advertises a coordinate space that region requests do not use.
    Spec-following viewers (OpenSeadragon fed `info.json`, Mirador, UV) therefore render
    garbage (DEV-7485).
  - Projects that rely on restricted view (e.g. `0854` on demo) look much worse than before v9.
- **dsp-app cannot fetch `info.json` with credentials** (DEV-7520). This forced a workaround
  in dsp-app#3509.
- **A subsampled JP2 pins a CPU core forever** (DEV-7432). Kakadu spins in the decode thread,
  and the abandoned thread keeps spinning past the deadline. A few requests saturate a host.
- **Silent data errors.**
  - A second EXIF value for an existing tag is dropped (DEV-7431). This applies only to the
    tag/group `addKeyVal` overloads; the key-string overload already replaces.
  - Overwriting a larger file with a JPEG leaves stale trailing bytes (DEV-6211).

## Proposed Solution

One phase per reviewable change, ordered from small and certain to large and risky:

1. **JPEG write truncation.** Add `O_TRUNC` to the file-path branch of `SipiIOJpeg::write()`.
2. **EXIF tag handling.**
   - The tag/group `addKeyVal` overloads replace an existing value instead of appending.
   - DEV-6361 is verified against the pinned libtiff source and locked in with a test.
3. **`info.json` CORS parity.** `serve_info_json` computes its CORS headers through
   `cors_allow`, as `serve_knora_json` already does.
4. **Subsampled JP2 decode.** `SipiIOJ2k::read` requests full-height stripes from subsampled
   chroma, which makes Kakadu spin forever (reproduced). Decode subsampled components at their
   own size and upsample them, with a fail-fast guard for unsupported shapes.
5. **One restriction decision in the shell.**
   - The Rust shell computes the restricted dims `(RW, RH)` once, refuses no-op restrictions
     by kind, and passes `(RW, RH)` across the FFI seam as two integers.
   - `info.json`, `knora.json` and the image route then agree on refusals and dims.
   - Images smaller than the restriction box are served instead of refused.
6. **The restricted image served as its own image** (DEV-7485 option 1, which also fixes
   DEV-7456).
   - Region and `pct:` coordinates resolve against `(RW, RH)`.
   - Pixels are cut from the one image rendered at `(RW, RH)`.
   - The canonical URL and the cache key use the restricted coordinate space.

## Alternative Approaches Considered

- **DEV-7485 option 2: keep native coordinates.** Report native dims plus
  `maxWidth`/`maxArea`. Rejected:
  - OpenSeadragon 6.1.1 ignores `maxWidth`/`maxArea`, so the seams stay.
  - It discloses the native size that `restricted_dims` deliberately hides.
- **DEV-7456 per-tile exact rounding.** Resample each native region to exactly
  `[floor(x·f), floor((x+w)·f))`. Rejected in favour of rendering once and cropping:
  - Crop-from-one-rendering is exact by construction.
  - It needs no new resampling path.
  - It stays out of `SipiImage::read` and the codecs, so there is no hot-path change.
- **Single source in C++.** The engine computes `(RW, RH)` and Rust asks for it through a new
  FFI call. Rejected:
  - The production IIIF parsing already lives in Rust (ADR-0021).
  - The S2-16 ruling already made `clamp_dims_to_size` the one clamp, so that the server
    never re-implements a security-relevant clamp.
  - Passing two integers is a smaller seam change than adding a function.
  - Cost: one shape probe on a deadline thread per `info.json`/`knora.json`, and one extra per
    sized-restrict image request (measured in Phase 5).

## Technical Considerations

The sections below follow the design's weight, not phase order. Where they and a phase
checklist differ, the checklist is authoritative.

The labels S2-04, S2-09 and S2-16 name findings of the 2026-09 security-hardening wave 2:
- **S2-04**: restricted views capped by a sampling factor across regions. This is the `f`-cap
  in `serve_image.cpp`.
- **S2-09**: a restriction that restricts nothing is refused (the dims check in
  `serve_image.cpp`).
- **S2-16**: restricted `info.json`/`knora.json` hide native dims and tiles (`restricted_dims`
  in `routes.rs`).

### Phase 5: one restriction decision in the shell (DEV-7485, part 1)

Today the restricted dims are computed twice, with different rounding:
- `info.json`/`knora.json` get them from `restricted_dims` (`src/server/rust/src/routes.rs`)
  via `iiif_parser::clamp_dims_to_size` (`src/iiifparser/rust/parse.rs`), which uses
  `.round()` and integer division.
- The renderer re-parses the `restricted_size` string in C++ and calls `SipiSize::get_size`,
  which uses `ceilf`. `info.json` and `full/max` can differ by 1 px.
- The no-op refusal (S2-09) is decided on dims in C++ (`serve_image.cpp:590`), so `size=max`
  is refused on the image but answered 200 with native dims on `info.json`.
- An image already smaller than the box (e.g. 100×80 under `!128,128`) is refused on the image
  today: 400 from "Upscaling not allowed", or 403 once dims are compared. Yet it is no
  disclosure risk.

The fix:
- **Rust decides.** One helper in `routes.rs` takes the preflight `size`, the watermark
  presence and the native dims.
  - It returns either `(RW, RH)` as `NonZeroU32`s or a refusal.
  - Presence of a `size` is decided with `access_kv_str`.
  - A value that cannot be converted for the seam is a 500, never "no size".
  - It refuses on the restriction **kind**, not on dims, when no watermark is set. These are
    the S2-09 cases:
    - `max`/full
    - `pct:` ≥ 100, with or without `^`
    - `red:0`

    These are the kinds that can never reduce any image. They mark a misconfiguration.
  - A `^` prefix on a box kind (e.g. `^!128,128`) still reduces, since `clamp_dims_to_size`
    caps at native, so it is not refused.
  - The kind comes from a new public classifier in the `iiif_parser` crate. `parse_size` and
    `SizeParts` stay private. It runs only on restrict strings, so the hot-path rule does not
    apply.
  - A box restriction (`!w,h`, `w,`, `,h`, `w,h`, `pct:` < 100, `red:n` with n ≥ 1) caps at its
    box. An image already inside the box gets `(RW, RH)` equal to its native dims and is served:
    that is within the configured policy.
    - This deliberately narrows today's dims-based check, which refuses any restriction whose
      result equals the native dims.
    - The ADR records this rule.
  - Refusal status follows each route's existing convention:
    - 403 on the image and on `info.json`.
    - 404 on `knora.json`, which hides existence.
  - An unparsable restrict `size` is a 500, since it is a preflight misconfiguration.
  - A restricted axis below 2 px is refused as well, because the codecs reject scaling to a
    1 px axis (`geometry.cpp:531-542`). Example: `!128,128` on a 2000×10 image.
  - A never-reducing size *with* a watermark (e.g. `size=max` plus a watermark) is not refused.
    The helper returns the native dims, so the image is served as a restricted image at
    native size with the watermark, and `info.json` reports native dims plus
    `maxWidth`/`maxHeight`.
  - When a box restriction yields the native dims without a watermark, the shell logs a
    warning, so an over-large configured box is visible.
  - `info.json`, `knora.json` and the image request all use the helper, so they agree on
    refusals and dims.
- **Native dims on the image path.** The Rust image request builder has no dims today; C++
  probes the shape itself. A sized-restrict request therefore adds one `ffi::image_dims` probe
  in the shell.
  - Cost: since the probe moves to a deadline thread, **every** `info.json`/`knora.json`
    (restricted or not) pays one thread spawn and one libmagic database load on top of the
    header read (`read_shape` calls `getFileMimetype`).
    - A sized-restrict image request pays this probe in the shell plus the existing one in C++.
    - Image requests already pay the same on the serve path, so `info.json` rises to
      image-request cost, not beyond.
    - Phase 5 measures `info.json` latency before and after. If the median rises by more than
      2 ms, the deadline wrapper is dropped (H7 tracks it).
    - The handle is a `thread_local` `unique_ptr` closed at thread exit (`Parsing.cpp`), so it
      does not leak. Only a wedged thread keeps it, the same residual the serve path accepts.
  - A probe failure or timeout is a 500 and fails closed: it never sends `0, 0`, which would
    serve unrestricted. The probe has no Sentry report callback; accepted.
  - The engine rejects a half-set pair (exactly one of the two 0), and `(RW, RH)` larger than
    the image it reads, with a 500. A file replaced between the two probes therefore cannot
    widen the output.
  - `sipi_image_file_dims` (Lua `SipiImage.dims`) stays without a deadline; see H7.
  - The worker lambda captures by value. The Rust-owned `out`/`emit` are touched only on the
    calling thread.
  - `sipi_image_dims` runs `read_shape` with no deadline (`src/ffi/cpp/sipi_ffi.cpp`), unlike
    the serve path. Phase 5 puts it under the decode deadline, which also bounds `info.json`.
- **Seam.** Replace `const char *restricted_size` in `SipiServeRequest`
  (`src/ffi/cpp/sipi_ffi.h:211`) with `uint32_t restricted_width` and
  `uint32_t restricted_height`, mirrored as `u32` in `src/server/rust/src/ffi.rs:311`.
  - `0, 0` means no size cap. A watermark-only restriction sends `0, 0` plus a `watermark_path`.
  - Two `uint32_t` occupy the 8 bytes of the old pointer, so `sizeof` and every later offset
    stay unchanged. Rename the `offsetof == 96` guard (`sipi_ffi.h:498`, `ffi.rs:1528`) and add
    one for `restricted_height` at 100.
  - `ffi.rs` is the only Rust mirror; `src/scripting/rust`, the fuzz harnesses, benches and
    e2e do not construct the struct.
- **C++** takes `(RW, RH)` from the request.
  - The dims-based S2-09 check is removed, since Rust refuses before the seam.
  - The existing `f`-cap stays in this phase, fed from the passed dims. Phase 6 replaces it.
  - Exactness (`full/max` == `(RW, RH)` on non-dividing sizes) is only guaranteed after Phase 6,
    because the `f`-cap still goes through float `ceilf`. Its tests live there.
- **Distortion.** `clamp_dims_to_size` returns a `w,h` restriction verbatim, clamped per axis,
  so a non-aspect `w,h` restriction yields a distorted `(RW, RH)`. That is the configured
  behaviour; keep it.

### Phase 6: the restricted image as its own image (DEV-7456, DEV-7485 part 2)

When `restricted_width > 0`, in `build_image_response` (`src/ffi/cpp/serve_image.cpp`):

1. Resolve the region against `(RW, RH)` with `SipiRegion::crop_coords(RW, RH, …)`.
   Partially-outside regions clamp, and a start beyond the image throws `SipiError`, which the
   seam already maps to 400. Resolve before the HEAD return, so HEAD and GET agree.
2. Resolve the client `size` against the region extent with the usual IIIF rules.
   - Larger than the extent requires `^`.
   - The restricted `info.json` advertises `maxWidth` = RW and `maxHeight` = RH (IIIF 3.0
     §5.2: a server that upscales must state a maximum).
   - `^max` therefore equals `max`. Any output wider than RW or taller than RH is a 400.
   - The bound applies to the pre-rotation size, and it is checked before the cache lookup and
     the HEAD return. Rotation swaps or grows the bounding box without adding information.
   - So a `^` request can upscale a small region up to, but never beyond, the
     `full/max` size.
3. Decode the full image at `(RW, RH)`: `SipiImage::read` with a FULL region and
   `SipiSize(PIXELS_XY)` `RW,RH` sets the output to exactly `RW × RH` (`SipiSize.cpp:223-224`).
   The codecs post-scale when the reduce level does not land exactly.
4. Crop with `processing::crop(img, region)` (`src/image_processing/cpp/processing.h:64`). It
   resolves the region against the decoded image, which is exactly `RW × RH`.
5. Scale only when the target differs from the crop extent. The scale operators reject a
   ≤ 1 px axis (`geometry.cpp:531-542`), and a size-`max` tile must be a pure crop. Dispatch on
   `eng.scaling_quality` the same way the codecs do (`SipiIOJ2k.cpp:877-887`).
6. Rotation, quality/ICC, watermark and encode then run in their existing order.
7. Remove the `f`-cap block and its "two residuals accepted" comment. Both residuals are gone by
   construction: overlapping sub-pixel regions are cut from one rendering, and the canonical
   `Link` carries restricted-space coordinates.
8. **Passthrough guard.** The passthrough branch serves the source file as a `FileBody` when
   region and size are FULL, there is no rotation or watermark, and the format matches.
   - Today the `f`-cap's size swap keeps restricted requests out of it.
   - Without it, `full/max/0/default.jp2` on a restricted JP2 would return the native original.
   - Add `restricted_width == 0` to the passthrough condition.
9. **Memory budget.** `compute_decode_dims` estimates from the client region and size against
   native dims. For a restricted request, estimate the decode from (FULL, `PIXELS_XY(RW, RH)`)
   plus the final output dims, so a small region on a non-pyramidal TIFF cannot bypass the
   full-lane budget while decoding the whole image.
   - The shell picks the admission lane (`state.admission.classify`, `routes.rs:321`) before
     the preflight decision, so it cannot know a request is restricted. Small restricted
     regions therefore stay in the Tile lane while the engine decodes the whole image at
     `(RW, RH)`.
   - Accepted: the engine memory budget (estimated as in item 9) still gates every decode, so
     there is no OOM bypass. The cost is Tile-lane thread occupancy under a crawler on
     restricted non-pyramidal TIFFs, tracked in H7(i).
   - `classifier_disagreement_total` rises with restricted traffic as a consequence.
10. **Canonical URL and cache key.** Call `build_canonical_url` (`serve_image.cpp:152`, one
    caller) with `(RW, RH)` and the restricted-space region and size, plus a new parameter.
    - That parameter appends `/r<RW>x<RH>` to the cache key only (`.second`), not to the `Link`
      (`.first`). Without it, a restricted and an unrestricted request for the same canonical
      coordinates would share an entry, which `SipiCache.h` documents as a
      content-substitution primitive.
    - ADR-0025 hashes the key, so a suffix is safe. No other cache-key consumer exists.

Further consequences:
- **Seam guarantee.** A tile requested at size `max` is a pure crop of the one `(RW, RH)`
  rendering, so such tiles stitch to `full/max` exactly. Tiles at another size involve a second
  resample and are not expected to stitch exactly.
- **`info.json` `sizes`/`scaleFactors`.** These derive from the restricted dims
  (`src/server/rust/src/info.rs`, `sizes_for`). For `(RW, RH)` ≤ 512, `pyramid_scale_factors`
  yields `[1]`, so `sizes` is the single entry `(RW, RH)`.
- **Canonical size for every request (owner decision 2026-10-20).** The canonical size in the
  `Link` and the cache key now resolves against the region extent for every request,
  unrestricted ones included. Before, `w,` and `w,h` forms on one region could share a cache
  entry and serve a wrong-size image. The pixels served are unchanged; unrestricted
  relative-size region requests get a corrected `Link` and a new cache key.
- **Unchanged:**
  - Watermark-only restrict: native dims and native coordinates.
  - Allow and Stream.
  - Multi-page images: dims are file-level shape, as today.
- **Cost.** Each restricted region request decodes at `(RW, RH)`:
  - JP2 uses a Kakadu reduce level, so this is cheap.
  - A non-pyramidal TIFF decodes at native size.
  - Accepted, since a restricted `info.json` carries no `tiles`, so viewers mostly request
    `full/max`. No shrunk-image cache (YAGNI).
- **Hot path.** No `src/format_handlers`, `SipiImage` or `iiifparser` source changes in this
  phase; it composes existing operators. Re-checked at phase review.
- **Docs that encode the old semantics:**
  - `UBIQUITOUS_LANGUAGE.md`: the Cache key row (canonical + `/0|/1`), the Watermark row, and
    restrict as a sampling-factor cap.
  - `docs/src/lua/index.md`: the restrict description.
  - Rewrite all of them.
- **ADR.** `docs/adr/0028-restricted-view-coordinate-space.md` records the semantics; no
  existing ADR covers them.
  - It supersedes the wave-2 S2-04 mechanism (a sampling-factor cap applied per region) but
    keeps its two load-bearing decisions:
    - Region requests on restricted images are served, not rejected, because rejecting breaks
      viewers.
    - Nothing served exceeds `full/max`'s information.
- **Release impact.** Third-party clients that send native coordinates for a size-restricted
  image get different results.
  - The phase commit is `fix(server)!:` with a `BREAKING CHANGE:` footer, so release-please
    proposes 10.0.0.
  - dsp-app's current tiling path uses native coordinates. It must move to `info.json` tiling
    in the same DSP release that bumps the sipi digest in dsp-api (Human Actions H1 and H2).

### Phase 3: reversing the credential-less `info.json` decision (DEV-7520)

`info.json` answers `Access-Control-Allow-Origin: *` with no credentials by an explicit,
twice-affirmed decision: first during the Rust cutover (oracle parity) and again in security
wave 2 (S2-16), which says to keep it that way. The stated reasons:

1. "info.json is public (it carries the auth-service block *for everyone*), so it needs no
   credentialed CORS."
2. "a browser cannot read a credentialed cross-origin response under `*`, so authorized dims
   are not exposed cross-origin."

What changed:

- **Reason 1 no longer holds.** Since S2-16, the `info.json` body depends on the caller's
  credentials:
  - A restricted caller gets clamped dims and no tile grid.
  - An authorized caller gets native dims and tiles.
  - An unauthorized caller gets a 401.
  It is per-user content, and dsp-app sends `withCredentials: true` on every IIIF request, so
  `*` makes every such request fail (DEV-7520).
- **Reason 2 protects nothing that is still hidden.** Its protection was that a hostile origin
  could not read an authorized user's native dims. That origin can already read the same
  user's images with credentials, since image responses echo the Origin with
  `Access-Control-Allow-Credentials: true`. It can also read `knora.json`, which already uses
  `cors_allow`. Both give away the native dims. Withholding them from `info.json` alone hides
  nothing.
- **The response now varies on `Origin`.** The ACAO depends on the request's Origin in reflect
  mode too, so `info.json` always sends `Vary: Origin`, in addition to `Cookie, Authorization`.
- **Parity with the image route and `knora.json`.** Making `info.json` match them is consistent
  with the public-server CORS posture: echo the Origin plus credentials, because no allowlist
  can enumerate a public server's legitimate origins. The opt-in `SIPI_ALLOWED_ORIGINS`
  allowlist keeps working for operators who set it.

**Known gap, out of scope:** early-return error responses on `info.json` (403 bare restrict,
500s, resolve and preflight failures) carry no CORS headers, the same as `knora.json` today.
The image route in reflect mode also echoes the Origin without `Vary: Origin`. Both go to a follow-up issue
(Human Action H7).

### Phase 4: `file3.jp2` root cause (DEV-7432, reproduced)

`test/_test_data/images/iso-15444-4/testfiles_jp2/file3.jp2`:
- **Geometry.** 480×640 (W×H), 3 components, 8 bit, sYCC (enumerated 18), single tile, no MCT.
- **Subsampling.** Components 1 and 2 are subsampled 2×2, giving 240×320.
- **Control.** `file2.jp2` is the same image as sYCC without subsampling.

Reproduced on a fresh `just bazel-build` (macOS arm64):

| Command | Result |
|---|---|
| `sipi query file3.jp2` | deadline exceeded after 120.6 s with ~116 s of user CPU: one core spinning |
| `sipi convert file3.jp2` to TIFF and to JPEG | deadline exceeded after 120.1 s, no output |
| `file1`, `file2`, `file4` | 0.04–0.10 s |
| `file2` convert, then `sipi compare` vs `reference_jp2/jp2_2.tif` | mean 0.49, max 1. The sYCC path is correct |
| Server `file3` `info.json` | 200 in 2 ms. `read_shape` is fine |
| Server `file3` `full/max` and `full/120,` | 408 at 60 s, from the request timeout (`DEFAULT_REQUEST_TIMEOUT_SECS`, `src/server/rust/src/config.rs:65`), which fires before the 120 s decode deadline |

**Each hung request pins one core for good.** The abandoned decode thread keeps spinning after
the deadline. A handful of requests for such a file saturates a host until restart.

**Root cause** (`src/format_handlers/cpp/SipiIOJ2k.cpp`, in `SipiIOJ2k::read`):
- `codestream.get_dims(0, dims)` (`:557`) reads only component 0's dims.
- `std::vector<int> stripe_heights(codestream.get_num_components(), dims.size.y)` (`:757`)
  requests component 0's height from every component.
- `decompressor.pull_stripe(dst, stripe_heights.data())` (8 bit at `:774`; the 12- and 16-bit
  cases follow the same pattern) uses the interleaved single-buffer form.

For a subsampled component that asks for more rows than the component has. The sample shows
`kdu_stripe_decompressor::pull_common` busy-spinning in the calling thread, with every Kakadu
worker idle.

An interleaved buffer cannot hold 240-wide chroma in a 480-wide image, and the stripe
decompressor does not upsample. So fixing the heights alone is not enough.

**Fix (decided: decode it, plus a guard):**
1. Before `decompressor.start`, read each component's dims with `codestream.get_dims(c, …)`
   after the existing input restrictions (reduce level and region).
2. When every component's subsampling equals component 0's, keep today's interleaved path
   unchanged.
3. When a component is subsampled by exactly 2 in either or both axes, pull each component into
   its own planar buffer at its own size, then upsample it onto component 0's grid.
   - Use the planar `pull_stripe` overload with per-component heights.
   - Upsample by co-sited sample replication (nearest neighbour) with JPEG 2000 **co-sited**
     siting: sample k sits at reference position k·dx, not centred as in JPEG/JFIF, and covers
     reference positions k·dx … k·dx + dx − 1.
     - For reference column x, the sample index is `floor(x/dx) − ceil(x0/dx)`, clamped to the
       component's extent.
     - Take x0 from `get_dims(c, d, true).pos`, so odd region origins and reduce levels stay
       aligned.
     - Replication, not linear interpolation: against `reference_jp2/jp2_3.tif`, replication
       gives a mean absolute difference of 0.49 (max 1, 51.2 dB PSNR) and co-sited linear
       interpolation 2.32 (max 110, 35.6 dB), so the reference was made by replication.
   - Interleave into the image buffer before the existing colour conversion
     (`convertYCC2RGB`).
   - Decide subsampling with `get_min_subsampling(c, …, true)`, never from a dims ratio. With
     odd extents or a reduce level, chroma dims are `ceil(…)` and almost never exactly half.
   - Use output components (`want_output_comps = true`), as the stripe decompressor counts
     them.
   - **Channel order (owner decision 2026-10-20).** `convertYCC2RGB` hard-codes Cr/Cb/Y. That
     is only right for files whose `cdef` box declares that order (`file2.jp2`); files without
     one (`file3.jp2`, `unit/ycbcr16.jpx`) decode mis-coloured on `main`.
     - The fix reads the declared channel mapping (`jp2_channels::get_colour_mapping`) for
       every JP2, interleaves in Y/Cb/Cr order, and converts from that order.
     - It lands as its own `fix(format_handlers)` commit, a pre-existing bug, ahead of the
       subsampling commit. Palette handling is unchanged.
   - **Upsampling (owner decision 2026-10-20).** The measurement selected co-sited sample
     replication: MAD 0.49 against the reference, versus 2.32 for the linear variant.
     - Replication ships now.
     - Proper co-sited linear upsampling, validated against OpenJPEG, is a follow-up (H7).
   - Memory: the planar buffers plus the interleaved buffer take about 1.5× for 4:2:0.
     Accepted, since only subsampled files take this path.
   - Reference tolerance: which decoder produced `reference_jp2/jp2_3.tif` is unknown, so the
     threshold comes from a measurement, not a guess.
     - The test allows the measured mean absolute difference of the kept method (0.49) + 0.5,
       with PSNR ≥ 30 dB as a sanity bound.
     - The file2 control (mean 0.49, max 1) is the unsubsampled path's baseline, not a target
       for file3.
   - Rejected alternative: `kdu_region_decompressor` with `kdu_channel_mapping` interpolates
     natively, but it would add a second decode path and its own threading and buffer types to
     `SipiIOJ2k`.
4. For any other subsampling shape (factors other than 1 or 2, or mismatched tiles), return
   `std::unexpected(SipiValueError{ErrorCode::kUnsupportedFormat, …})` before decoding, so the
   request fails fast instead of spinning.
   - `policy_for` maps this code to a 500 with a Sentry report.
   - Accepted: such files are rare, and a report is wanted.
5. When any component is subsampled, expand the Kakadu `roi` outward to multiples of the
   subsampling factor before `apply_input_restrictions`, so every component has at least one
   sample. Crop `img` back to the requested region before the existing post-scale. Without
   this, a 1×1 region at an odd origin gives a 0-sample chroma buffer and an out-of-bounds
   read.
6. Apply the same to the 8-, 12- and 16-bit cases.

**Hot path.** This touches the JP2 decode path, so a before/after `just bench decode` is
required. The baseline must filter out the new `file3` case, which would hang today.

**Follow-ups, out of scope (Human Action H7):**
- Abandoned deadline threads never stop, so any future codec spin leaks a core.
- The 60 s request timeout fires before the 120 s decode deadline.

### Phase 2: DEV-6361 evidence

libtiff 4.7.2 (pinned in `MODULE.bazel`) declares `EXIFTAG_FLASHENERGY` as `TIFF_SETGET_FLOAT`.
That is a scalar, so SIPI's `EXIF_DT_RATIONAL` (`src/format_handlers/cpp/SipiIOTiff.cpp:308`)
is correct. The variable-count form belongs to `TIFFTAG_EP_FLASHENERGY`, which sits in a
disabled `#if 0` block. `SPATIALFREQUENCYRESPONSE` (`:309`, `EXIF_DT_PTR`) matches libtiff's
`C16_UINT8`. This comes from upstream; Phase 2 checks the fetched source.

### Conventions

- **Commit scopes** follow `CONVENTIONS.md` § Module Layout and are scoped by responsibility:
  `format_handlers` (Phases 1, 4), `metadata` (2) and `server` (3, 5, 6). Phases 5 and 6 edit
  `src/ffi` code, but the point is restricted-view semantics, not the seam mechanism.
- **Commits.** One commit per phase, except that a Phase 1 overwrite fix for another handler
  is its own commit, as the checklist says. The PR lands by rebase-merge.
- **Verification commands.** The repo has no `eng.yaml`, so verification uses the `CLAUDE.md`
  recipes verbatim.
- **Rust lint gates.** Every phase that touches Rust runs `just bazel-rustfmt-check` and
  `just bazel-clippy-check`. They are CI gates that no test recipe exercises.
- **Hot-path rule.** Phases 1, 2, 3, 5 and 6 change no decode/encode loop: Phase 1 changes an
  `open()` flag, and Phase 6 composes existing operators. Phase 4 changes
  `SipiIOJ2k::read`, so it carries a before/after `just bench decode` deliverable.
  - Phase 5 adds a classifier to `src/iiifparser/rust/parse.rs` but touches neither
    `parse_request` nor `parse_size`. It runs only on restrict decisions.
  - Phase 1 changes an `open()` flag in `src/format_handlers`, outside any decode/encode loop.
  - Neither needs a benchmark; the phase review confirms this.
- **Line numbers.** They are as of `d40c257c` and shift as phases land. Locate code by symbol
  name.

## Implementation Phases

#### Phase 1: JPEG write truncates an existing file (DEV-6211)

- [x] Add `O_TRUNC` to the file-path `open()` in `SipiIOJpeg::write()` (`src/format_handlers/cpp/SipiIOJpeg.cpp`, the non-streaming, non-`stdout:` branch)
- [x] Add a `JpegWrite` test in `src/format_handlers/cpp/jpeg_write_test.cpp` (part of `//src/format_handlers:formats_test`): pre-fill the destination with junk larger than the output, write a JPEG, assert the file size equals that of a fresh write and the file re-reads with the expected dimensions
- [x] Add one parametrised overwrite test for TIFF, PNG and J2K in a new `src/format_handlers/cpp/format_overwrite_test.cpp`, added to the `formats_test` srcs in `src/format_handlers/BUILD.bazel`, using `sipi::test::tmp_dir()` and comparing file sizes, never bytes (ICC headers are wall-clock stamped); write J2K as `"jpx"`; no new env or license setup is needed (`malformed_tiff_test.cpp` already writes `jpx` under `formats_test`); TIFF (libtiff `"w"`) and PNG (`fopen("wb")`) are expected to pass unchanged, J2K (Kakadu `jp2_family_tgt::open`, closed source) is the open question
- [x] If the J2K (or any other) overwrite case fails, fix that handler in a separate `fix(format_handlers)` commit naming the handler
- [x] Run `bazel test //src/format_handlers:formats_test --test_output=streamed`; it passes
- [x] Run `just bazel-test-unit`; it passes
- [x] Run `just bazel-test-approval`; it passes with no golden changes
- [x] Commit `fix(format_handlers): truncate an existing file when writing a JPEG` referencing DEV-6211; the body notes that a failed write now leaves an empty file instead of stale bytes, as TIFF and PNG already do
- [x] Run `just commit-lint`; it passes
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 2: EXIF tag values replace instead of append (DEV-7431, DEV-6361)

- [x] Change both tag/group `Exif::addKeyVal` overloads in `src/metadata/cpp/exif.h` (the scalar overload including its string early-return, and the pointer overload) to erase every existing entry for the key immediately before each `exifData.add(key, v.get())` (after the type dispatch and `v->read`, so a call that throws leaves the existing entry untouched; in the scalar overload also before the string branch's `add`): `ExifData::erase` removes one entry per call, so loop `while ((pos = exifData.findKey(key)) != exifData.end()) exifData.erase(pos);`; leave the key-string overload (`exifData[key] = val`) and the existing `SipiError` throws unchanged
- [x] Audit the tag/group `addKeyVal` callers in `SipiIOTiff.cpp` `readExif`; confirm none relies on keep-first, and state the audit result in the commit body
- [x] Add tests in `src/metadata/cpp/exif_rational_regression_test.cpp` (`//src/metadata:exif_rational_test`), without adding friends or accessors to `Exif` (`exifData` is private): call each tag/group overload twice for the same tag (scalar `Exiv2::Rational`, string, and the pointer overload with an `Exiv2::Rational[4]`); assert with `getValByKey` that the second value wins, and that the key name (e.g. `Exif.Photo.BrightnessValue`) occurs once in the text of `std::ostringstream << *exif`
- [x] Add a test starting from `img.getExif()` of `image_orientation.jpg` (which already holds `Exif.Photo.BrightnessValue` 0/0): a tag/group `addKeyVal` of that tag leaves it once in the `operator<<` text, carrying the new value
- [x] Add a test that an unsupported `T` throws `SipiError` and leaves the existing value intact
- [x] In `NonZeroOverZeroRationalFailsWrite` only (`src/format_handlers/cpp/tiff_exif_rational_test.cpp`), replace the fresh `std::make_shared<Sipi::Exif>()` and its comment with `auto exif = img.getExif(); ASSERT_NE(exif, nullptr);` so the test writes over the JPEG's own 0/0 entry; leave the two `LensSpecification` tests as they are
- [x] Locate the fetched libtiff source with `find -L "$(bazel info output_base)/external" -name tif_dirinfo.c` (`external/` entries are symlinks, so `-L` is required), and confirm `EXIFTAG_FLASHENERGY` is `TIFF_SETGET_FLOAT` and `EXIFTAG_SPATIALFREQUENCYRESPONSE` is `TIFF_SETGET_C16_UINT8`; if either differs, correct the matching entry in the `SipiIOTiff.cpp` EXIF tag table
- [x] Add a FlashEnergy TIFF round-trip test in `tiff_exif_rational_test.cpp`, following the sibling `TiffExifRational` tests: `addKeyVal(EXIFTAG_FLASHENERGY, "Photo", Exiv2::Rational{5, 2})`, write a TIFF, read it back, assert `EXPECT_NEAR(first / second, 2.5, 1e-4)` (the read side goes through `float`, so the pair need not come back as exactly 5/2)
- [x] Run `bazel test //src/metadata:exif_rational_test //src/format_handlers:formats_test --test_output=streamed`; both pass
- [x] Run `just bazel-test-unit`; it passes (`exif.h` is included across the engine)
- [x] Run `just bazel-test-approval`; it passes with no golden changes
- [x] Commit `fix(metadata): replace an existing EXIF entry in addKeyVal instead of appending` referencing DEV-7431, with the DEV-6361 verdict (table confirmed correct, or what was corrected) in the commit body
- [x] Run `just commit-lint`; it passes
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 3: `info.json` echoes the Origin with credentials (DEV-7520)

- [x] Extract one helper in `src/server/rust/src/routes.rs` (e.g. `json_cors(origin, allowlist) -> (Option<String>, bool)`) holding the `match` that `serve_knora_json` uses today: no Origin → `*`; otherwise `cors_allow` → echoed Origin (allowed) or no ACAO (denied); switch `serve_knora_json` to it
- [x] Use the helper in `serve_info_json`, and always add `Origin` to the `info.json` `Vary` list (reflect mode and allowlist mode, with or without an Origin), merged with the existing `Cookie, Authorization` values, because the response now varies on `Origin`
- [x] Pass `&state.allowed_origins` at the single `serve_info_json` call site (`dispatch_engine` in `routes.rs`); the 401 auth-challenge body goes through the same `json_response` call, so it gets the headers with no second code path
- [x] Rewrite the "deliberately public, credential-less" comment in `serve_info_json`, the `json_response` rustdoc ("`Some("*")` for info.json") and the inline "`*` (public info.json) must not pair with credentials" comment in `json_response` to state the current invariant
- [x] Update the info.json CORS rows in `docs/src/development/testing-strategy.md` (CORS table) to the new semantics and tests
- [x] Reword the `routes.rs` unit-test comment "Public `*` (info.json) → ACAO but never credentials" and add to the `SIPI_ALLOWED_ORIGINS` section of `docs/src/guide/running.md` that `info.json` is covered by the allowlist
- [x] Assert in the info.json preflight e2e test that `Access-Control-Allow-Methods` and `Access-Control-Allow-Headers` are explicit lists, not `*` (a credentialed preflight cannot use `*`)
- [x] Change `cors_info_json_with_origin` (`test/e2e/tests/iiif_compliance.rs`) to assert the echoed Origin, `Access-Control-Allow-Credentials: true` and a `Vary` containing `Origin`; keep `cors_info_json_without_origin` asserting `*` and no `Access-Control-Allow-Credentials`; drop the stale "info.rs hardcodes *" comment text in these tests
- [x] Add an e2e test for an `info.json` request with an `Origin` and a `Cookie` header (the issue's curl reproduction): echoed Origin, `Access-Control-Allow-Credentials: true`
- [x] Add a 401 `info.json` e2e test on `/auth/lena512.jp2/info.json` with an Origin: echoed Origin and `Access-Control-Allow-Credentials: true`
- [x] Add an e2e test for an OPTIONS preflight on an `info.json` path (routing is path-agnostic, so this is a guard): echoed Origin and `Access-Control-Allow-Credentials: true`
- [x] Add the first CORS allowlist e2e test in `test/e2e/tests/security.rs`, modelled on `public_hosts_allowlist_substitutes_hostile_forwarded_host` (`SipiServer::start_env` with `SIPI_ALLOWED_ORIGINS=https://allowed.example` and its own `SIPI_CACHE_DIR`): a denied Origin gets no ACAO on `info.json`; an allowed Origin gets the echo and a `Vary` containing `Origin`
- [x] Run the e2e tests and confirm `git status --porcelain test/e2e` shows no changed `.snap` and no `.snap.new`: `iiif_compliance__info-json-headers.snap` records ACAO, but its request sends no Origin, so it stays `*`
- [x] Run `bazel test //test/e2e:iiif_compliance //test/e2e:security //test/e2e:server --test_output=streamed`; all pass
- [x] Run `just bazel-rustfmt-check`; it passes
- [x] Run `just bazel-clippy-check`; it passes
- [x] Commit `fix(server): echo the Origin with credentials on info.json` referencing DEV-7520; the body states that `info.json` now varies on `Origin`, so caches in front of SIPI must honour `Vary`
- [x] Run `just commit-lint`; it passes
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 4: Subsampled JP2 components decode instead of spinning (DEV-7432)

- [x] Add a dedicated full-image decode benchmark `decode_full_subsampled_jp2` for `file3.jp2` to `src/format_handlers/cpp/decode_benchmark.cpp`; resolve the file as `$SIPI_WORKSPACE_ROOT` (exported by `just bench`) + `/test/_test_data/images/iso-15444-4/testfiles_jp2/file3.jp2`, and `std::abort()` on a failed read like the sibling benchmarks; the existing `SIPI_DECODE_BENCH` macro cannot host it (it reads `@sipi_bench_fixtures` and decodes 1024 px tiles from a 480×640 image)
- [x] Record the baseline on the unchanged decode code, from the repository root: `just bench decode --benchmark_repetitions=20 --benchmark_filter='^(decode_tile|decode_thumb)/' --benchmark_out="$PWD/bench-before.json" --benchmark_out_format=json` (the new benchmark is excluded; it would hang before the fix)
- [x] In `SipiIOJ2k::read`, after the input restrictions, read each output component's subsampling with `codestream.get_min_subsampling(c, subs, true)` and its dims with `codestream.get_dims(c, d, true)` (output components, `want_output_comps = true`, as `kdu_stripe_decompressor` counts them); keep the interleaved `pull_stripe` path unchanged when every component's subsampling equals component 0's
- [x] Place the subsampling guard directly after `validate_decode_dims` (before the Kakadu thread env is created): any component finer than component 0, any factor other than 1 or 2, or a component count that differs between the guard, the buffers and the loop bounds returns `kUnsupportedFormat`; use the output-component count (`get_num_components(true)`) everywhere, including `img->getNc()` and `stripe_heights`
- [x] When any component is subsampled, expand `roi` outward to multiples of the subsampling factor on the reference grid before `codestream.apply_input_restrictions(…, &roi)` in `SipiIOJ2k::read`, decode, then crop `img` back to the requested region (offsets divided by the reduce factor) before the post-scale block (`if ((size != nullptr) && (!redonly))`), so every component has at least one sample (a 1×1 region at an odd origin would otherwise give a 0-sample chroma buffer and an out-of-bounds read)
- [x] For components whose subsampling is exactly 2× component 0's in x and/or y, call the planar `pull_stripe` overload (one buffer per component, `stripe_heights[c]` = that component's height), upsample each subsampled component onto component 0's grid with co-sited JPEG 2000 siting (sample k at reference position k·dx; left index `floor(x/dx) − ceil(x0/dx)` clamped to `[0, w_c − 1]`, right index `min(left + 1, w_c − 1)`; derive positions from `d.pos`, so odd region origins and reduce levels stay aligned), and interleave into the image buffer before the existing colour conversion; cover the 8-, 12- and 16-bit cases; leave the existing `jp2_channels`/palette handling (component i = channel i) unchanged
- [x] The guard's error is `std::unexpected(SipiValueError{ErrorCode::kUnsupportedFormat, …})` (maps to 500 with a Sentry report via `policy_for`; accepted, since such files are rare and worth seeing); subsampling is decided from `get_min_subsampling`, never from a dims ratio
- [x] Add a `//src/format_handlers:formats_test` test: `read` on `file3.jp2` completes and returns 480×640 with 3 channels
- [x] Measure the `file3.jp2` decode against `reference_jp2/jp2_3.tif` (read both through `SipiImage::read`, check `getNx/getNy/getNc` first) with the co-sited linear upsampling, and once with a temporary nearest-neighbour variant behind a local flag; record both numbers in the commit body, then delete the flag and the nearest variant; keep co-sited unless nearest's mean absolute difference is lower by more than 0.5 (then keep nearest and update the Phase 4 Technical Considerations in the same commit)
- [x] Add a `//src/format_handlers:formats_test` test comparing the `file3.jp2` decode with `reference_jp2/jp2_3.tif`: mean absolute difference at most the measured value of the kept method + 0.5, and PSNR ≥ 30 dB as a sanity bound
- [x] Add a `//src/format_handlers:formats_test` test that `file2.jp2` (sYCC, not subsampled) still matches `reference_jp2/jp2_2.tif` (mean absolute difference ≤ 1.0), guarding the unchanged interleaved path
- [x] Add `//src/format_handlers:formats_test` tests for a reduced-resolution read and for region reads of `file3.jp2` with an odd origin and odd extent, a 1×1 region at an odd origin, and a 1×N region at an odd origin: each completes with the expected dims and does not hit the unsupported guard
- [x] Add a `//src/format_handlers:formats_test` test that a JP2 with an unsupported subsampling factor returns `kUnsupportedFormat` without hanging (no tool in the repo can encode one: copy `file3.jp2` into `test/_test_data/images/malformed/` with the SIZ marker's `XRsiz`/`YRsiz` for components 1 and 2 patched to 4, documented in `test/_test_data/images/malformed/README.md` like the existing hand-edited fixtures; the guard fires before `decompressor.start`, so the inconsistent packet data is never decoded); the test runs the read on a worker thread and waits with a bounded `wait_for` of 5 s, so a regression fails instead of hanging `formats_test`
- [x] Add a truncated copy of the patched-SIZ fixture (main header plus the start of the codestream, under the 32 KB fuzz `max_len`; the guard fires on the header) to `fuzz_seeds_j2k` in `test/_test_data/BUILD.bazel`, in this same commit as the fix; `file3.jp2` itself (242 KB) is not a seed, so a valid subsampled decode and the 12/16-bit planar path stay outside the fuzz corpus (noted in the commit body)
- [x] Run `bazel test //src/format_handlers/fuzz:j2k_decode_fuzz //src/format_handlers/fuzz:j2k_roundtrip_fuzz --test_output=streamed`; both pass
- [x] Add `assert_jp2_decode(2)` and `assert_jp2_decode(3)` to the conversion tasks in `test/e2e/tests/cli_conversions.rs`, and update the "others in the conformance set have known issues" comment
- [x] Add a server e2e test in `test/e2e/tests/server.rs` with a 30 s client timeout, using the shared `server()` helper if its image root reaches `test/_test_data/images/iso-15444-4/testfiles_jp2/` (check the prefix mapping in the e2e config first), else `SipiServer::start_env` with that root: `file3.jp2` `info.json` answers 200 with 480×640, and `full/max/0/default.jpg` answers 200 with a 480×640 image
- [x] Run `just bench decode` with the same flags and filter to `"$PWD/bench-after.json"`, then `just bench-compare bench-before.json bench-after.json`; delete both files afterwards (never commit them); the pre-existing cases show no regression beyond noise (U-test not green, or median shift within the baseline CV)
- [x] Run `just bench decode --benchmark_filter=decode_full_subsampled_jp2`; it completes (records the new path's cost for the commit body)
- [x] Run `bazel test //src/format_handlers:formats_test --test_output=streamed`; it passes
- [x] Run `just bazel-test-approval`; it passes with no golden changes
- [x] Run `just bazel-test-e2e`; it passes
- [x] Read the JP2 channel mapping (`jp2_channels::get_colour_mapping`) in `SipiIOJ2k::read` for every JP2, interleave YCbCr channels in Y/Cb/Cr order on both the interleaved and the planar path, and switch `convertYCC2RGB` to that order; land it as its own commit `fix(format_handlers): convert YCbCr JPEG 2000 images in their declared channel order`, ordered before the subsampling commit
- [x] Add `//src/format_handlers:formats_test` tests: `unit/ycbcr16.jpx` decodes with correct colours (state the check used), `file2.jp2` still matches `reference_jp2/jp2_2.tif` (mean ≤ 1.0), and the `file3.jp2` reference test is unskipped and passes (mean ≤ measured + 0.5, PSNR ≥ 30 dB)
- [x] Re-record the decode benchmark baseline from `1a8b7617` and compare after both commits (`just bench decode` + `just bench-compare`); no regression beyond noise
- [ ] Run `bazel test --config=asan --config=ubsan --test_tag_filters=-no-sanitizer --test_output=errors //src/format_handlers:formats_test //src/format_handlers/fuzz:j2k_decode_fuzz //test/e2e:cli_conversions //test/e2e:server`; it passes (LSan fails on leaks: every planar buffer must be freed on every early return and Kakadu exception path)
- [x] Run `just bazel-rustfmt-check`; it passes
- [x] Run `just bazel-clippy-check`; it passes
- [x] Commit `fix(format_handlers): decode subsampled JPEG 2000 components instead of spinning` referencing DEV-7432; the body names `kdu_stripe_decompressor::pull_common` called from `SipiIOJ2k::read` as the spin, the stripe heights taken from component 0 as the cause, and the reference-diff and benchmark numbers
- [x] Run `just commit-lint`; it passes
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 5: One restriction decision in the shell (DEV-7485, part 1)

- [x] Add a public classifier to the `iiif_parser` crate (`src/iiifparser/rust/parse.rs`, e.g. `pub fn size_never_reduces(size: &str) -> Result<bool, ParseError>`; `parse_size` and `SizeParts` stay private): true for `max`/full, `pct:` ≥ 100 (with or without `^`) and `red:0`; false for every box kind, with or without `^`
- [x] Add `//src/iiifparser/rust:iiif_parser_test` cases for the classifier: `max`, `^max`, `pct:100`, `^pct:200`, `red:0` → true; `!128,128`, `^!128,128`, `pct:50`, `^pct:50`, `128,`, `5000,`, `red:1` → false
- [x] Add one helper in `src/server/rust/src/routes.rs` that takes the preflight `size`, the watermark presence and native dims, and returns `(NonZeroU32, NonZeroU32)` via `clamp_dims_to_size`, or a refusal when the classifier says the size never reduces and no watermark is set, or when either restricted axis is below 2 px (the codecs cannot render a 1 px axis); an unparsable `size` is a 500 (a preflight misconfiguration)
- [x] Decide "a size is present" with `access_kv_str` (not `access_kv_cstring`, which returns `None` for a value containing NUL and would read as watermark-only at native resolution); a `size` that cannot be converted for the seam is a 500
- [x] When the helper returns the native dims without a watermark (a box at or above the image), emit a `log_warn` naming the identifier and the box, so an over-large configured box is visible
- [x] Use the helper in `restricted_dims`: a refusal answers 403 on `info.json` and 404 on `knora.json` (which hides existence, as `bare_restrict_knora_json_is_not_found` expects)
- [x] Run the `read_shape` in `sipi_image_dims` (`src/ffi/cpp/sipi_ffi.cpp`) under `run_with_deadline` with `engine_context_or_default().decode_timeout_ms` (as `image_handle.cpp` does; `engine_context()` throws in `seam_probe_test`, which installs no context); the worker lambda captures the path by value and returns the shape, and `out`/`emit` are written only on the calling thread after success; a timeout returns an error
- [x] In the image request builder in `routes.rs` (the restrict branch that builds `c_size`/`c_watermark`), for a restrict decision with a `size`: probe native dims with `ffi::image_dims`, call the helper, return 403 on a refusal, and fill the request's restricted dims; a probe failure or timeout answers 500 and never sends `0, 0` (fail closed; the probe has no Sentry report callback, accepted)
- [x] Replace `const char *restricted_size` in `SipiServeRequest` with `uint32_t restricted_width` and `uint32_t restricted_height` (`0, 0` = no size cap) in `src/ffi/cpp/sipi_ffi.h`, mirrored as `u32` in `src/server/rust/src/ffi.rs`
- [x] Rename the `restricted_size` offset guards (`static_assert` in `sipi_ffi.h`, `offset_of!` in `ffi.rs`) to `restricted_width` at 96 and add `restricted_height` at 100; `sizeof(SipiServeRequest)` and every later offset are unchanged
- [x] In `serve_image.cpp`, take `(RW, RH)` from the request instead of parsing `restricted_size`; a half-set pair (exactly one of them 0) is a 500; `RW > img_w` or `RH > img_h` (file changed between the shell probe and the engine probe) is a 500; remove the dims-based S2-09 check (the shell refuses before the seam); keep the `f`-cap fed from the passed dims (Phase 6 removes it); new error paths log no native dims at warn level or in Sentry messages
- [x] Update `make_request` in `src/ffi/cpp/serve_image_test.cpp` and every other `restricted_size` user; `grep -rn restricted_size src test` returns no matches
- [x] Add `//src/iiifparser/rust:iiif_parser_test` cases for `clamp_dims_to_size` with non-dividing sizes (2368×1792 with `!128,128`; a `pct:` giving a non-integer result) and an image smaller than the box (100×80 with `!128,128` → 100×80)
- [x] Add Rust unit tests of the helper: an unparsable size yields the 500 outcome; `!128,128` on a 2000×10 image is refused (1 px axis); `clamp_dims_to_size` never returns 0, including for native 0×0; `max`, `pct:100`, `pct:150`, `^pct:200` and `red:0` without a watermark are refused; the same with a watermark are not; `^!128,128` on 512×512 returns 128×128; `!128,128` on a 100×80 image and `5000,` on a 2000 px image return native dims and are not refused; `!128,128` on 2368×1792 returns the clamped dims
- [x] Add a preflight prefix `test_restrict_pct100` returning `{type='restrict', size='pct:100'}` next to `test_restrict_max` in `test/_test_data/config/sipi.init-knora.lua`
- [x] Add e2e tests that `size=max` and `size=pct:100` restrictions without a watermark answer 403 on the image and on `info.json`, and 404 on `knora.json`; keep `restrict_size_max_is_refused` unweakened (it is now the image-route coverage of the moved S2-09 check)
- [x] Add an e2e assertion that `knora.json` width/height equal `info.json` width/height for a size-restricted image
- [x] Add a preflight prefix `test_restrict_bigbox` returning `{type='restrict', size='!1024,1024'}` in `sipi.init-knora.lua`, and an e2e test that `lena512.jp2` under it is served at 512×512 on the image route with matching `info.json` dims (an image inside the box)
- [x] Add `//src/ffi:serve_image_test` cases: a half-set restricted pair is a 500; `(RW, RH)` larger than the image is a 500
- [x] Add a seam test (in `seam_probe_test` or a new `//src/ffi` test) that `sipi_image_dims` on a nonexistent path returns an error status
- [x] Before the `sipi_image_dims` deadline change, record the median latency of 200 sequential unrestricted `info.json` requests against a local `just run` server; after the change, repeat; if the median rises by more than 2 ms, keep the probe without the deadline wrapper and say so in the commit body (H7 then tracks the unbounded probe)
- [x] Run `bazel test //src/ffi/... //src/iiifparser/rust:iiif_parser_test --test_output=streamed`; all pass (covers `seam_probe_test` and the layout asserts)
- [x] Run `just bazel-test-e2e`; it passes
- [ ] Run `bazel test --config=asan --config=ubsan --test_tag_filters=-no-sanitizer --test_output=errors //src/ffi/... //test/e2e:server //test/e2e:security //test/e2e:dsp_api_closure`; it passes
- [x] Run `just bazel-rustfmt-check`; it passes
- [x] Run `just bazel-clippy-check`; it passes
- [x] Update the `restricted_dims` entry in `ARCH-MAP.md` if the helper changes its name or role
- [x] Commit `fix(server): decide restricted-view dimensions once in the shell` referencing DEV-7485; the body states that `info.json` (403) and `knora.json` (404) now refuse no-op restrictions as the image (403) does, that images inside the restriction box are served, and that each `info.json`/`knora.json` and sized-restrict image request now pays one shape probe on a deadline thread
- [x] Run `just commit-lint`; it passes
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 6: A size-restricted image is served as its own image (DEV-7456, DEV-7485 part 2, DEV-6118)

Work order (one commit at the end): engine change plus its `serve_image_test` cases until that target is green, then `info.rs`, then the e2e fixture and tests, then docs and the ADR, then the gates.

- [x] In `build_image_response` (`serve_image.cpp`), for `restricted_width > 0`, before the cache lookup and the HEAD return: resolve the region against `(RW, RH)` with `crop_coords` and the client size against the region extent; an output wider than RW or taller than RH, checked on the pre-rotation size, is a 400
- [x] Replace the decode for that case: `SipiImage::read` with a FULL region and a `PIXELS_XY` `RW,RH` size constructed with `upscaling = false`, then `processing::crop`, then scale only when the target differs from the crop extent (dispatching on `eng.scaling_quality` as the codecs do); rotation, quality/ICC, watermark and encode follow unchanged
- [x] Remove the `f`-cap block and its "two residuals accepted" comment
- [x] Add `restricted_width == 0` to the passthrough (`FileBody`) condition
- [x] Make the decode-memory estimate (`compute_decode_dims` call) for a restricted request use (FULL, `PIXELS_XY(RW, RH)`) plus the final output dims
- [x] Emit `maxWidth` = RW and `maxHeight` = RH in the restricted `info.json` (`src/server/rust/src/info.rs`), so `^max` equals `max` and nothing exceeds `full/max`; in the engine, any restricted output wider than RW or taller than RH is a 400
- [x] Append `/r<RW>x<RH>` to the cache key (`.second`) only, at the `build_canonical_url` call site in `build_image_response` (owner decision 2026-10-20; `build_canonical_url` keeps its parameters), and build the canonical URL from `(RW, RH)` and the restricted-space region/size
- [x] Add a `serve_image_test` helper that drains a `StreamBody` producer and decodes the image, so tests can assert output dims and pixels
- [x] Add `//src/ffi:serve_image_test` cases on a non-dividing restricted image: `full/max` returns exactly `(RW, RH)`; a restricted request for `full/max` in the source format returns a `StreamBody` at `(RW, RH)`, never a `FileBody`
- [x] Add `//src/ffi:serve_image_test` region cases: a region in `(RW, RH)` coordinates returns the matching crop of `full/max`; `pct:0,0,50,50` returns the top-left quarter; a partially-outside region is clamped; a region starting beyond `(RW, RH)` is a 400
- [x] Add `//src/ffi:serve_image_test` size cases: `max` and `^max` return the region extent; `!w,h` fits the box; `^pct:200` on a quarter region returns twice its extent; `^pct:200` on `full` is a 400 (exceeds `(RW, RH)`); `^w,`, `^,h`, `^w,h` and `^!w,h` exceeding RW or RH are each a 400; `pct:200` without `^` is a 400; a 32×1 edge tile at size `max` succeeds; `full/max/90` on a non-square restricted image returns the swapped dims (the bound is pre-rotation)
- [x] Add `//src/ffi:serve_image_test` cases: rotation `90` swaps the restricted region dims; size plus watermark returns the restricted dims with the watermark applied; watermark-only restrict keeps native dims and native region coordinates
- [x] Add a `//src/ffi:serve_image_test` cache-key case through a real `SipiCache` in the `EngineContext` (cache dir under a unique `sipi::test::tmp_dir()` subdir, entry count read after both requests complete): a restricted and an unrestricted request with identical canonical coordinates create two cache entries, and restrictions 96×128 and 128×96 on the same file create two entries
- [x] Add a `//src/ffi:serve_image_test` case that a restriction with a size and a watermark never reaches the passthrough
- [x] Add a Rust unit test in `info.rs` and an e2e assertion that the restricted `info.json` carries `maxWidth` = RW, `maxHeight` = RH and a single `sizes` entry `(RW, RH)`, and that an unrestricted `info.json` carries no `maxWidth`/`maxHeight`
- [x] Add the e2e fixture `test/_test_data/dsp-api/images/0801/pe/rm/perm1odd.jp2` by copying `test/_test_data/images/unit/_HasCommentBlock.jp2` (373×496; `!128,128` gives 96.26 × 128, which `clamp_dims_to_size` rounds to `(RW, RH)` = (96, 128), while the old `ceilf` path gave 97), and add a `perm1odd` branch to the mock DSP-API in `test/e2e/tests/dsp_api_closure.rs` answering `{"permissionCode":1,"restrictedViewSettings":{"size":"!128,128"}}`
- [x] Add an e2e seam test in `dsp_api_closure.rs` on `perm1odd.jp2`: read width/height from `info.json`, request 32 px and 64 px tile grids over it (edge tiles included) at size `max` as PNG, decode them with the `image` crate, stitch, and assert raw-pixel equality with the decoded `full/max/0/default.png` (never compare PNG bytes)
- [x] Add an e2e test that a left-half region in `info.json` coordinates returns the left half of `full/max` (DEV-7485 acceptance)
- [x] Add an e2e test that `info.json` width/height equal the `full/max` output dims for `perm1odd.jp2`
- [x] Add an e2e test that the canonical `Link` of a restricted request carries restricted-space coordinates and never the native width/height, on GET, on HEAD and on a 400: `full/max` canonicalises to `…/full/max/0/default.jpg` with no `^`, and a region request canonicalises to restricted-space `x,y,w,h` and `w,h`
- [x] Update `permission_code_1_restricts_the_size_of_a_region_request` (`dsp_api_closure.rs`) to request the region in restricted coordinates and assert it is a crop of `full/max`, never larger than `(RW, RH)`
- [x] Add an e2e test in `test/e2e/tests/iiif_compliance.rs` that `full/max` and `full/^max` on an unrestricted JP2 and an unrestricted TIFF return the native dimensions (DEV-6118 regression; Allow guard)
- [x] Rewrite the Cache key, Watermark and restrict entries in `UBIQUITOUS_LANGUAGE.md` and the restrict description in `docs/src/lua/index.md` to the new semantics (including `maxWidth`/`maxHeight` on restricted `info.json`)
- [x] List the new restricted-view tests in the restricted-reduction rows of `docs/src/development/testing-strategy.md`
- [x] Write `docs/adr/0028-restricted-view-coordinate-space.md`: `(RW, RH)` as the restricted image's coordinate space, crop-from-one-rendering, the shell as the single source of `(RW, RH)` and of the no-op refusal (refused kinds vs box restrictions), the cache-key marker, `maxWidth`/`maxHeight` = `(RW, RH)` bounding every `^` request (checked pre-rotation), the 1 px-axis refusal, the Tile-lane admission residual, the deliberate serving of images inside an over-large box (with its warning log), the passthrough guard, the superseded S2-04 per-region sampling cap and the two decisions it keeps, and option 2 rejected
- [x] Run `bazel test //src/ffi:serve_image_test --test_output=streamed`; it passes
- [x] Run `just bazel-test-unit`; it passes
- [x] Run `just bazel-test-approval`; it passes
- [x] Run `just bazel-test-e2e`; it passes
- [ ] Run `bazel test --config=asan --config=ubsan --test_tag_filters=-no-sanitizer --test_output=errors //src/ffi/... //test/e2e:dsp_api_closure //test/e2e:server //test/e2e:iiif_compliance`; it passes
- [x] Run `just bazel-rustfmt-check`; it passes
- [x] Run `just bazel-clippy-check`; it passes
- [x] Commit `fix(server)!: serve a size-restricted image as its own image` referencing DEV-7456, DEV-7485 and DEV-6118, with a `BREAKING CHANGE:` footer stating that region and size on a size-restricted image resolve in the restricted image's coordinate space, and the DEV-6118 verdict in the body
- [x] Run `just commit-lint`; it passes
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 7: Editor tooling gets a compile database (session request 2026-10-20)

clangd and clang-tidy have no `compile_commands.json` since the move from CMake to Bazel. Every
C++ file therefore shows bogus "file not found" diagnostics in the editor, and the clang-tidy
instructions in `docs/src/development/cpp-style-guide.md` (the `compile_commands.json`
section) still describe CMake.

**Extractor (decided after research, 2026-10-20):** the helly25 fork of
hedron-compile-commands.
- Upstream `hedronvision/bazel-compile-commands-extractor` is broken on Bazel 9: it calls
  `native.py_binary` (upstream issue #279, open) and has seen no maintenance since mid-2024.
- The fork keeps the module name `hedron_compile_commands`, supports Bazel 9 (it loads
  `py_binary`/`cc_binary` from `rules_python`/`rules_cc`) and is actively maintained. It is not
  in the BCR, so it is installed by `git_override`, pinned to
  `078ba7675c05bea2666c92d9a8abe200d8547655` (main, 2026-09-16, verified).
- `kiron1/bazel-compile-commands`, a standalone binary, is the fallback. A hand-written
  `aquery` script is the last resort.

- [x] Add to `MODULE.bazel`: `bazel_dep(name = "hedron_compile_commands", dev_dependency = True)` with `git_override(module_name = "hedron_compile_commands", remote = "https://github.com/helly25/bazel-compile-commands-extractor.git", commit = "078ba7675c05bea2666c92d9a8abe200d8547655")`, with a one-line comment on why the fork (upstream broken on Bazel 9) and not the BCR, plus a `single_version_override` holding `rules_python` at 1.8.0 (the fork declares 2.0.1, which MVS would apply to the whole graph); update `MODULE.bazel.lock`
- [x] Add a `refresh_compile_commands` target in its own package `tools/compile_commands/BUILD.bazel` (only that package loads the dev-only fork; the root package is loaded by the e2e tests) (load from `@hedron_compile_commands//:refresh_compile_commands.bzl`) covering `//src/cli:sipi`, `//src:sipi_lib` and every `cc_test` under `//src/...` and `//test/...` (`//test/unit/...` has no targets of its own), with `exclude_external_sources = True`; never include the Linux-only `//src:sipi_image`, and run in fastbuild without `--config=asan/ubsan/fuzz`
- [x] Add a `just compile-commands` recipe running `bazel run //:refresh_compile_commands`
- [x] Add `compile_commands.json`, `.cache/` and the `external` symlink to `.gitignore`
- [x] Add a repo-root `.clangd` that removes flags clangd cannot parse (at least `-fno-canonical-system-headers` and `-fmodule-map-file=*`), so the hermetic clang's command lines are usable
- [x] Run `just compile-commands`; `compile_commands.json` has entries for `src/format_handlers/cpp/SipiIOJ2k.cpp` and `src/ffi/cpp/serve_image.cpp`, and each entry's driver (first argument) is an absolute path to the hermetic clang that exists (the fork emits it relative, so the recipe passes `--bcce-compiler=<absolute path>` resolved through a `//bazel:clang++` alias; `--bcce-*` are runtime args, not macro args)
- [x] Run `clangd --check=src/format_handlers/cpp/SipiIOJ2k.cpp --query-driver='**'` with the nix-shell clangd; it resolves `<algorithm>`, `<expected>`, the Kakadu headers and generated headers with no `file not found` error (note the clangd version and any version-skew warning against LLVM 22.1.7 in the commit body)
- [x] Run `clang-tidy -p . --checks='-*,bugprone-use-after-move' src/format_handlers/cpp/SipiIOJpeg.cpp src/ffi/cpp/serve_image.cpp` (clang-tidy rejects `--checks='-*'` alone); it reports no `file not found` errors
- [x] If the fork cannot produce a working database with the hermetic toolchain after `--bcce-prefer-target-config` and `--bcce-compiler`, switch to `kiron1/bazel-compile-commands` (or, last, a ~40-line `bazel aquery --output=jsonproto` generator under `tools/`), and state in the commit body what failed and which path was taken
- [x] Replace the CMake-based `compile_commands.json` instructions in `docs/src/development/cpp-style-guide.md` with the `just compile-commands` workflow, and add a short clangd section to `docs/src/development/developing.md` (run `just compile-commands`; start clangd with `--query-driver='**'`; for Zed, set it under `lsp.clangd.binary.arguments`)
- [x] Run `just bazel-build`; it passes (CLAUDE.md requires it on macOS for any `MODULE.bazel`/`BUILD.bazel`/`justfile` change)
- [x] Run `just bazel-coverage`; it passes (owner decision 2026-10-20: it fails identically before this phase on macOS (`_cc_coverage.profdata` missing) and Phase 7 changes no compile/link/toolchain action; pre-existing breakage tracked in DEV-7588)
- [x] Commit `build(bazel): generate compile_commands.json for clangd and clang-tidy`
- [x] Run `just commit-lint`; it passes
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

## Human Actions

"After ship" means after this plan's PR is merged and release-please has cut 10.0.0.

Vitrinli never deploys on its own:
- dsp-api builds `daschswiss/knora-sipi` on an upstream `daschswiss/sipi` image pinned per
  architecture by digest in `dsp-api/MODULE.bazel`.
- ops-deploy deploys that image (api tag) and dsp-app (app tag) together, from one
  `RELEASE.json` entry.

So the release can be cut at any time. Production changes only with a DSP release that carries
both the dsp-api digest bump (H1) and the matching dsp-app change (H2). dsp-app#3509 (DEV-7457,
the single-image workaround) is not needed.

| Id | Action | Who | When | Why not the agent |
|----|--------|-----|------|-------------------|
| H1 | Open the dsp-api PR that bumps the `daschswiss/sipi` digests in `MODULE.bazel` to 10.0.0 (procedure: "Docker Image Versions" in dsp-api `docs/05-internals/development/third-party.md`) | Maintainer | after ship | Change in another repo, owned by its maintainers |
| H2 | In dsp-app, show size-restricted (RV) images by passing Sipi's `info.json` to OpenSeadragon as the tile source, with the normal credentialed `HttpClient`, instead of building tiles from the API's native `dimX`/`dimY`; close dsp-app#3509 unmerged; ship it in the same DSP release as H1. Native-coordinate tiles break against 10.0.0, and `info.json` tiling is garbled before it, so the two must deploy together, as DSP releases always do | dsp-app owner | after ship | Change in another repo; the DSP release composition is the release owner's call |
| H3 | In ops-deploy `roles/dsp-deploy/defaults/main.yml`, update the `DSP_IIIF_ALLOWED_ORIGINS` comment ("Anonymous info.json keeps sending `*` regardless") to say the allowlist now covers credentialed `info.json`; confirm the DSP session cookie is `SameSite=None; Secure` so credentialed cross-site reads work | Maintainer | after ship | Change in the operator-owned deploy repo |
| H4 | Close DEV-6361 and DEV-6118 per the verdicts in the Phase 2 and Phase 6 commit bodies, and reply to the DEV-6118 reporter | Maintainer | after ship | Closing an externally reported issue is the maintainer's call |
| H5 | Close DEV-5938 as obsolete (no Dockerfile; Bazel + RBE cache dependency builds) | Maintainer | after ship | Backlog triage decision |
| H6 | On stage, anonymously, open `0854/4LcAqHes4tQ-M1H8a6ucd6F.jpx` and `0802/1Tra2ZGBoyl-0LJXp83T4gv.jpx` in OpenSeadragon fed their `info.json`, and confirm they render whole and without seams | Maintainer | after ship | Needs the deployed stage environment and a browser |
| H7 | File follow-up issues: (a) abandoned decode-deadline threads never stop, so any codec spin leaks a core; (b) the 60 s request timeout fires before the 120 s decode deadline; (c) `info.json`/`knora.json` early-return errors carry no CORS headers, which browsers surface as opaque network errors, and the image route echoes the Origin without `Vary: Origin` in reflect mode; (d) `sipi_image_file_dims` (Lua `SipiImage.dims`) still runs `read_shape` without a deadline; (e) the cache key carries only a watermark flag, not the watermark identity, and the code caches watermarked output despite its comment; (f) reflect mode echoes the literal `null` Origin with credentials; (g) image responses carry no `private`/`Vary: Cookie, Authorization` when a hook is configured; (h) the 401 `info.json` auth-service block copies every unlisted preflight key into the body; (i) admission picks the lane before preflight, so restricted region requests (full decode at `(RW, RH)`) occupy the Tile lane | Maintainer | after ship | Backlog triage decision |
| H7b | File a follow-up issue: replace chroma sample replication in `SipiIOJ2k` with co-sited linear upsampling, validated against OpenJPEG output (owner decision 2026-10-20) | Maintainer | after ship | Backlog triage decision |
| H8 | In the first 24 h after the deploy, check in the SIPI Grafana dashboard (dsp-api repo) the 4xx rate on `info.json`/`knora.json` (no-op restrictions now refuse), `sipi_wedged_threads`, the admission lane mix, and Sentry for `kUnsupportedFormat` reports; adjust panels or alerts that misfire | Maintainer | after ship | Needs production observability access |
| H9 | On stage: confirm `file3.jp2`-style subsampled JP2s render, a credentialed `info.json` fetch from the dsp-app origin succeeds, and then move DEV-6211, DEV-7431, DEV-7432, DEV-7485, DEV-7456 and DEV-7520 to Done | Maintainer | after ship | Needs the deployed stage environment and the maintainer's closing call |
| H10 | When the release with Phase 6 deploys, purge the SIPI disk cache directory (old entries were keyed before the canonical-size and `/r<RW>x<RH>` key changes) | Maintainer / operator | after ship | Operational step on deployed infrastructure |

## Acceptance Criteria

- [ ] Overwriting an existing file with a smaller JPEG yields a file the size of a fresh write (DEV-6211)
- [ ] `addKeyVal` on an existing tag replaces the value, with one entry per key (DEV-7431)
- [ ] FlashEnergy round-trips through TIFF write/read (DEV-6361)
- [ ] A credentialed `info.json` request (including 401 and preflight) with an allowed Origin gets the echoed Origin and `Access-Control-Allow-Credentials: true`; one without an Origin still gets `*` (DEV-7520)
- [ ] `file3.jp2` decodes to 480×640 within the reference tolerance, through the CLI and the server; unsupported subsampling shapes fail fast (DEV-7432)
- [ ] `info.json`, `knora.json` and `full/max` agree on the restricted dimensions (DEV-7485)
- [ ] For a size-restricted image, tiles requested at size `max` over the `info.json` dims stitch to exactly `full/max` (DEV-7456)
- [ ] For a size-restricted image, a region in `info.json` coordinates returns the matching part of `full/max` (DEV-7485)
- [ ] No region/size combination on a restricted image returns more than `(RW, RH)`; restricted `info.json` advertises `maxWidth`/`maxHeight` = `(RW, RH)`; the cache never mixes restricted and unrestricted entries; a restricted request never takes the passthrough
- [ ] Allow, Stream and watermark-only Restrict behave as before; `full/max` on unrestricted images returns native dims (DEV-6118)
- [ ] No-op restriction kinds (`max`, `pct:` ≥ 100, `red:0`) without a watermark answer 403 on the image and on `info.json`, and 404 on `knora.json`; an image inside the restriction box is served at native size
- [ ] Restricted `info.json` advertises `maxWidth`/`maxHeight` = `(RW, RH)`; a restricted axis below 2 px is refused
- [ ] An unsupported JP2 subsampling shape fails within 5 s with `kUnsupportedFormat`; tiny regions at odd origins on subsampled JP2s decode
- [ ] `just bazel-test-unit`, `just bazel-test-approval`, `just bazel-test-e2e`, the scoped sanitizer runs (`bazel test --config=asan --config=ubsan`), `just bazel-rustfmt-check`, `just bazel-clippy-check` and `just commit-lint` pass on the final branch; the Phase 4 `just bench-compare` shows no regression

## Dependencies & Risks

- **Phase 6 is a breaking contract change.** The `!` produces major release 10.0.0.
  - Third-party IIIF clients that use native coordinates on size-restricted images change
    behaviour; the release notes say so.
  - For DSP, dsp-app must switch RV images to `info.json` tiling in the same DSP release as the
    digest bump (H1, H2).
- **One PR, one release (maintainer decision).** Phases 1–5 (CORS, the JP2 core-pinning fix,
  EXIF, JPEG write) reach production together with Phase 6, through the same dsp-api bump.
- **Phase 4 changes the JP2 decode path.** Mitigation:
  - The non-subsampled path stays unchanged, checked by the `file2` reference test and the
    `file1`/`file2`/`file4` `assert_jp2_decode` e2e cases.
  - The subsampled path is pinned by the conformance reference.
  - The guard bounds unsupported shapes.
- **Phase 6 decode cost** for non-pyramidal TIFFs is accepted (see Technical Considerations).
- **Disk cache across the upgrade.** No purge is needed:
  - Sized-restrict entries get the new `/r<RW>x<RH>` key suffix, so old restricted entries are
    never hit by restricted requests.
  - An old restricted entry could only be hit by an unrestricted request for the same reduced
    canonical URL, which returns the same pixels.
  - Old entries age out under LRU.
  - A rollback from 10.0.0 has the same property in reverse.
- **FFI field change** (Phase 5): two `uint32_t` replace one pointer, so the layout size is
  unchanged. The `static_assert`s and the `offset_of!` test catch a missed mirror at build and
  test time.
- **Phase 6 passthrough and memory budget.** Dropping the `f`-cap would otherwise let a
  restricted `full/max` in the source format serve the native file, and let small restricted
  regions decode whole images outside the full-lane budget. Both are explicit Phase 6
  deliverables with tests.

## Success Metrics

- **DEV-7456 seams.**
  - Baseline from the issue (≈2368 px image, `!128,128`, 1024 px tiles): mean lossless
    difference of 3.0 in tile interiors and 10.6 at borders.
  - Target: zero difference on the e2e fixture (373×496, 32 and 64 px grids, tiles at size
    `max`).
  - The 2368 px case is confirmed manually under H6.
- **`file3.jp2` request latency.**
  - Baseline: 408 after 60 s, with a core pinned for good.
  - Target: 200 well within the e2e's 30 s timeout. The `decode_full_subsampled_jp2` median is
    recorded in the Phase 4 commit body; expected under 1 s, not asserted.
- **`info.json` latency.** The Phase 5 before/after median on an unrestricted image rises by
  no more than 2 ms, or the deadline wrapper is dropped.
- **Phase 4 `just bench-compare`.** No decode regression beyond noise on the pre-existing
  cases.

## References

- **Restricted view:**
  - `src/ffi/cpp/serve_image.cpp` (`build_image_response`, `build_canonical_url`)
  - `src/ffi/cpp/sipi_ffi.h` (`SipiServeRequest`)
  - `src/server/rust/src/ffi.rs`
  - `src/server/rust/src/routes.rs` (`restricted_dims`, `serve_info_json`, `serve_knora_json`)
  - `src/server/rust/src/info.rs`
  - `src/iiifparser/rust/parse.rs` (`clamp_dims_to_size`)
  - `src/iiifparser/cpp/value_objects/SipiSize.cpp`
  - `src/iiifparser/cpp/value_objects/SipiRegion.cpp`
- **CORS:**
  - `src/server/rust/src/routes.rs` (`cors_allow`, `cors_preflight_response`, `json_response`)
  - `test/e2e/tests/iiif_compliance.rs`
  - `test/e2e/tests/security.rs`
- **EXIF:**
  - `src/metadata/cpp/exif.h`
  - `src/format_handlers/cpp/SipiIOTiff.cpp` (EXIF tag table, `readExif`, `writeExif`)
- **JPEG write:** `src/format_handlers/cpp/SipiIOJpeg.cpp` (`SipiIOJpeg::write`)
- **Deadline:**
  - `src/ffi/cpp/decode_guard.h`
  - `src/ffi/cpp/engine_context.h`
  - `src/cli/cpp/commands/decode_deadline.h`
- **Restricted view, further:**
  - `src/ffi/cpp/sipi_ffi.cpp` (`sipi_image_dims`)
  - `src/throttling/rust` (admission classification)
  - `test/e2e/tests/dsp_api_closure.rs`
  - `UBIQUITOUS_LANGUAGE.md`
  - `docs/src/lua/index.md`
- **JP2:**
  - `src/format_handlers/cpp/SipiIOJ2k.cpp`
  - `src/format_handlers/cpp/decode_benchmark.cpp`
  - `test/e2e/tests/cli_conversions.rs`
  - `docs/src/development/benchmarking.md`
  - `docs/src/development/fuzzing.md`
- **ADRs:**
  - `docs/adr/0021-iiifparser-polyglot-colocation.md`
  - `docs/adr/0024-value-based-image-errors.md`
  - `docs/adr/0022-two-lane-admission-control.md`
  - `docs/adr/0025-cache-index-header.md`
  - `docs/adr/0026-engine-seam.md`
  - `docs/adr/0027-stream-permission-type.md`
- **Learnings:**
  - `docs/learnings/testing/timeout-e2e-must-be-bounded.md`
  - `docs/learnings/architecture/ffi-seam-rust-mirrors-and-signature-drift.md`
  - `docs/learnings/architecture/kakadu-teardown-order-and-pod-templates-accepting-std-string.md`
  - `docs/learnings/testing/seam-mitigation-does-not-green-a-codec-fuzz-leg.md`
- **Linear:** DEV-6211, DEV-7431, DEV-6361, DEV-7520, DEV-7432, DEV-7485, DEV-7456, DEV-6118
