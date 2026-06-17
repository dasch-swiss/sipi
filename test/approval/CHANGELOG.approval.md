# Approval Goldens — Re-Approval Audit Log

This file records every intentional drift of a committed `*.approved.*`
golden file under `test/approval/approval_tests/`. Most goldens are
byte-for-byte stable — a diff is a regression. When a real upstream
change makes a diff intentional (e.g., a libpng version bump shifting
the default zlib compression level by one byte), the procedure is:

1. Verify the new output is **visually equivalent** and IIIF-compliant.
   Attach diff images / pixel-stat comparison to the PR description.
2. Get a maintainer sign-off on the diff in the PR review.
3. Re-approve: rename the new `*.received.*` to `*.approved.*` and
   commit alongside the change that caused it (same PR — never split).
4. Add an entry below.

Format: one row per re-approval, newest at the top.

| Date | PR | Golden(s) | Cause | Visual diff verified by |
|------|-----|-----------|-------|-------------------------|
| 2026-06-17 | sipi (DEV-6563) | `ImageEncodeBaseline.{JpegFullToJpegDownscaled, J2kRegionToJpeg, CmykTiffToJpeg, CielabTiffToJpeg, JpegRotatedToJpeg}.jpg` (5 JPEG-encode goldens) | **JPEG encoder switched from progressive to baseline sequential.** libjpeg-turbo's x86 SSE2 progressive Huffman encoder (`jcphuff`) throws "Missing Huffman code table entry" for SIPI's scan script — its statistics-gather pass and its encode pass disagree on the emitted symbol stream, so a written symbol was never assigned a code. Architecture-divergent (the Arm NEON path is unaffected, which is why arm CI was green and amd64 red), and **not** fixable via `optimize_coding`, since the disagreement is *between* the two passes `optimize_coding` runs. Baseline uses the canonical `jchuff` encoder, bit-exact across SIMD implementations. Same quantized DCT coefficients in a different scan order → different compressed bytes, identical decoded pixels. (Supersedes the byte values from the libjpeg-turbo-swap re-baseline below for these 5 `.jpg` goldens; the 3 JPEG-*decode* goldens there — `.tif`/`.png` — are untouched, since this switch only changes the encode path.) | `sipi compare` (Phase-0 absolute \|Δ\|) on every prior progressive golden vs the new baseline output: **"Files identical!"** for all 5 — pixel-identical, only the entropy/scan layout differs. Full local suite green on darwin-arm64 (unit 17/17, approval, e2e); cross-arch byte-exactness verified on amd64/arm64 via CI. Maintainer sign-off: I. Subotic, 2026-06-17. |
| 2026-06-17 | sipi (DEV-6563) | `ImageEncodeBaseline.{JpegToTiffDownscaled.tif, JpegRotatedDownscaledTiff.tif, JpegFullToJpegDownscaled.jpg, JpegFullToPng.png, J2kRegionToJpeg.jpg, CmykTiffToJpeg.jpg, CielabTiffToJpeg.jpg, JpegRotatedToJpeg.jpg}` (8 goldens) | **libjpeg-turbo 3.1.3 replaces IJG libjpeg 9f** (foreign_cc elimination, Phase 4/6). Turbo's SIMD IDCT drifts ~a few LSB from the IJG reference on decode, and its Huffman/SIMD encoder emits different compressed bytes for the same pixels — so every golden whose pipeline decodes OR encodes JPEG shifts. Pure-TIFF round-trips (`TiffRegionRoundTrip`, `CmykTiffDownscaled`, `TiffToPng`) stay byte-identical under the native libtiff 4.7.1, confirming the drift is libjpeg-only. **Tolerance gate, not byte-exact** (plan §"libjpeg-turbo vs IJG 9f"): benign encoder-precision difference (small avg) = re-baseline; large/systematic delta would be a regression. | `sipi compare` (Phase-0-corrected true \|Δ\|) on decoded old-golden-vs-new-output: **decode-only drift** (lossless TIFF/PNG outputs) max \|Δ\|=6, avg 0.30; **lossy re-encode** (JPEG outputs) max 21–44, avg 1.4–3.4; `J2kRegionToJpeg` decodes to **identical pixels** (only the compressed bytes differ). Avg ≤3.4 everywhere (a colorspace/channel bug would push avg into the dozens) and the lcms2 colour path is byte-identical (Phase 5), so the maxima are inherent turbo-vs-IJG precision, not a defect. Maintainer sign-off: I. Subotic, 2026-06-17. |
| 2026-06-02 | sipi#661 | `ImageEncodeBaseline.J2kRegionToJp2.approved.jp2` | Kakadu SDK bump v8.5 (`v8_5-01382N`) → v8.7 (`v8_7-01727L`). The v8.7 encoder emits a single differing byte in the JPEG2000 codestream (offset 17299 of 31804, inside the `jp2c` entropy-coded body); the ICC profile and every JP2 box are byte-identical. Benign encoder drift, not a regression — re-encoding the same `gray_with_icc.jp2` region produces an equivalent codestream that decodes to the same image. | `cmp -l` confirmed exactly 1 differing byte, located in the codestream body (not in any header/box/ICC profile); `sipi compare` reports the v8.5 golden and v8.7 output decode to identical pixels ("Files identical!"); both JP2s decode to byte-identical PNGs and `sipi query` shows identical 256×256 dimensions, ICC profile, and colorspace. |
| **PENDING (DEV-6537)** | sipi#639 | `ImageEncodeBaseline.*.approved.{tif,jpg,png,jp2}` (every golden that round-trips through a writer; exact list enumerated on regen) | DEV-6537 ADR-0004 + ADR-0005 joint implementation. Three subtractive / additive deltas converge on one consolidated re-approval: (a) **JPEG / PNG / plain-TIFF writers stop emitting `Essentials`** (Phase 6.5-6.7) — Access File outputs no longer carry a `SIPI:` legacy carrier. (b) **Pyramidal-TIFF / JP2 goldens whose authoring path migrates to `convert service-file`** gain the new protobuf carrier (TIFF tag 65112 / JP2 SIPI UUID box at slot 4) and lose the legacy carrier (TIFF tag 65111 / JP2 codestream-comment `SIPI:` block); see ADR-0005 for the box-layout invariant. (c) **CLI flag forms `--convert` / `--query` / `--compare` removed** (Phase 11.4, no deprecation cycle per maintainer decision 2026-05-14); any approval driver that shells out to `sipi` migrates to the verb-noun subcommand form. **Regen procedure:** linux-x86_64 CI runner; `SOURCE_DATE_EPOCH=946684800` + `SIPI_WORKSPACE_ROOT="."` injected by `test/approval/BUILD.bazel` (ADR-0002). Pixel content bit-identical; only header bytes change. | Per-golden `cmp -l` confining the diff to the new-carrier byte range; `jpylyzer` (Phase 15.11 CI step) on regenerated JP2 outputs reports the SIPI UUID box as an informational `Unknown UUID` and no validation failures. |
| 2026-05-02 | sipi#602 | `ImageEncodeBaseline.JpegToTiffDownscaled.approved.tif`, `ImageEncodeBaseline.JpegRotatedDownscaledTiff.approved.tif` | Expanded `exiftag_list[]` added 9 EXIF 2.3 / 2.31 tags to the round-trip set. The two inputs above (`MaoriFigure.jpg`, `MaoriFigureWatermark.jpg`) carry `Exif.Photo.OffsetTime`, `Exif.Photo.OffsetTimeOriginal`, and `Exif.Image.DateTime` / `Exif.Photo.DateTimeOriginal` — previously stripped by the writer, now preserved. Diff is +40 bytes per file (the IFD entries for the four newly round-tripped tags); pixel content unchanged. `exiftool` confirmed all new fields originate verbatim from the source JPEG. | n/a — metadata-only delta; `exiftool -j` showed identical pixel-related fields and the four new EXIF entries match the source JPEG |
| 2026-04-29 | sipi#587 | `ImageEncodeBaseline.TiffRegionRoundTrip.approved.tif`, `ImageEncodeBaseline.CmykTiffDownscaled.approved.tif` | First introduction of `SOURCE_DATE_EPOCH` rewrite in `Icc::iccBytes()`. Both inputs (`lena512.tif` Generic-Gray-Gamma-2.2 ICC, `cmyk.tif` SWOP CMYK ICC) carry an embedded ICC profile that round-trips through `cmsSaveProfileToMem`, so under env-var injection their date bytes (24-35) shift to 2000-01-01T00:00:00Z. `cmp -l` confirmed the diff is exactly the 12-byte `dateTimeNumber` field per profile — no pixel-level drift. | n/a — byte-level diff confirmed surgical via `cmp -l`; pixel content unchanged |

## ICC creation-date determinism (`SOURCE_DATE_EPOCH`)

`ImageEncodeBaseline.*` goldens for JPEG, PNG, and JP2 outputs — and
any TIFF that carries through an embedded ICC profile — are byte-stable
**only when the `SOURCE_DATE_EPOCH` environment variable is set**.
`Icc::iccBytes()` reads the variable once (cached thread-safely)
and overwrites bytes 24-35 of every emitted profile (the ICC creation
date) plus zeros bytes 84-99 (the Profile ID). Without the env var,
lcms2's `cmsCreateProfilePlaceholder` stamps the wall-clock time and
the seconds field drifts by one byte across consecutive runs.

CMake injects `SOURCE_DATE_EPOCH=946684800` (2000-01-01T00:00:00Z UTC,
an arbitrary SIPI-test convention) for `sipi.approvaltests` via
`set_tests_properties(... ENVIRONMENT ...)` in
`test/approval/CMakeLists.txt`. So under `ctest -L approval` everything
is byte-deterministic.

A maintainer running `sipi.approvaltests` directly outside ctest must
export the same value first:

```bash
SOURCE_DATE_EPOCH=946684800 ./sipi.approvaltests \
  --gtest_filter='ImageEncodeBaseline.*'
```

Without it, expect `.received.*` files for any test whose pipeline
emits an ICC profile (`JpegFullToJpegDownscaled`, `JpegFullToPng`,
`J2kRegionToTiff`, `J2kRegionToJpeg`, `J2kRegionToJp2`,
`CmykTiffDownscaled`, `CmykTiffToJpeg`, `CielabTiffToJpeg`,
`JpegRotatedToJpeg`, `PngRoundTrip`, `TiffRegionRoundTrip`,
`TiffToPng`). The `.received.*` will differ from the committed golden
in exactly twelve bytes per embedded ICC profile — the dateTimeNumber
field. This is expected, not a regression.

Production runtime is unaffected: SIPI never sets `SOURCE_DATE_EPOCH`,
so deployed binaries continue to embed wall-clock-stamped ICC headers
verbatim from lcms2. See `docs/adr/0002-icc-profile-determinism-test-
only.md`.
