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
`gh_release_archive` repository_rule can authenticate. Forked PRs
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

## Remote build execution and caching

CI builds run on a self-hosted **NativeLink Remote Build Execution** backend: a
single x86_64 worker that cross-compiles all three target arches
(`linux-amd64`, `linux-aarch64`, `darwin-aarch64`), fronting a remote cache
(AC + CAS, `:50051`, mTLS) and a `bazel-remote` download cache (`:50052`) for
`http_archive` source tarballs. The connection, mTLS, and
cross-compile flags are assembled by the
[`bazel-rbe`](../../../.github/actions/bazel-rbe) composite action and injected
per workflow step — Bazel does not expand env vars in `.bazelrc`, so they live
in the workflow, not the rc file. The backend-agnostic tuning flags
(`--remote_download_minimal`, `--remote_local_fallback`, `--remote_timeout`,
`--remote_max_connections`) live in `.bazelrc` and are safe no-ops without a
remote configured.

See **[Remote build execution](./rbe.md)** for the full topology, the
cross-compile flag rationale, and the developer-facing workflow, and
[`infra/nativelink/`](../../../infra/nativelink) for the OpenTofu IaC and the
infra-team runbook.

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
