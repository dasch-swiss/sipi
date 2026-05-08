# CI and Release

This page documents SIPI's CI pipeline and release automation.

## Release Automation (release-please)

Releases are fully automated via [release-please](https://github.com/googleapis/release-please).
When commits are merged to `main`, release-please reads their
[Conventional Commit](https://www.conventionalcommits.org/) prefixes to
determine the SemVer bump and generate the changelog.

**Configuration files:**

- `.github/release-please/config.json` — changelog sections, release type
- `.github/release-please/manifest.json` — current version
- `.github/workflows/release-please.yml` — GitHub Actions workflow

**How commit types map to releases:**

| Prefix | SemVer Effect | Changelog Section |
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

## Nightly Fuzz Testing

A nightly fuzz workflow (`.github/workflows/fuzz.yml`) runs libFuzzer against
the IIIF URL parser to find crashes and edge cases. Fuzz corpora are persisted
as artifacts across runs so coverage accumulates over time.

See [Fuzzing](fuzzing.md) for details on the fuzz harness, corpus management,
and how to reproduce crashes locally.

## Pull Request CI

Workflow: `.github/workflows/ci.yml`

### Standard test matrix

The `test` job is matrixed on `ubuntu-24.04` (amd64) and `ubuntu-24.04-arm`
(arm64). Each leg runs, in order:

1. `just nix-build` — Nix/Clang dev derivation; unit tests run in the Nix
   sandbox.
2. `just nix-test-e2e` and `just hurl-test` — Rust e2e + Hurl HTTP contract
   tests against the built binary. `nix-test-e2e` consumes the Nix-built
   `.#e2e-tests` derivation (test binaries vendored from `Cargo.lock` via
   crane), so cargo is not on the runner's PATH. `hurl-test` is the
   short-term holdout that still needs a tool on PATH (installed via
   `nix profile install nixpkgs#hurl`).
3. `just nix-coverage` and Codecov upload — amd64 only.
4. `just bazel-docker-build-${arch}` — produces the per-arch Docker
   image via Bazel `rules_oci` (`//src:image_load`) and loads it into
   the local Docker daemon as `daschswiss/sipi:latest`. Image owner:
   DEV-6346 (PR Y+4).
5. `just nix-test-smoke` — Rust smoke test from `.#smoke-test` against
   the Bazel-built image. Smoke runner stays Nix-built until DEV-6347
   (Y+5) ports it to `rules_rust`.
6. `Docker Scout — compare to production` — both arches, on PR events only.
7. `Docker Scout — CVE report (SARIF)` and `Upload SARIF to GitHub Security`
   — amd64 only, on PR events only (CVE findings are arch-independent; one
   report per build is enough).

The Docker steps reuse the Nix store populated by `just nix-build`, so
`ext/*` derivations (exiv2, libtiff, kakadu, …) are warm-cache hits.

### macOS PR check

**`nix-macos / arm64 build`** (`macos-14`):

- `just nix-build-default` on Darwin (wraps `nix build .#default` —
  clang + libc++).
- Sole CI gate for the Darwin build-completeness invariant — confirms
  `flake.nix` still evaluates and `.#default` builds on macOS arm64.

### Forked PR behavior

`nix-macos-build` is skipped for forked PRs because the Kakadu FOD
needs `secrets.DASCHBOT_PAT`, which isn't shared with forks. The
standard CI (`nix-clang`) job still runs on forked PRs, but its
`docker/login-action` step is gated to skip on forks (Docker Hub
credentials are unavailable to fork runners).

## Tag Release CI/CD

Workflow: `.github/workflows/publish.yml`

Trigger: tag push matching `v*`.

Gate model:
1. `validate-docker` must pass.
2. `release-gate` fires on `validate-docker` success.
3. Publish jobs run in parallel after the gate:
   - `publish-docker / {arch}` — builds the Docker image via
     `just bazel-docker-build-${arch}` (`bazel run :image_load --stamp`),
     extracts the `.debug` file via
     `just bazel-docker-extract-debug ${arch}` (builds
     `:sipi_debug_layout`), runs smoke tests, pushes the image via
     `just bazel-docker-push-${arch}` (`bazel run :image_push_${arch}`),
     uploads SBOM, pushes debug symbols to Sentry.
   - `manifest` — `just bazel-docker-publish-manifest` runs
     `crane index append` to assemble the multi-arch manifest at
     `daschswiss/sipi:v<version>` from the two pushed per-arch digests.
   - `docs` — mkdocs deploy.
4. `sentry` finalises the release after the manifest job completes.

### Docker image build (PRs and tag releases)

Both `ci.yml` (PRs) and `publish.yml` (tags) build the Docker image
entirely through Bazel `rules_oci` (`//src:image`, DEV-6346). There is
no GHA Docker-layer cache (`type=gha,mode=max`); there is no
`Dockerfile` — `src/BUILD.bazel` is the single source of truth.
Multi-arch manifest assembly happens via `crane index append` on a
coordinator job (per-arch CI runners produce per-arch images, then a
single coordinator stitches the digests into a multi-arch manifest).

The image's runtime contract: built on `gcr.io/distroless/base-debian12`
(pinned by digest), runs sipi as root (NFS uid coordination tracked in
DEV-5920), `TZ=Europe/Zurich`, `LC_ALL=C.UTF-8`, OCI labels stamped
from `STABLE_GIT_COMMIT` + `STABLE_SIPI_VERSION`. The image ships
HEALTHCHECK-agnostic (HEALTHCHECK is a Docker schema-2 extension, not
part of OCI); compose-level healthcheck is in INFRA-1226.

## Local Reproduction

Every CI step invokes `just <recipe>` — no inline cmake or `nix build`
calls. To reproduce any CI job locally, run the same recipe. With the
Determinate Systems native-linux-builder available to macOS authors,
Linux-target recipes (`bazel-build-fuzz`, `bazel-build-sanitized`) run
locally without a CI round-trip.

`just nix-build*` recipes wrap `nix build`, so CI invokes them directly
without a surrounding `nix develop`. The Rust e2e and Docker smoke test
binaries are also Nix-built (via `.#e2e-tests` and `.#smoke-test` —
defined in `nix/rust-tests.nix`), so `just nix-test-e2e` and
`just nix-test-smoke` need only `just` on PATH. The remaining holdout
is `hurl-test`, which still shells out to `hurl` (installed at CI step
time via `nix profile install nixpkgs#hurl`).

```bash
# Native dev build + e2e + Hurl + coverage + Docker image + smoke
# (the full `ci.yml test` job for one arch)
just nix-build
just nix-test-e2e                         # binaries from .#e2e-tests
just hurl-test                            # still needs `hurl` on PATH
just nix-coverage
just bazel-docker-build-amd64             # or bazel-docker-build-arm64 on aarch64 host
just nix-test-smoke                       # binary from .#smoke-test

# Sanitizer build + e2e (what sanitizer.yml runs)
just bazel-build-sanitized                # bazel build --config=asan --config=ubsan //src:sipi
SIPI_BIN=$PWD/bazel-bin/src/sipi \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:log_path=/tmp/asan-e2e \
  LSAN_OPTIONS=suppressions=$PWD/.lsan_suppressions.txt \
  just nix-test-e2e

# Fuzz build + run (what fuzz.yml runs; linux-x86_64 only)
just bazel-build-fuzz
mkdir fuzz-corpus-live
just bazel-run-fuzz fuzz-corpus-live 60 fuzz/handlers/corpus

# Docker image with split debug symbols (what publish.yml publish-docker
# runs). The PR-time `ci.yml test` job builds the same image inline; this
# block only adds the Sentry-bound `bazel-docker-extract-debug` step plus
# the per-arch push.
just bazel-docker-build-amd64             # build + load amd64 image
just nix-test-smoke                       # smoke test against loaded image
just bazel-docker-extract-debug amd64     # produces sipi-amd64.debug for Sentry
just bazel-docker-push-amd64              # push image to Docker Hub
# After both per-arch pushes, on the coordinator runner:
just bazel-docker-publish-manifest        # crane index append → multi-arch manifest

# Darwin build (what ci.yml nix-macos-build runs)
just nix-build-default
```

**CI invokes justfile only:** if a CI step is not a `just <recipe>`
invocation, that's a drift signal — either the step is non-build glue
(e.g. artifact upload, Codecov upload, Sentry push) or the justfile is
missing a recipe and should grow one.
