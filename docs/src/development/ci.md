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

Workflow: `.github/workflows/ci.yml`.

### Test matrix

The `test` job runs on three platforms — `linux-amd64`,
`linux-arm64`, `darwin-arm64`. On each:

1. **Build + test** via Bazel.
   - `linux-amd64` runs `just bazel-coverage` (instrumented build,
     emits lcov for Codecov).
   - The other two arches run `just bazel-test` (fastbuild, no
     instrumentation; same test signal without paying the
     1.5–2× compile/runtime cost on arches that don't upload).
2. **Hurl HTTP contract tests** — `just hurl-test` against the
   built sipi binary.
3. **Coverage upload (linux-amd64 only)** — Codecov consumes the
   combined lcov directly from
   `bazel-out/_coverage/_coverage_report.dat`.
4. **Docker smoke tests (Linux only)** — `just bazel-test-smoke`
   builds `//src:image` as a transitive `data` dep of the
   `:docker_smoke` rust_test, the test loads the OCI tarball into
   the local Docker daemon, and runs the smoke suite against the
   loaded container.
5. **Docker Scout** (Linux PRs only):
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

CI's Bazel disk cache is managed by `actions/cache@v5` rather than
`setup-bazel`'s built-in disk-cache wiring. The full rationale
(0-byte poisoning on analysis-phase failures, targeted key formula,
repository-cache disabled to fit the 10 GB GHA quota) is documented
inline in `.github/workflows/ci.yml`'s "CACHE STRATEGY" comment.

The cache key formula covers only inputs that actually affect
`rules_foreign_cc` action keys: `MODULE.bazel{,.lock}`,
`ext/**/BUILD.bazel`, `bazel/**`, `patches/**`, `platforms/**`,
`.bazelrc`, `.bazelversion`, `flake.lock`. App/test sources are
deliberately excluded so changes there do not evict the foreign_cc
cache.

`--incompatible_strict_action_env` is set in `.bazelrc`, so it
applies to every CI invocation — preventing host env vars (e.g.
GitHub-injected `GITHUB_RUN_ID`) from poisoning Bazel's cache keys
across runs.

## Local reproduction

Every CI step invokes `just <recipe>` — there are no inline
`bazel ...` calls in any workflow. To reproduce any CI job
locally, run the same recipe inside `nix develop`:

```bash
nix develop

# Full PR test job, one arch (matches `ci.yml test` on linux-amd64)
just bazel-coverage
just hurl-test
just bazel-test-smoke

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
