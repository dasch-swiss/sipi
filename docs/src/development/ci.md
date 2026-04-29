# CI and Release

This page documents SIPI's CI pipeline, release automation, and the Zig/static
build hardening that runs in parallel with Docker during rollout.

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

## Scope

- Keep Docker publishing and Zig/static artifacts in parallel.
- Enforce Zig/static validation as required gates before release side effects.
- Produce fully static Linux binaries (`x86_64-linux-musl`, `aarch64-linux-musl`).
- Enforce strict macOS Zig dylib policy (`/usr/lib/libSystem.B.dylib` only).

## Zig Version and Build Policy

- Zig is pinned to `0.15.2` in CI workflows.
- Linux static targets:
  - `x86_64-linux-musl` (amd64)
  - `aarch64-linux-musl` (arm64)
- CI uses **native per-arch builds via Docker-in-Ubuntu**: each architecture
  gets its own runner (`ubuntu-24.04` for amd64, `ubuntu-24.04-arm` for arm64).
  JS actions (checkout, setup-zig, upload-artifact) run on the bare Ubuntu host.
  The build itself runs inside `docker run alpine:3.21` with the source
  bind-mounted — Alpine is required because Zig has a bug where it doesn't
  ignore `/usr/include` even with `-target`, and Ubuntu's glibc headers would
  contaminate musl builds.
- LTO is disabled for musl static builds (`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF`).

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
4. `just nix-docker-build-${arch}` — produces the per-arch Docker image via
   `pkgs.dockerTools.streamLayeredImage` and loads it into the local Docker
   daemon.
5. `just nix-test-smoke` — Rust smoke test from `.#smoke-test` against the
   loaded image.
6. `Docker Scout — compare to production` — both arches, on PR events only.
7. `Docker Scout — CVE report (SARIF)` and `Upload SARIF to GitHub Security`
   — amd64 only, on PR events only (CVE findings are arch-independent; one
   report per build is enough).

The Docker steps reuse the Nix store populated by `just nix-build`, so
`ext/*` derivations (exiv2, libtiff, kakadu, …) are warm-cache hits.

### Static and macOS PR checks

Both static Linux and macOS validation run through the Nix flake —
Zig-in-Nix for musl cross-compile and Nix's default clang/libc++
stdenv for Darwin.

**`nix-static / {arch}`** (`ubuntu-24.04`, `ubuntu-24.04-arm`):

1. `just nix-build-static-${arch}` — wraps `nix build .#static-${arch}`,
   which runs `mkStaticBuild` in `flake.nix` using Zig as the C/C++
   cross-compiler targeting `{x86_64,aarch64}-linux-musl`. Kakadu is
   fetched by the FOD (`GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}`,
   `configurable-impure-env` enabled on the daemon).
2. `just nix-static-linkage-verify result/bin/sipi` — static linkage
   assertion via `readelf -d`; must not report any `NEEDED` entries.
3. `just nix-test-e2e` with `SIPI_BIN=$GITHUB_WORKSPACE/result/bin/sipi`
   — proves the Nix-built static musl binary runs on a glibc host. Uses
   the `.#e2e-tests` derivation, so cargo is not required on the runner.

**`nix-macos / arm64 dylib-audit`** (`macos-14`):

- `just nix-build-default` on Darwin (wraps `nix build .#default` —
  clang + libc++).
- `just nix-macos-dylib-audit result/bin/sipi` — `otool -L` audit;
  only `/usr/lib/libSystem.B.dylib` and
  `/System/Library/{Frameworks,PrivateFrameworks}/...` allowed.

### Forked PR behavior

`nix-static` and `nix-macos-audit` are skipped for forked PRs because
the Kakadu FOD needs `secrets.DASCHBOT_PAT`, which isn't shared with
forks. The `test` job still runs on forked PRs, but its
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
     `just nix-docker-build-${arch}` (wraps
     `nix build .#packages.<arch>-linux.docker-stream
            .#packages.<arch>-linux.sipi-debug` in a single call),
     extracts the `.debug` file via `just nix-docker-extract-debug`,
     runs smoke tests, pushes the image, uploads SBOM, pushes debug
     symbols to Sentry.
   - `publish-static-release / {arch}` — builds release archive via
     `just nix-build-release-archive-${arch}` (wraps
     `nix build .#release-archive-${arch}`), uploads to GitHub
     Release, pushes debug symbols to Sentry.
   - `manifest` — multi-arch Docker manifest combining the two
     pushed per-arch images.
   - `docs` — mkdocs deploy.
4. `sentry` finalises the release after manifest + static publishes.

### Docker image build (PRs and tag releases)

Both `ci.yml` (PRs) and `publish.yml` (tags) build the
Docker image entirely through Nix. The image is produced by
`pkgs.dockerTools.streamLayeredImage` from the same derivation graph
as every other build artifact, so `Cachix` substitutes `ext/*`
(exiv2, libtiff, kakadu, …) instead of recompiling them on every
run. There is no GHA Docker-layer cache (`type=gha,mode=max`); there
is no `Dockerfile` — `flake.nix` is the single source of truth.

The image's runtime contract: runs as `root`, `tini` as PID 1,
HEALTHCHECK against `/health` on port 1024, `TZ=Europe/Zurich`,
`LC_ALL=C.UTF-8`, OCI labels populated from `flake.lock`. `C.UTF-8`
is sufficient because sipi has no code path that depends on locale
categories beyond `LC_CTYPE` (which `C.UTF-8` covers), and operators
do not override `LC_ALL` at runtime. See
[`nix.md`](nix.md#docker-image) for the derivation details.

### Static artifact flow

A single per-arch `publish-static-release` job produces everything:

- `just nix-build-release-archive-${arch}` (wraps `nix build .#release-archive-${arch}`) emits in `result/`:
  - `sipi-v<version>-linux-${arch}.tar.gz` (binary + config + scripts + server)
  - `sipi-v<version>-linux-${arch}.tar.gz.sha256`
  - `sipi-linux-${arch}.debug` (split debug symbols)
- `gh release upload` attaches the three files to the tag release.
- `sentry-cli debug-files upload` pushes the `.debug` file to Sentry.

## Local Reproduction

Every CI step invokes `just <recipe>` — no inline cmake or `nix build`
calls. To reproduce any CI job locally, run the same recipe. With the
Determinate Systems native-linux-builder available to macOS authors,
Linux-target recipes (`nix-build-static-*`, `nix-build-sanitized`,
`nix-build-fuzz`, `nix-build-release-archive-*`) run locally without
a CI round-trip.

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
just nix-docker-build-amd64               # or -arm64 on aarch64 host
just nix-test-smoke                       # binary from .#smoke-test

# Static musl binaries (what ci.yml nix-static runs)
just nix-build-static-amd64
just nix-build-static-arm64
just nix-static-linkage-verify result/bin/sipi

# Sanitizer build + tests (what sanitizer.yml runs)
just nix-build-sanitized                  # tests run in sandbox
SIPI_BIN=$PWD/result/bin/sipi just nix-test-e2e

# Fuzz build + run (what fuzz.yml runs)
just nix-build-fuzz
mkdir fuzz-corpus-live
just nix-run-fuzz fuzz-corpus-live 60 fuzz/handlers/corpus

# Docker image with split debug symbols (what publish.yml publish-docker
# runs). The PR-time `ci.yml test` job builds the same image inline; this
# block only adds the Sentry-bound `nix-docker-extract-debug` step.
just nix-docker-build-amd64               # arch-pinned image + sipi-debug
just nix-test-smoke                       # smoke test against loaded image
just nix-docker-extract-debug amd64       # produces sipi-amd64.debug for Sentry

# Release archive (what publish.yml publish-static-release runs)
just nix-build-release-archive-amd64

# Darwin build + audit (what ci.yml nix-macos-audit runs)
just nix-build-default
just nix-macos-dylib-audit result/bin/sipi
```

**CI invokes justfile only:** if a CI step is not a `just <recipe>`
invocation, that's a drift signal — either the step is non-build glue
(e.g. artifact upload, Codecov upload, Sentry push) or the justfile is
missing a recipe and should grow one.
