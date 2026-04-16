# JPEG test fixtures

## Real failing fixtures (DEV-6250)

| File | Source | Characteristics |
|---|---|---|
| `35-2421d-o.jpg` | Heritage collection | 404×201, 8-bit RGB, JFIF + APP13 (Photoshop/IPTC with German umlauts) + APP2 (ICC) + APP1 (EXIF) + APP1 (XMP). Created by Adobe Photoshop CS (2008). |
| `35-2421d-r.jpg` | Heritage collection | Same characteristics as above; sibling variant. |

Both are legitimate archival images that sipi must be able to ingest as an
IIIF-compatible preservation server. They fail on `main` prior to the
DEV-6250 fix — see `Jpeg_35_2421d_ReadsSuccessfullyTest` (R6).

## Synthetic fixtures (DEV-6250 / DEV-6257 / F3)

Regenerate with:

    uv run test/unit/sipiimage/fixtures/generate_jpeg_fixtures.py \
        test/_test_data/images/jpeg/

The generator lives at
`test/unit/sipiimage/fixtures/generate_jpeg_fixtures.py`. The `.jpg` files
are committed so CI does not need a Python/Pillow tool chain.

| File | Purpose |
|---|---|
| `cmyk/cmyk_photoshop_app14.jpg` | 128×128 CMYK baseline **with** Adobe APP14 marker (transform=0). Pinned by `JpegCmykPhotoshopApp14InversionTest` (R9) to cover the Photoshop inversion path. |
| `cmyk/cmyk_raw_no_app14.jpg` | Same pixel content but with APP14 stripped. Pinned by `JpegCmykRawNoApp14NotInvertedTest` (R10) to prove the fix does not over-invert files lacking APP14. |
| `malformed_xmp.jpg` | 64×64 RGB JPEG with a deliberately corrupted APP1 XMP segment (valid JPEG envelope, non-XML XMP payload). Used by `CliJsonOutputStdoutIsSingleJsonDoc` (F3) to exercise the `log_warn` → stderr routing required by the `--json` single-document contract. |
