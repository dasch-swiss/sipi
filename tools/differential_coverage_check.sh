#!/usr/bin/env bash
#
# Drift guard for the differential parity gate (plan 02 §7).
#
# The gate in `test/e2e/tests/differential.rs` must stay COMPLETE: every
# replayable e2e behaviour is represented in its corpus, and every behaviour
# that isn't is inherently non-replayable (uploads/state, raw-TCP, timing,
# lifecycle, config-write, CLI, proptest, snapshots). Because the corpus is
# deduped (many e2e tests hit one request), we can't map tests 1:1 — so we
# pin the e2e #[test] count as a coarse ratchet. When it changes, the author
# must consciously update the corpus (or confirm the new test is
# non-replayable) and bump EXPECTED_E2E_TESTS below in the same change.
#
# This is intentionally cheap (grep + compare); run in CI and via
# `just differential-coverage-check`.
set -euo pipefail

cd "$(dirname "$0")/.."

# Baseline: #[test] + #[tokio::test] across test/e2e/tests/*.rs EXCLUDING the
# differential corpus file itself. Bump this (with a corpus update) whenever an
# e2e test is added or removed.
EXPECTED_E2E_TESTS=274

# Count via awk on a concatenated stream with literal `[ \t]` + exact string
# compare — deterministic across awk/grep implementations. (Earlier a
# `grep -E '\s'` form disagreed between GNU grep on CI and a local `ugrep`
# alias; awk + `[ \t]` + equality avoids that regex-class portability trap.)
actual=$(find test/e2e/tests -maxdepth 1 -name '*.rs' ! -name 'differential.rs' -exec cat {} + |
  awk '{ s = $0; gsub(/^[ \t]+|[ \t]+$/, "", s); if (s == "#[test]" || s == "#[tokio::test]") c++ } END { print c + 0 }')

if [ "$actual" -ne "$EXPECTED_E2E_TESTS" ]; then
  cat >&2 <<EOF
differential coverage drift: e2e #[test] count is $actual, expected $EXPECTED_E2E_TESTS.

An e2e test was added or removed. Before bumping EXPECTED_E2E_TESTS in
tools/differential_coverage_check.sh, make sure the change is reflected in the
differential gate:
  - replayable (idempotent GET/HEAD against a static fixture)  -> add a Case to
    the corpus in test/e2e/tests/differential.rs
  - non-replayable (upload/state, raw-TCP, timing, lifecycle, config-write,
    CLI, proptest, snapshot)                                    -> no Case needed
Then set EXPECTED_E2E_TESTS=$actual in the same commit.
EOF
  exit 1
fi

echo "differential coverage OK: $actual e2e tests, corpus in differential.rs is the parity gate."
