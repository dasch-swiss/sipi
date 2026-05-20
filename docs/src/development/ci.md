# CI and Release

This page documents SIPI's CI pipeline and release automation.

## Release automation (release-please)

Releases are fully automated via
[release-please](https://github.com/googleapis/release-please).
When commits are merged to `main`, release-please reads their
[Conventional Commit](https://www.conventionalcommits.org/) prefixes
to determine the SemVer bump and generate the changelog.

**Configuration files:**

- `.github/release-please/config.json` — changelog sections, release type
- `.github/release-please/manifest.json` — current version
- `.github/workflows/release-please.yml` — GitHub Actions workflow

**How commit types map to releases:**

| Prefix | SemVer effect | Changelog section |
|--------|--------------|-------------------|
| `feat:` | minor bump | Features |
| `fix:` | patch bump | Bug Fixes |
| `feat!:` / `fix!:` | major bump | Breaking Changes |
| `perf:` | patch bump | Performance Improvements |
| `docs:`, `style:`, `refactor:`, `test:`, `build:`, `ci:`, `chore:` | no bump | hidden |

!!! warning "Correct commit prefixes are critical"
    A commit without a valid Conventional Commit prefix will be invisible to
    release-please — it won't trigger a release or appear in the changelog.
    See [Commit Message Schema](developing.md#commit-message-schema) for the
    full format specification.

## Pull request CI

Workflow: `.github/workflows/ci.yml`. Trigger: `pull_request` only.

### Test matrix

The `test` job runs on three platforms — `linux-amd64`,
`linux-arm64`, `darwin-arm64`. Every platform runs the same steps:

1. **Build + test** — `just bazel-test` (fastbuild, no
   instrumentation; unit + approval + e2e in a single Bazel
   invocation).
2. **Docker smoke tests (Linux only)** — `just bazel-test-smoke`
   builds `//src:image` as a transitive `data` dep of the
   `:docker_smoke` rust_test, the test loads the OCI tarball into
   the local Docker daemon, and runs the smoke suite against the
   loaded container.
3. **Docker Scout** (Linux PRs only):
   - `Docker Scout — compare to production` — both arches.
   - `Docker Scout — CVE report (SARIF)` and
     `Upload SARIF to GitHub Security` — amd64 only (CVE findings
     are arch-independent).

A separate `docs` job runs `just docs-build` (mkdocs strict-mode
build) on ubuntu-latest. The `docs-build` job is the gate that
catches broken cross-links and stale nav entries on every PR.

### Forked PR behavior

Every Bazel-invoking step sets
`GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}` on its `env:` so the
`kakadu_archive` repository_rule can authenticate. Forked PRs
don't have access to `DASCHBOT_PAT`, so the Kakadu fetch fails
and the build short-circuits. Internal PRs are unaffected.

## Post-merge coverage

Workflow: `.github/workflows/coverage.yml`. Trigger:
`push: branches: [main]` + `workflow_dispatch` for manual runs.
Fires on every merge to `main`.

A single `linux-amd64` job enters the `.#llvm-tools` dev shell
(default shell + `llvmPackages_19.llvm` for `llvm-cov` /
`llvm-profdata`) and runs `just bazel-coverage`. The combined
lcov report at `bazel-out/_coverage/_coverage_report.dat` is
uploaded to Codecov.

**Why split out:** Coverage instrumentation adds 1.5–2× compile
overhead and slower test runtime; running it on every PR push
across three platforms wasted CI minutes without commensurate
signal. Per-PR coverage delta in Codecov is the trade-off — drift
shows up immediately after merge instead. To restore PR-scoped
signal selectively, add a `pull_request: paths: ['src/**']` trigger
to this workflow.

**Why a separate dev shell:** `bazel coverage`'s
`collect_cc_coverage.sh` hard-requires `COVERAGE_GCOV_PATH` and
`LLVM_COV` env vars on every test action. The justfile recipe
resolves them via `$(command -v llvm-{cov,profdata})`, so those
binaries must be on PATH. Keeping LLVM 19 host binaries out of the
default shell saves ~200 MB of closure on first `nix develop` for
everyday users.

## Tag release CI/CD

Workflow: `.github/workflows/publish.yml`. Trigger: tag push
matching `v*`.

Gate model:

1. `validate-docker / {amd64, arm64}` — each per-arch runner
   builds the Docker image via
   `just bazel-docker-build-${arch}` and runs
   `just bazel-test-smoke` against it.
2. `release-gate` — fires on `validate-docker` success.
3. Publish jobs run in parallel after the gate:
   - `publish-docker / {amd64, arm64}` — rebuilds the per-arch
     image, extracts the `.debug` file via
     `just bazel-docker-extract-debug ${arch}`, runs smoke tests,
     pushes via `just bazel-docker-push-${arch}`, uploads SBOM,
     pushes debug symbols to Sentry.
   - `manifest` — runs `just bazel-docker-publish-manifest`
     (`crane index append`) to assemble the multi-arch manifest at
     `daschswiss/sipi:v<version>` from the two pushed per-arch
     digests. Also tags the manifest as `:latest`.
   - `docs` — mkdocs deploy.
4. `sentry` finalises the release after the manifest job
   completes.

## Cache strategy

CI routes **all three Bazel cache layers — AC, CAS, and the
repository_cache for `http_archive` source tarballs — through a
single bazel-remote gRPC endpoint** hosted on Cloud Run, backed by
GCS. One bucket, one auth path, one endpoint.

**Topology.**

```
GHA workflow ──gRPC over TLS, HTTP basic auth──▶ Cloud Run: bazel-cache-proxy
                                                  │ (us-central1, scale-to-zero,
                                                  │  buchgr/bazel-remote-cache)
                                                  ▼
                                                gs://dasch-bazel-cache
                                                (us-central1, STANDARD,
                                                 uniform IAM, 30-day lifecycle)
```

The Cloud Run service runs `buchgr/bazel-remote-cache:latest` with
flags:

- `--gcs_proxy.bucket=dasch-bazel-cache --gcs_proxy.use_default_credentials`
  — GCS as the upstream tier via ADC (the attached
  `bazel-cache-proxy@dsp-repository-automation` service account).
  Local disk cache is the primary tier; ephemeral per Cloud Run
  instance but content survives in GCS.
- `--experimental_remote_asset_api` — enables the Remote Asset API
  that Bazel's `--experimental_remote_downloader` calls to mirror
  `http_archive` tarballs through the cache.
- `--htpasswd_file=/etc/bazel-remote/htpasswd` — basic-auth gate.
  The htpasswd file is mounted from Secret Manager
  (`bazel-cache-htpasswd`) as a Cloud Run secret file.
- `--grpc_address=0.0.0.0:8080 --http_address=unix:///tmp/http.sock`
  — gRPC on Cloud Run's expected port; HTTP listener bound to a
  unix socket and so not externally reachable.

**Auth.** Bazel sends HTTP Basic Auth credentials via a credential
helper script (`tools/bazel-cred-helper.sh`). The helper reads
`$BAZEL_CACHE_PASSWORD` from the Bazel subprocess environment at
request time, base64-encodes `ci-runner:<password>`, and emits
`{"headers":{"Authorization":["Basic <b64>"]}}` per the Bazel
[credential-helper protocol](https://github.com/bazelbuild/proposals/blob/main/designs/2022-06-07-bazel-credential-helpers.md).
Cloud Run IAM is **not** used to gate access — Bazel's gRPC client
doesn't speak Cloud Run OIDC. The password lives in Secret Manager
(`bazel-cache-htpasswd`, bcrypt-hashed for bazel-remote to verify)
and in the GH repo secret `BAZEL_CACHE_PASSWORD` (plain text, read
by the helper at runtime). Fork PRs cannot see the GH secret →
helper emits empty headers → request goes unauthenticated → bucket
declines → Bazel falls back to local execution via
`--remote_local_fallback`.

**Why a credential helper instead of inline credentials?** Two
simpler approaches don't work in our setup:

- `--remote_cache=grpcs://ci-runner:<password>@<host>:443` puts the
  credentials in the HTTP/2 `:authority` pseudo-header. Cloud Run's
  Google Front End (GFE) rejects `:authority` values that contain
  `user:pass@host` with HTTP 400 before the request reaches our
  container.
- `--remote_header=Authorization=Basic <b64>` is the Bazel-native
  flag for adding metadata. It works directly on the bazel CLI, but
  the space between `Basic` and the base64 token loses its
  argument-boundary protection when `just`'s `{{FLAGS}}` textual
  substitution re-tokenises the recipe args inside the
  `bash -c "..."` invocation. Bazel ends up parsing the value as
  `Basic` (the literal token) without the base64 payload, and
  bazel-remote rejects the request as `UNAUTHENTICATED`.

The credential helper sidesteps both: the `--credential_helper=
$GITHUB_WORKSPACE/tools/bazel-cred-helper.sh` flag has no embedded
whitespace, and the credentials never appear on the Bazel command
line at all.

**Bazel wiring.** Each Bazel-running workflow step assembles the
flag string in a shell variable when `BAZEL_CACHE_PASSWORD` is
present:

```
--remote_cache=grpcs://<host>:443
--experimental_remote_downloader=grpcs://<host>:443
--credential_helper=$GITHUB_WORKSPACE/tools/bazel-cred-helper.sh
--remote_upload_local_results=true
```

Bazel does not expand environment variables inside `.bazelrc`, so
these CLI flags live in workflow steps; the `.bazelrc` only carries
cache-backend-agnostic static flags
(`--remote_cache_compression`, `--remote_download_minimal`,
`--remote_local_fallback`, `--remote_timeout=30s`,
`--remote_max_connections=100`) that are safe-no-op without
`--remote_cache`.

Local dev never sets `BAZEL_CACHE_PASSWORD` and never passes
`--remote_cache=…`, so local builds don't touch the bucket. If a
developer somehow invokes Bazel with `--credential_helper=` while
their env doesn't have `BAZEL_CACHE_PASSWORD`, the helper emits
empty headers and the request goes unauthenticated.

**Top-level flags in `.bazelrc`** (always on, safe-no-op without a
remote cache): `--remote_cache_compression` (zstd over the wire),
`--remote_download_minimal` (don't fetch intermediate action
outputs), `--remote_local_fallback` (build locally if the cache is
unreachable — a Cloud Run outage degrades CI wall-clock but never
breaks it).

`--incompatible_strict_action_env` is set in `.bazelrc` so host env
vars (e.g. GitHub-injected `GITHUB_RUN_ID`) don't poison Bazel's
cache keys across runs.

### Cost

Cloud Run with `--min-instances=0`: zero cost while CI is idle.
Active CI runs incur ~$0/month at current PR volume (low-millis of
CPU-seconds per build, free egress within GCP since the bucket is
in the same region). GCS storage: ~$0.40-$0.80/month at 20-40 GB
sustained, STANDARD class in `us-central1`. Total expected monthly
spend on the cache: under $2.

### Runbooks

**Password rotation (annual).**

```bash
NEW_PASS=$(openssl rand -hex 16)
# 1. New htpasswd line into Secret Manager
htpasswd -nbB ci-runner "$NEW_PASS" > /tmp/htpasswd
gcloud secrets versions add bazel-cache-htpasswd --data-file=/tmp/htpasswd
shred -u /tmp/htpasswd
# 2. Push the plain password to GH (workflows read this)
gh secret set BAZEL_CACHE_PASSWORD --repo dasch-swiss/sipi --body "$NEW_PASS"
# 3. Bounce the Cloud Run revision so the new secret version is picked up
gcloud run services update bazel-cache-proxy --region=us-central1 \
  --update-secrets=/etc/bazel-remote/htpasswd=bazel-cache-htpasswd:latest
# 4. (After verifying CI works) disable the old Secret Manager version
gcloud secrets versions list bazel-cache-htpasswd
gcloud secrets versions disable <OLD_VERSION> --secret=bazel-cache-htpasswd
```

**Cache nuke (suspected corruption).** Bazel re-populates on the
next run:

```bash
gcloud storage rm --recursive gs://dasch-bazel-cache/**
```

**Inspect cache state.**

```bash
gcloud storage du gs://dasch-bazel-cache --readable-sizes
gcloud storage ls gs://dasch-bazel-cache --recursive | head
```

## Build Event Service (BuildBuddy)

CI publishes a Bazel **Build Event Service (BES)** stream to
`dasch.buildbuddy.io` for every invocation. BES is read-only build-event
mirroring (invocation timeline, action graph, per-test logs, failure
stderr); it does not affect caching or build execution.

Each Bazel-running workflow step appends a `$BES` flag string to its
`just bazel-*` invocation when the repo secret `BUILDBUDDY_ORG_API_KEY`
is set:

```
--bes_backend=grpcs://dasch.buildbuddy.io
--bes_results_url=https://dasch.buildbuddy.io/invocation/
--bes_header=x-buildbuddy-api-key=$BUILDBUDDY_ORG_API_KEY
--build_metadata=ROLE=CI
```

Bazel prints `Streaming build results to:
https://dasch.buildbuddy.io/invocation/<uuid>` near the start of the
build; that URL renders the action graph, target timings, and per-test
output for that invocation.

**Fork PRs.** GitHub does not expose secrets to forked-PR runs, so
`BUILDBUDDY_ORG_API_KEY` is empty and `$BES=""` — Bazel runs without
publishing. Mirrors the existing `$REMOTE` fork-safety gate.

**Why not in `.bazelrc`.** Bazel does not expand env vars in
`.bazelrc`, so the API-key-bearing flag cannot live there. Splitting
the static URLs into `.bazelrc` and the API key into the workflow
would leave `.bazelrc` in a half-configured state where local builds
attempt to publish to BuildBuddy without credentials. Keeping all
three flags together in the workflow keeps the convention consistent
with the cache wiring above. `--bes_header` (BES-scoped) is used in
preference to `common --remote_header` so the BuildBuddy key does not
attach to bazel-remote / downloader gRPC streams, which authenticate
through `tools/bazel-cred-helper.sh`.

**Out of scope here.** Caching stays on bazel-remote on Cloud Run; RBE
is not used. Future workstreams may collapse cache + BES + RBE under a
single BuildBuddy endpoint, at which point a single `common
--remote_header=x-buildbuddy-api-key=…` line replaces all three
auth paths.

## Local reproduction

Every CI step invokes `just <recipe>` — there are no inline
`bazel ...` calls in any workflow. To reproduce any CI job
locally, run the same recipe inside `nix develop`:

```bash
nix develop

# Full PR test job, one arch (matches `ci.yml test`)
just bazel-test
just bazel-test-smoke

# Coverage (matches `coverage.yml`; needs the .#llvm-tools shell)
nix develop .#llvm-tools --command just bazel-coverage

# Sanitizer build + e2e (what sanitizer.yml runs)
just bazel-build-sanitized
just bazel-test-e2e --config=asan --config=ubsan

# Fuzz build + run (what fuzz.yml runs; linux-x86_64 only)
just bazel-build-fuzz
mkdir fuzz-corpus-live
just bazel-run-fuzz fuzz-corpus-live 60 fuzz/handlers/corpus

# Docker image with split debug symbols (what publish.yml does)
just bazel-docker-build-amd64
just bazel-test-smoke
just bazel-docker-extract-debug amd64
just bazel-docker-push-amd64
just bazel-docker-publish-manifest
```

**CI invokes justfile only.** If a CI step is not a `just <recipe>`
invocation, that's a drift signal — either the step is non-build
glue (e.g. artifact upload, Codecov upload, Sentry push) or the
justfile is missing a recipe and should grow one.

## Nightly fuzz testing

A nightly fuzz workflow (`.github/workflows/fuzz.yml`) runs
libFuzzer against the IIIF URL parser to find crashes and edge
cases. Fuzz corpora are persisted as artifacts across runs so
coverage accumulates over time.

See [Fuzzing](fuzzing.md) for details on the fuzz harness, corpus
management, and how to reproduce crashes locally.
