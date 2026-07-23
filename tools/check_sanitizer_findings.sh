#!/usr/bin/env bash
#
# Check for actual sanitizer findings (SUMMARY lines), not just file
# existence: `.bazelrc`'s `test:asan` block sets
# `ASAN_OPTIONS=…halt_on_error=0:log_path=$TEST_UNDECLARED_OUTPUTS_DIR/asan-e2e`,
# so ASan writes a report (even empty) per test, Bazel surfaces it under
# `<pkg>/<target>/test.outputs/`, and `halt_on_error=0` lets a leak be
# reported without failing the test — so trust `SUMMARY:`, not exit codes.
# `find` (not a fixed glob) collects reports across every layer (src / unit /
# approval / e2e), since the scope is no longer e2e-only.
#
# Invoked as `bash tools/check_sanitizer_findings.sh` by the sanitizer jobs in
# ci.yml, after `just bazel-test-unit`/`bazel-test-e2e --config=asan
# --config=ubsan`.
set -euo pipefail

mapfile -t REPORTS < <(find bazel-testlogs -type f -path '*/test.outputs/asan-e2e.*' 2>/dev/null)
if [ ${#REPORTS[@]} -gt 0 ] && grep -q "SUMMARY:" "${REPORTS[@]}" 2>/dev/null; then
  echo "::error::Sanitizer findings detected — PR cannot merge with memory-safety issues"
  echo ""
  echo "=== Unique findings ==="
  grep -h "SUMMARY:" "${REPORTS[@]}" 2>/dev/null | sort -u || true
  echo ""
  echo "=== Full reports ==="
  for f in "${REPORTS[@]}"; do
    echo "--- $f ---"
    cat "$f"
    echo ""
  done
  exit 1
fi
echo "No sanitizer findings. All tests passed cleanly under ASan + UBSan."
