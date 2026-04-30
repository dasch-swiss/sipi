#!/usr/bin/env bash
# Bazel workspace-status script.
#
# Wired by `.bazelrc`'s `--workspace_status_command=tools/workspace_status.sh`.
# Bazel runs this on every build and parses each line as a `KEY VALUE` pair.
# Keys prefixed `STABLE_*` invalidate downstream actions only when the value
# *changes* — so editing a `.cpp` file does not regenerate `SipiVersion.h`
# (the values stay identical) but bumping `version.txt` does.
#
# Consumers (planted in subsequent commits):
#   - `src/BUILD.bazel`   — `expand_template(stamp_substitutions = {…})`
#                           bakes STABLE_* values into `SipiVersion.h`.
#   - `src/BUILD.bazel`   — `oci_image` rule reads STABLE_IMAGE_CREATED for
#                           reproducible image timestamps (Y+4 / DEV-6346).
#   - `tools/sentry`      — Sentry release uploader reads STABLE_GIT_VERSION
#                           on the publish.yml workflow (Y+4).
#
# `set -euo pipefail` ensures any read failure (e.g. missing version.txt)
# fails Bazel loudly rather than producing an empty stamp.

set -euo pipefail

# `BUILD_WORKSPACE_DIRECTORY` is set by `bazel run` but not by `bazel build`;
# the workspace-status script always runs from the workspace root, so
# falling back to `.` is correct.
cd "${BUILD_WORKSPACE_DIRECTORY:-.}"

# Commit SHA — pinned binary ↔ source pairing for debugger / Sentry / debug-info.
echo "STABLE_GIT_COMMIT $(git rev-parse HEAD 2>/dev/null || echo unknown)"

# Pretty version (e.g. v4.1.1, or v4.1.1-2-gabcdef-dirty in dev). Used in
# `--version` output via `BUILD_SCM_TAG`.
echo "STABLE_GIT_VERSION $(git describe --tags --always --dirty 2>/dev/null || echo unknown)"

# Image-creation timestamp pinned to the commit's authored date — never wall
# clock — so two builds of the same source from the same lockfile produce
# byte-identical OCI image tarballs.
echo "STABLE_IMAGE_CREATED $(git log -1 --format=%cI 2>/dev/null || echo 1970-01-01T00:00:00Z)"

# release-please integration. `version.txt` is the single source of truth;
# `expand_template` substitutes this into `SipiVersion.h.in`'s
# `@SIPI_VERSION_STRING@`. release-please bumps this file via PR; the
# resulting commit triggers a re-link (only the few translation units that
# include `SipiVersion.h` recompile).
echo "STABLE_SIPI_VERSION $(tr -d '[:space:]' < version.txt)"
