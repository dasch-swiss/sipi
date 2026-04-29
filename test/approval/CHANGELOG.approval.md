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
| _empty_ | | | | |
