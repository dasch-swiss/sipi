# Malformed image test fixtures

Crafted malformed image fixtures for the codec memory-safety regression
suite: files whose headers claim dimensions, channel counts, or internal
offsets that a well-formed encoder would never produce, used to exercise the
decode-path input-validation and overflow-guard code added alongside them.

Each fixture's defect is documented in the table below as fixtures land.

| File | Defect |
|---|---|
| `tiff_oversized_dimensions.tif` | `IMAGEWIDTH` claims 1048576px (> `kMaxDecodeDim`), no pixel data written; regresses `validate_decode_dims()` rejecting the header before any buffer is sized (DEV-6063). |
| `tiff_colormap_unsupported_bps.tif` | Palette (indexed-color) TIFF with `BITSPERSAMPLE=32`, outside SipiIOTiff's supported set; regresses the bps allowlist gating the `1 << bps` colormap-length computation, run before any `ColorMap` tag is read (N6). |
| `tiff_transferfunction_grayscale.tif` | Well-formed grayscale TIFF with `WHITEPOINT` + `TRANSFERFUNCTION` set and no embedded ICC profile; regresses the corrected variadic `TIFFGetField(TIFFTAG_TRANSFERFUNCTION, ...)` call (3 `uint16_t*` out-pointers, `1 << bps` entries each) that previously corrupted the stack (N3). |
| `tiff_palette_1bit.tif` | 1-bit-per-sample palette TIFF with a 2-entry colormap; regresses the colormap-vector `resize()` (not `reserve()`) fix (DEV-6064) and the validate-before-lookup guard at the palette-to-RGB expansion step (DEV-6065) — decode must either succeed or fail closed, never crash. |
| `jpeg_xmp_truncated.jpg` | Well-formed 32x32 RGB JPEG with an APP1 XMP segment whose payload is *only* the 29-byte `http://ns.adobe.com/xap/1.0/\0` namespace header — no packet body, no `<?xpacket begin` wrapper anywhere in the segment; regresses `SipiIOJpeg::read_shape()`'s bounded XMP extraction (DEV-6066), which previously tracked its scan position from 0 instead of from the namespace offset and could walk past the end of the marker's heap buffer. |
| `jpeg_icc_short_app2.jpg` | APP2 segment whose payload is exactly the 12-byte `ICC_PROFILE\0` identifier with none of the 2 trailing sequence-number/count bytes; regresses the underflow guard in `SipiIOJpeg::read()`'s ICC length computation (`data_length - offset - 14`), done in unsigned arithmetic with an explicit length check before any `realloc`/`memcpy` (N1). |
| `jpeg_photoshop_short_app13.jpg` | APP13 segment truncated to 5 bytes (`"Photo"`, a prefix of the 14-byte `"Photoshop 3.0\0"` identifier it is compared against); regresses the `marker->data_length >= 14` guard added before the `strncmp` in both `read()` and `read_shape()` (N4). |

## Git LFS

Fixtures under this directory are Git LFS objects. A fresh worktree lands
them as ~131-byte pointer files, not the real image bytes — image tests then
fail with a cryptic error-500 / `text/plain` response rather than a decode
error. Run `git lfs pull` and check the file size (`ls -l`) before assuming a
test failure is a codec bug.

## Regenerating

The TIFF fixtures are produced by
`test/unit/fixtures/generate_malformed_images.cpp` (a `cc_binary`, built via
Bazel):

    bazel run //test/unit/fixtures:generate_malformed_images -- test/_test_data/images/malformed/

The JPEG marker-parsing fixtures (`jpeg_*.jpg`) are produced by
`test/unit/sipiimage/fixtures/generate_jpeg_fixtures.py` — its
`generate_malformed()` function writes into a `malformed/` directory
alongside the `.../images/jpeg/` output dir the script is invoked with:

    uv run test/unit/sipiimage/fixtures/generate_jpeg_fixtures.py \
        test/_test_data/images/jpeg/

The `.tif`/`.jp2`/`.jpg`/`.png` files these generators produce are committed
(via Git LFS) so CI does not need to regenerate them at test time. Each
fixture and its defect must be added to the table above in the same change
that commits the fixture.
