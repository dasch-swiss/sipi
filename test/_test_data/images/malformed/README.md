# Malformed image test fixtures

Crafted malformed image fixtures for the codec memory-safety regression
suite: files whose headers claim dimensions, channel counts, or internal
offsets that a well-formed encoder would never produce, used to exercise the
decode-path input-validation and overflow-guard code added alongside them.

This directory is currently empty. Each fixture's defect is documented in the
table below as fixtures land in later commits.

| File | Defect |
|---|---|
| _(none yet)_ | |

## Git LFS

Fixtures under this directory are Git LFS objects. A fresh worktree lands
them as ~131-byte pointer files, not the real image bytes — image tests then
fail with a cryptic error-500 / `text/plain` response rather than a decode
error. Run `git lfs pull` and check the file size (`ls -l`) before assuming a
test failure is a codec bug.

## Regenerating

The generator source lives at
`test/unit/fixtures/generate_malformed_images.cpp` (a `cc_binary`, built via
Bazel):

    bazel run //test/unit/fixtures:generate_malformed_images -- test/_test_data/images/malformed/

The `.tif`/`.jp2`/`.jpg`/`.png` files this produces are committed (via Git
LFS) so CI does not need to regenerate them at test time. Each fixture and
its defect must be added to the table above in the same change that commits
the fixture.
