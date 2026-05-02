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
| 2026-05-02 | sipi#602 (DEV-6356) | `ImageEncodeBaseline.JpegToTiffDownscaled.approved.tif`, `ImageEncodeBaseline.JpegRotatedDownscaledTiff.approved.tif` | Expanded `exiftag_list[]` (DEV-6356 / DEV-6366) added 9 EXIF 2.3 / 2.31 tags to the round-trip set. The two inputs above (`MaoriFigure.jpg`, `MaoriFigureWatermark.jpg`) carry `Exif.Photo.OffsetTime`, `Exif.Photo.OffsetTimeOriginal`, and `Exif.Image.DateTime` / `Exif.Photo.DateTimeOriginal` — previously stripped by the writer, now preserved. Diff is +40 bytes per file (the IFD entries for the four newly round-tripped tags); pixel content unchanged. `exiftool` confirmed all new fields originate verbatim from the source JPEG. | n/a — metadata-only delta; `exiftool -j` showed identical pixel-related fields and the four new EXIF entries match the source JPEG |
| 2026-04-29 | sipi#587 (DEV-6333) | `ImageEncodeBaseline.TiffRegionRoundTrip.approved.tif`, `ImageEncodeBaseline.CmykTiffDownscaled.approved.tif` | First introduction of `SOURCE_DATE_EPOCH` rewrite in `SipiIcc::iccBytes()`. Both inputs (`lena512.tif` Generic-Gray-Gamma-2.2 ICC, `cmyk.tif` SWOP CMYK ICC) carry an embedded ICC profile that round-trips through `cmsSaveProfileToMem`, so under env-var injection their date bytes (24-35) shift to 2000-01-01T00:00:00Z. `cmp -l` confirmed the diff is exactly the 12-byte `dateTimeNumber` field per profile — no pixel-level drift. | n/a — byte-level diff confirmed surgical via `cmp -l`; pixel content unchanged |

## ICC creation-date determinism (`SOURCE_DATE_EPOCH`)

`ImageEncodeBaseline.*` goldens for JPEG, PNG, and JP2 outputs — and
any TIFF that carries through an embedded ICC profile — are byte-stable
**only when the `SOURCE_DATE_EPOCH` environment variable is set**.
`SipiIcc::iccBytes()` reads the variable once (cached thread-safely)
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
