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
| `jpeg_photoshop_overshoot_app13.jpg` | APP13 segment whose payload is a *valid* `"Photoshop 3.0\0"` identifier (14 bytes) followed by exactly one 7-byte 8BIM resource and nothing after: `"8BIM"` (4) + resource id `0x0404` (2) + a 1-byte Pascal name-length of `0x00` (empty, even-length name). `parse_photoshop()` sees a 7-byte block, so the caller's `data_length >= 14` + identifier `strncmp` guard passes and the parser runs. Consuming `8BIM`(4)+id(2) leaves `ptr` at offset 6 with `end` at 7; the name-length byte is 0, `slen++` (length byte) makes `slen=1`, and the odd->even padding bump makes `slen=2` — one more than the single byte remaining. Before the fix, `ptr += slen` walked one byte past `end`, so the next `4 > (size_t)(end - ptr)` computed `end - ptr == -1`, cast to `SIZE_MAX`, defeated the guard, and over-read `datalen` and beyond. Regresses the `if (slen > (size_t)(end - ptr)) break;` guard (and the sibling `datalen` guard) added before the pointer advances in `parse_photoshop()` — the adversarial review's most severe finding. Distinct from `jpeg_photoshop_short_app13.jpg`, which pins the *earlier* `data_length >= 14` identifier guard (N4) and never reaches `parse_photoshop()`. |
| `palette_undersized_lut.jp2` | Palette (indexed-color) JP2 with an 8-bit index component but a `pclr` box declaring only 128 entries, so index values 128..255 would read past the R/G/B LUTs; regresses `validate_j2k_palette_mapping()` rejecting the mapping through the whole `SipiIOJ2k::read()` decode path — Kakadu parses the box structure, the guard fails it closed before the expansion buffer is sized (DEV-6065). The well-formed counterpart it must be distinguished from is the ISO/IEC 15444-4 conformance file `iso-15444-4/testfiles_jp2/file9.jp2` (256-entry, 3-column palette), which decodes to RGB. |
| `j2k_oversized_dimensions.jp2` | JP2 whose codestream `SIZ` marker segment (`0xFF51`) declares `Xsiz`/`Ysiz`/`XTsiz`/`YTsiz` of 262144px (`1 << 18`, above `kMaxDecodeDim` = `1 << 17`), with the `ihdr` box's height/width patched to match so container and codestream agree; regresses `validate_decode_dims()` rejecting the header — Kakadu parses the (structurally valid, single-tile) main header and `SipiIOJ2k::read()` gets as far as `codestream.get_dims()` before the guard reports the rejection, well before any decode buffer is sized (DEV-6063). |

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
`test/unit/fixtures/generate_jpeg_fixtures.py` — its
`generate_malformed()` function writes into a `malformed/` directory
alongside the `.../images/jpeg/` output dir the script is invoked with:

    uv run test/unit/fixtures/generate_jpeg_fixtures.py \
        test/_test_data/images/jpeg/

`palette_undersized_lut.jp2` is not produced by those generators: SIPI's own
J2K writer emits no palette JP2 (it decodes palette->RGB and has no
palette-encode path), and no palette-capable JP2 encoder ships in the dev
shell. It is hand-edited from the ISO/IEC 15444-4 conformance file
`iso-15444-4/testfiles_jp2/file9.jp2` by shrinking the `pclr` box's entry
count (`NE`) from 256 to 128, truncating the palette data to match, and
fixing the `pclr` and enclosing `jp2h` box lengths — every other box,
including the codestream, is byte-identical to file9. The codestream still
declares an 8-bit index, so `validate_j2k_palette_mapping()` sees `nentries
(128) < 1 << bps (256)` and rejects it.

`j2k_oversized_dimensions.jp2` is hand-edited from the smallest existing JP2
fixture, `images/unit/ycbcr16.jpx` (an 8x8, single-tile JPX): the `ihdr` box's
height/width fields and the codestream `SIZ` marker's `Xsiz`/`Ysiz`/`XTsiz`/
`YTsiz` fields (all four, to keep the file a single tile matching the
original structure) are patched from `8` to `262144` (`1 << 18`, big-endian
`uint32`), with no other bytes changed. Kakadu accepts the patched main
header as structurally valid — it never needs to read the still-8x8-sized
tile-part payload to report the declared dimensions — so `SipiIOJ2k::read()`
reaches `codestream.get_dims()` and `validate_decode_dims()` before any pixel
data would be touched.

The `.tif`/`.jp2`/`.jpg`/`.png` files the generators produce are committed
(via Git LFS) so CI does not need to regenerate them at test time. Each
fixture and its defect must be added to the table above in the same change
that commits the fixture.
