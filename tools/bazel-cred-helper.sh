#!/usr/bin/env bash
# Bazel credential helper for the gs://dasch-bazel-cache (bazel-remote on
# Cloud Run) endpoint. Reads HTTP Basic Auth credentials from the
# $BAZEL_CACHE_PASSWORD environment variable at invocation time and emits an
# `Authorization: Basic <b64>` header in the Bazel credential-helper protocol.
#
# Protocol: see https://github.com/bazelbuild/proposals/blob/main/designs/2022-06-07-bazel-credential-helpers.md
#   - Bazel invokes us with `get` as the first arg and a JSON object on stdin
#     of the form {"uri": "..."}. We ignore the URI — the same credentials
#     apply to every URI we serve from this cache.
#   - We emit JSON on stdout of the form
#     {"headers": {"Authorization": ["Basic <base64>"]}}
#
# Why this exists: the inline forms (`--remote_cache=grpcs://user:pass@host`
# and `--remote_header=Authorization=Basic <b64>`) both fail in our setup —
# the former is rejected by Cloud Run's GFE (credentials in HTTP/2
# :authority), and the latter loses its embedded space when `just`'s
# `{{FLAGS}}` textual substitution re-tokenises the recipe args. A
# credential helper sidesteps both because its CLI flag value has no spaces
# and the credentials never appear on the Bazel command line at all.

set -euo pipefail

# Read and discard stdin. Bazel sends {"uri": "..."} but for our single-host
# cache the answer is the same regardless of the URI.
cat >/dev/null

# If the env var is unset (local dev, fork PRs), emit empty headers — Bazel
# will then send no Authorization header and the request will go through
# unauthenticated (which our cache will reject, but the build proceeds via
# --remote_local_fallback).
if [ -z "${BAZEL_CACHE_PASSWORD:-}" ]; then
  printf '{}\n'
  exit 0
fi

b64=$(printf '%s' "ci-runner:$BAZEL_CACHE_PASSWORD" | base64 | tr -d '\n')
printf '{"headers":{"Authorization":["Basic %s"]}}\n' "$b64"
