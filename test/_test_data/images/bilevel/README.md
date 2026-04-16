# Bilevel TIFF test fixtures

Synthetic 1-bit bilevel TIFF fixtures used by the `sipi_image_tests`
regression tests for DEV-6249. Regenerate with:

    $TMPDIR/generate_bilevel_tiffs test/_test_data/images/bilevel/

The generator source lives at
`test/unit/sipiimage/fixtures/generate_bilevel_tiffs.cpp`. The `.tif` files
below are committed so CI does not need a libtiff tool chain or Python at
test time.

| File | Width×Height | Compression | Photometric |
|---|---|---|---|
| `bilevel_lzw_miniswhite.tif` | 128×128 | LZW | MINISWHITE |
| `bilevel_none_miniswhite.tif` | 128×128 | None | MINISWHITE |
| `bilevel_lzw_minisblack.tif` | 128×128 | LZW | MINISBLACK |
| `bilevel_roi_test.tif` | 256×192 | LZW | MINISWHITE |

The pixel content is a deterministic 16-pixel checkerboard so visual
inspection makes inversion / ROI bugs obvious.
