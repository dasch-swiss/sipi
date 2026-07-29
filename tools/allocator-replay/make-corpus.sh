#!/usr/bin/env bash
# Generate the JP2-only replay corpus for the allocator harness.
#
# Prod serves exclusively JPEG 2000, and the Kakadu decode path is where the
# allocator-behavior story lives (see docs/adr/0019 and
# docs/src/development/allocator-replay.md), so the corpus is large JP2s
# synthesized from the repo's test images with the sipi CLI. Idempotent:
# existing outputs are kept.
#
# Usage: make-corpus.sh <out-dir> [sipi-binary]
#   out-dir      target directory for the generated images
#   sipi-binary  default: bazel-bin/src/cli/sipi (run `just bazel-build` first)
set -euo pipefail

OUT="${1:?usage: make-corpus.sh <out-dir> [sipi-binary]}"
SIPI="${2:-bazel-bin/src/cli/sipi}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
IMG="$REPO/test/_test_data/images"

[ -x "$SIPI" ] || { echo "sipi binary not found at $SIPI — run 'just bazel-build' first" >&2; exit 1; }
mkdir -p "$OUT"

gen() { # gen <output> <size> <source>
  [ -f "$OUT/$1" ] && { echo "kept $1"; return; }
  "$SIPI" convert -s "^$2" -F jp2 "$3" "$OUT/$1"
  echo "made $1 ($2)"
}

gen big4k.jp2 4000,4000 "$IMG/unit/CIELab16.tif"
gen big6k.jp2 6000,4500 "$IMG/knora/Leaves8NotJpeg.jpg"
gen big8k.jp2 8000,8000 "$IMG/knora/Leaves8.tif"
# 12k from the 8k output: a full decode is ~430 MB, matching observed prod
# peaks (decode-memory estimates up to ~350 MB on vre-prod-01).
gen big12k.jp2 12000,12000 "$OUT/big8k.jp2"
