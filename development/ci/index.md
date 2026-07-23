# CI and Release

This page documents SIPI's CI pipeline and release automation.

## Release automation (release-please)

Releases are fully automated via [release-please](https://github.com/googleapis/release-please). When commits are merged to `main`, release-please reads their [Conventional Commit](https://www.conventionalcommits.org/) prefixes to determine the SemVer bump and generate the changelog.

**Configuration files:**

- `.github/release-please/config.json` — changelog sections, release type
- `.github/release-please/manifest.json` — current version
- `.github/workflows/release-please.yml` — GitHub Actions workflow

**How commit types map to releases:**

| Prefix                                                             | SemVer effect | Changelog section        |
| ------------------------------------------------------------------ | ------------- | ------------------------ |
| `feat:`                                                            | minor bump    | Features                 |
| `fix:`                                                             | patch bump    | Bug Fixes                |
| `feat!:` / `fix!:`                                                 | major bump    | Breaking Changes         |
| `perf:`                                                            | patch bump    | Performance Improvements |
| `revert:`                                                          | patch bump    | Reverts                  |
| `docs:`, `style:`, `refactor:`, `test:`, `build:`, `ci:`, `chore:` | no bump       | hidden                   |

Correct commit prefixes are critical

A commit without a valid Conventional Commit prefix will be invisible to release-please — it won't trigger a release or appear in the changelog. See [Commit and PR Conventions](https://sipi.io/development/commit-conventions/index.md) for the full commit message schema, scope vocabulary, and what `fix:` means.

## Pull request CI

Workflow: `.github/workflows/ci.yml`. Trigger: `pull_request` plus manual `workflow_dispatch`. No Nix anywhere in this workflow — every job provisions Bazel and `just` on a plain GitHub-hosted runner via the `.github/actions/ci-setup` composite action (see "CI environment provisioning" below). Nix remains the local dev-shell provisioner only.

### `changes` gate

A `changes` job runs first and inspects the diff to decide whether the two sanitizer jobs (below) need to run: it looks for changes under `src/`, `include/`, `test/`, `fuzz/`, `bazel/`, `platforms/`, `config/`, `scripts/`, `MODULE.bazel*`, `.bazelrc`, `.bazelversion`, `BUILD.bazel`, `justfile`, `.lsan_suppressions.txt`, `ci.yml`, and `.github/actions/`. A docs-only PR skips both sanitizer jobs — a skipped required job still satisfies branch rulesets. The `test` matrix has no `needs:` on this job and starts immediately, in parallel with it. A manual `workflow_dispatch` run bypasses the filter and always runs both sanitizer jobs.

### Test matrix

The `test` job runs on three platforms — `linux-amd64`, `linux-arm64`, `darwin-arm64` — and all three legs start immediately; none of them wait on a separate lint gate. Every leg runs:

1. **Build + test** — `just bazel-test` (fastbuild, no instrumentation; unit + approval + e2e in a single Bazel invocation).
1. **Docker smoke tests (Linux only)** — `just bazel-test-smoke` builds `//src:image` as a transitive `data` dep of the `:docker_smoke` rust_test, the test loads the OCI tarball into the local Docker daemon, and runs the smoke suite against the loaded container.
1. **Docker Scout** (linux-amd64 PRs only, one leg — a single PR comment): `Docker Scout — compare to production`, `Docker Scout — CVE report (SARIF)`, and `Upload SARIF to GitHub Security`.

The standalone `lint` job is gone. Its four checks — the shttps→sipi boundary check, the differential-coverage drift guard, `bazel-rustfmt-check`, and `bazel-clippy-check` — now run as extra steps inside the `test / linux-arm64` leg, the shortest of the three legs, instead of gating the matrix from a separate job.

The differential parity gate (Rust shell vs the retained C++ oracle, `just bazel-test-differential`) also runs as an extra step, on the `linux-amd64` leg only — one platform is enough since Rust↔C++ divergences are code-level, not platform-specific.

A separate `docs` job runs `just docs-build` (mkdocs strict-mode build) on ubuntu-latest. The `docs-build` job is the gate that catches broken cross-links and stale nav entries on every PR.

### Path-gated sanitizer jobs

The jobs `sanitizer-unit` (`asan-ubsan-unit / amd64`) and `sanitizer-e2e` (`asan-ubsan-e2e / amd64`) run ASan+UBSan over the unit and e2e suites respectively, in parallel, gated by the `changes` job above. Splitting unit and e2e into two concurrent jobs saves wall-clock time; RBE dedups the shared instrumented `sipi` compile across both via the remote cache, so the split costs no extra compute. Runs on linux-x86_64 only (the macOS toolchain args omit the sanitizer header path). ASan/LSan symbol resolution (`ASAN_SYMBOLIZER_PATH`) now comes from the hermetic LLVM 22.1.7 toolchain via the `//bazel:llvm-symbolizer` alias and the `just asan-symbolizer` recipe (version-matched to clang 22.1.7) — not from a Nix `.#llvm-tools` shell.

### CI environment provisioning

Every Bazel-invoking job in `ci.yml` (and in `publish.yml`, `coverage.yml`, `fuzz.yml`) starts with the `.github/actions/ci-setup` composite action, which wraps:

- `bazelbuild/setup-bazelisk` — bazelisk on PATH (reads `.bazelversion`).
- `extractions/setup-just` — `just` on PATH.
- Optional git-LFS restore/pull (test images), when the job needs them.
- The Bazel repository cache (`actions/cache` over `~/.cache/bazel-repo`), keyed on `MODULE.bazel.lock`.
- The existing `.github/actions/bazel-rbe` composite action, for the RBE mTLS material and cross-compile flags.

`gh` is preinstalled on GitHub-hosted runners, so no setup step provisions it. `GH_TOKEN` is passed as plain step `env:` on the `just bazel-*` invocation that needs it (the `gh_release_archive` repository_rule shells out to `gh release download` for the Kakadu fetch) — there is no Nix-impure-env hack involved.

### Forked PR behavior

Every Bazel-invoking step sets `GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}` on its `env:` so the `gh_release_archive` repository_rule can authenticate. Forked PRs don't have access to `DASCHBOT_PAT`, so the Kakadu fetch fails and the build short-circuits. Internal PRs are unaffected.

### Required checks

Branch rulesets require the three `test / *` legs (`test / linux-amd64`, `test / linux-arm64`, `test / darwin-arm64`) plus the two `asan-ubsan-*` jobs. The `docs` job and the `changes` gate itself are not required checks.

## Post-merge coverage

Workflow: `.github/workflows/coverage.yml`. Trigger: `workflow_dispatch` only (manual runs) — see the workflow's header comment for why the `push: branches: [main]` trigger stays disabled under the current hermetic-llvm toolchain.

A single `linux-amd64` job runs via `ci-setup` and calls `just bazel-coverage`. The recipe's `collect_cc_coverage.sh` needs `COVERAGE_GCOV_PATH`/`LLVM_COV` on PATH; those resolve from the hermetic Bazel toolchain's `//bazel:llvm-profdata`/`//bazel:llvm-cov` aliases, not from a Nix dev shell — no `.#llvm-tools` shell is involved in CI. The combined lcov report at `bazel-out/_coverage/_coverage_report.dat` is uploaded to Codecov.

**Why split out:** Coverage instrumentation adds 1.5–2× compile overhead and slower test runtime; running it on every PR push across three platforms wasted CI minutes without commensurate signal. Per-PR coverage delta in Codecov is the trade-off — drift shows up immediately after merge instead. To restore PR-scoped signal selectively, add a `pull_request: paths: ['src/**']` trigger to this workflow.

## Tag release CI/CD

Workflow: `.github/workflows/publish.yml`. Trigger: tag push matching `v*`.

Like `ci.yml`, every Bazel-invoking job provisions its environment via the `.github/actions/ci-setup` composite action — no Nix.

Gate model:

1. `validate-docker / {amd64, arm64}` — each per-arch runner builds the Docker image via `just bazel-docker-build-${arch}` and runs `just bazel-test-smoke` against it.
1. `release-gate` — fires on `validate-docker` success.
1. Publish jobs run after the gate:
1. `publish-docker / {amd64, arm64}` — rebuilds the per-arch image, extracts the `.debug` file via `just bazel-docker-extract-debug ${arch}`, pushes via `just bazel-docker-push-${arch}`, uploads SBOM, pushes debug symbols to Sentry. It does not repeat the smoke test — `validate-docker` already validated the same commit.
1. `manifest` — needs `publish-docker`; runs `just bazel-docker-publish-manifest` (`crane index append`) to assemble the multi-arch manifest at `daschswiss/sipi:v<version>` from the two pushed per-arch digests, and tags it `:latest`. Provisioned via `imjasonh/setup-crane` + `extractions/setup-just` only — no Bazel/RBE setup, since the recipe only shells out to `crane`.
1. `sentry` — also needs `publish-docker` directly and runs in parallel with `manifest` (not after it), finalising the Sentry release.
1. `docs` — needs `release-gate`; mkdocs deploy.

## Remote build execution and caching

CI builds run on a self-hosted **NativeLink Remote Build Execution** backend: a single x86_64 worker that cross-compiles all three target arches (`linux-amd64`, `linux-aarch64`, `darwin-aarch64`), fronting a remote cache (AC + CAS, `:50051`, mTLS) and a `bazel-remote` download cache (`:50052`) for `http_archive` source tarballs. The connection, mTLS, and cross-compile flags are assembled by the [`bazel-rbe`](https://sipi.io/.github/actions/bazel-rbe) composite action and injected per workflow step — Bazel does not expand env vars in `.bazelrc`, so they live in the workflow, not the rc file. The backend-agnostic tuning flags (`--remote_download_minimal`, `--remote_local_fallback`, `--remote_timeout`, `--remote_max_connections`) live in `.bazelrc` and are safe no-ops without a remote configured.

See **[Remote build execution](https://sipi.io/development/rbe/index.md)** for the full topology, the cross-compile flag rationale, and the developer-facing workflow, and the [`ops-tf`](https://github.com/dasch-swiss/ops-tf) / [`ops-infra`](https://github.com/dasch-swiss/ops-infra) repos (private) for the VM provisioning and the NativeLink service configuration.

## Local reproduction

Every CI step invokes `just <recipe>` — there are no inline `bazel ...` calls in any workflow. CI itself runs Nix-free (see "Pull request CI" above); locally, Nix still provisions the dev shell. To reproduce any CI job locally, run the same recipe inside `nix develop`:

```
nix develop

# Full PR test job, one arch (matches `ci.yml test`)
just bazel-test
just bazel-test-smoke

# Coverage (matches `coverage.yml`; needs the .#llvm-tools shell locally)
nix develop .#llvm-tools --command just bazel-coverage

# Sanitizer build + e2e (matches the `sanitizer-unit`/`sanitizer-e2e`
# jobs folded into ci.yml)
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

**CI invokes justfile only.** If a CI step is not a `just <recipe>` invocation, that's a drift signal — either the step is non-build glue (e.g. artifact upload, Codecov upload, Sentry push) or the justfile is missing a recipe and should grow one.

## Nightly fuzz testing

A nightly fuzz workflow (`.github/workflows/fuzz.yml`) runs libFuzzer against the IIIF URL parser to find crashes and edge cases. Fuzz corpora are persisted as artifacts across runs so coverage accumulates over time.

See [Fuzzing](https://sipi.io/development/fuzzing/index.md) for details on the fuzz harness, corpus management, and how to reproduce crashes locally.
