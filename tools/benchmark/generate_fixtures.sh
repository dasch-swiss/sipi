#!/usr/bin/env bash
# Generate the SIPI benchmark fixture archive (decode/encode tiers).
#
# Produces `sipi-bench-fixtures-v1.tar.zst` — the variant matrix the
# `//src:decode_benchmark` and `//src:encode_benchmark` targets consume via
# the `@sipi_bench_fixtures` repository (a sha256-pinned release asset on
# dasch-swiss/dsp-ci-assets, fetched by `gh_release_archive`; see
# MODULE.bazel). This script is the checked-in provenance of that asset:
# re-running it with the pinned tool versions reproduces the matrix from the
# pinned upstream source.
#
# Master: imagecompression.info "New Test Images" big_building.ppm
# (7216×5412, 16-bit lossless PPM) — the de-facto codec-benchmark corpus
# (libjpeg-turbo vendors from it). The upstream readme.txt ("no sale"
# clause; not CC0) ships in the archive as NOTICE-imagecompression-info.txt.
#
# Variant matrix (8-bit sRGB, mirroring the Pillay study's format axis):
#   flat.tif      untiled flat TIFF, uncompressed     (deliberate slow baseline)
#   baseline.jpg  plain JPEG Q90                      (deliberate slow baseline)
#   pyr-none.tif  256×256 tiled pyramid, uncompressed (fast baseline)
#   pyr-zstd.tif  256×256 tiled pyramid, ZStd level 9
#   pyr-webp.tif  256×256 tiled pyramid, WebP Q90
#   pyr.jp2       JPEG2000, Pillay slide-14 params (rate 2.5, RPCL, PLT/TLM)
#
# HTJ2K (`Cmodes=HT` → .jph) is deliberately ABSENT: Kakadu's HT block
# coder is license-gated (`FBC_ENABLED` in coresys/fast_coding/fbc_common.h,
# commented out in our v8.7 pin — evaluation-only enablement per
# Enabling_HT.txt). The production SIPI build can neither encode nor decode
# HT codestreams, so an HTJ2K fixture would benchmark nothing SIPI can
# serve. Revisit if the Kakadu license is extended to HT.
#
# Pinned tool chain (record any deviation in the release notes):
#   vips 8.18.2 (nixpkgs)  — run via: nix shell nixpkgs#vips nixpkgs#gnutar
#   Kakadu v8.7 (01727L)   — kdu_compress built from the @kakadu Bazel
#                            external repo source:
#                              cp -RL "$(bazel info output_base)/external/+kakadu_extension+kakadu/." /tmp/kdu &&
#                              cd /tmp/kdu/make &&
#                              PATH="/usr/bin:$PATH" make -f Makefile-Mac-arm-64-gcc all_but_jni
#                            (kdu_compress + libkdu_v87R.so land under bin/ +
#                            lib/; the trailing kdu_text_extractor failure is
#                            benign). Point KDU_COMPRESS + KDU_LIB_DIR at them.
#   GNU tar + zstd         — deterministic archive (sorted names, fixed mtime)
#
# Usage:
#   KDU_COMPRESS=/tmp/kdu/bin/Mac-arm-64-gcc/kdu_compress \
#   KDU_LIB_DIR=/tmp/kdu/lib/Mac-arm-64-gcc \
#     nix shell nixpkgs#vips nixpkgs#gnutar --command \
#     tools/benchmark/generate_fixtures.sh [workdir]
#
# Upload (maintainer, one-time per version):
#   gh release create sipi-bench-fixtures-v1 --repo dasch-swiss/dsp-ci-assets \
#     --title "SIPI benchmark fixtures v1" --notes-file <notes> \
#     <workdir>/sipi-bench-fixtures-v1.tar.zst
#   ...then update the sha256 in MODULE.bazel's benchmark_fixtures extension.

set -euo pipefail

VERSION="v1"
NAME="sipi-bench-fixtures-${VERSION}"
WORKDIR="${1:-/tmp/sipi-bench-fixtures}"
ZIP_URL="https://imagecompression.info/test_images/rgb16bit.zip"
ZIP_SHA256="917716d769e8992d6955c2c3726b7a9b3625bd37051bd6d3241619a047353c27"
# ADR-0002 epoch — normalises any tool-embedded timestamp.
export SOURCE_DATE_EPOCH=946684800

KDU_COMPRESS="${KDU_COMPRESS:?set KDU_COMPRESS to the kdu_compress binary (see header)}"
KDU_LIB_DIR="${KDU_LIB_DIR:?set KDU_LIB_DIR to the Kakadu shared-lib dir (see header)}"

command -v vips >/dev/null || { echo "vips not on PATH (nix shell nixpkgs#vips)" >&2; exit 1; }
command -v zstd >/dev/null || { echo "zstd not on PATH" >&2; exit 1; }
TAR="tar"
"$TAR" --version 2>/dev/null | grep -q "GNU tar" || TAR="gtar"
"$TAR" --version 2>/dev/null | grep -q "GNU tar" || { echo "GNU tar required (nix shell nixpkgs#gnutar)" >&2; exit 1; }

mkdir -p "$WORKDIR/src"
cd "$WORKDIR"

# ── 1. Pinned source ────────────────────────────────────────────────────
if [ ! -f src/rgb16bit.zip ]; then
  echo "==> Downloading $ZIP_URL (826 MB)"
  curl -sL --retry 3 -o src/rgb16bit.zip "$ZIP_URL"
fi
echo "${ZIP_SHA256}  src/rgb16bit.zip" | shasum -a 256 -c -
unzip -o -q src/rgb16bit.zip big_building.ppm readme.txt -d src/

# ── 2. Variant matrix ───────────────────────────────────────────────────
OUT="$WORKDIR/$NAME/big_building"
rm -rf "$WORKDIR/$NAME"
mkdir -p "$OUT"

echo "==> 16-bit master → 8-bit sRGB flat.tif (slow baseline #1)"
vips colourspace src/big_building.ppm "$OUT/flat.tif[compression=none]" srgb

echo "==> baseline.jpg Q90 (slow baseline #2)"
vips jpegsave "$OUT/flat.tif" "$OUT/baseline.jpg" --Q 90

echo "==> tiled-pyramid TIFFs (none / zstd / webp)"
vips tiffsave "$OUT/flat.tif" "$OUT/pyr-none.tif" --tile --pyramid \
  --compression none --tile-width 256 --tile-height 256 --bigtiff
vips tiffsave "$OUT/flat.tif" "$OUT/pyr-zstd.tif" --tile --pyramid \
  --compression zstd --level 9 --tile-width 256 --tile-height 256
vips tiffsave "$OUT/flat.tif" "$OUT/pyr-webp.tif" --tile --pyramid \
  --compression webp --Q 90 --tile-width 256 --tile-height 256

echo "==> JP2 (Kakadu, Pillay slide-14 params)"
# kdu_compress reads PPM natively (TIFF support depends on compile flags).
vips copy "$OUT/flat.tif" "$WORKDIR/src/source8.ppm"
KDU_ARGS=(-rate 2.5 Clayers=1 Clevels=7 "Cprecincts={256,256}"
  Corder=RPCL "Cblk={64,64}" ORGgen_plt=yes ORGtparts=R ORGgen_tlm=8
  Cuse_sop=yes -num_threads 8)
DYLD_LIBRARY_PATH="$KDU_LIB_DIR" LD_LIBRARY_PATH="$KDU_LIB_DIR" \
  "$KDU_COMPRESS" -i "$WORKDIR/src/source8.ppm" -o "$OUT/pyr.jp2" "${KDU_ARGS[@]}"
rm -f "$WORKDIR/src/source8.ppm"

# ── 3. Notice + provenance ──────────────────────────────────────────────
cp src/readme.txt "$WORKDIR/$NAME/NOTICE-imagecompression-info.txt"
{
  echo "# SIPI benchmark fixtures ${VERSION}"
  echo
  echo "Generated by tools/benchmark/generate_fixtures.sh (dasch-swiss/sipi)."
  echo
  echo "Master: big_building.ppm from ${ZIP_URL}"
  echo "  zip sha256: ${ZIP_SHA256}"
  echo "  master sha256: $(shasum -a 256 src/big_building.ppm | cut -d' ' -f1)"
  echo "  Redistribution notice: NOTICE-imagecompression-info.txt"
  echo
  echo "Toolchain:"
  echo "  $(vips --version)"
  echo "  Kakadu: $(DYLD_LIBRARY_PATH="$KDU_LIB_DIR" LD_LIBRARY_PATH="$KDU_LIB_DIR" "$KDU_COMPRESS" -version 2>/dev/null | sed -n 's/.*version //p' | head -1)"
  echo "  SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}"
  echo
  echo "Variants (sha256, bytes, name):"
  (cd "$WORKDIR/$NAME" && for f in big_building/*; do
    printf '  %s  %12d  %s\n' "$(shasum -a 256 "$f" | cut -d' ' -f1)" "$(stat -f %z "$f" 2>/dev/null || stat -c %s "$f")" "$f"
  done)
} > "$WORKDIR/$NAME/README.md"

# ── 4. Deterministic tarball ────────────────────────────────────────────
echo "==> ${NAME}.tar.zst"
"$TAR" --sort=name --numeric-owner --owner=0 --group=0 \
  --mtime="@${SOURCE_DATE_EPOCH}" -C "$WORKDIR" -cf - "$NAME" \
  | zstd -19 -T0 -f -o "$WORKDIR/${NAME}.tar.zst"
shasum -a 256 "$WORKDIR/${NAME}.tar.zst"
