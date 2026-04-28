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

Workflow: `.github/workflows/test.yml`

### Standard test matrix

- Nix/Clang test matrix runs on:
  - `ubuntu-24.04`
  - `ubuntu-24.04-arm`

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
3. `just rust-test-e2e` with `SIPI_BIN=$GITHUB_WORKSPACE/result/bin/sipi`
   — proves the Nix-built static musl binary runs on a glibc host.

**`nix-macos / arm64 dylib-audit`** (`macos-14`):

- `just nix-build-default` on Darwin (wraps `nix build .#default` —
  clang + libc++).
- `just nix-macos-dylib-audit result/bin/sipi` — `otool -L` audit;
  only `/usr/lib/libSystem.B.dylib` and
  `/System/Library/{Frameworks,PrivateFrameworks}/...` allowed.

### Forked PR behavior

`nix-static` and `nix-macos-audit` are skipped for forked PRs because
the Kakadu FOD needs `secrets.DASCHBOT_PAT`, which isn't shared with
forks. Standard CI (nix-clang) still runs.

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

Both `docker-build.yml` (PRs) and `publish.yml` (tags) build the
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
without a surrounding `nix develop`. Recipes that consume dev-shell
tools — `rust-test-e2e` (needs `cargo`) and `hurl-test` (needs `hurl`) —
are the exception and run inside `nix develop --command …`.

```bash
# Native dev build + e2e + Hurl (what test.yml nix-clang runs)
just nix-build
nix develop --command bash -c "just rust-test-e2e && just hurl-test"
just nix-coverage

# Static musl binaries (what test.yml nix-static runs)
just nix-build-static-amd64
just nix-build-static-arm64
just nix-static-linkage-verify result/bin/sipi

# Sanitizer build + tests (what sanitizer.yml runs)
just nix-build-sanitized                  # tests run in sandbox
SIPI_BIN=$PWD/result/bin/sipi nix --option filter-syscalls false develop --command bash -c "just rust-test-e2e"

# Fuzz build + run (what fuzz.yml runs)
just nix-build-fuzz
mkdir fuzz-corpus-live
just nix-run-fuzz fuzz-corpus-live 60 fuzz/handlers/corpus

# Docker image (what docker-build.yml + publish.yml publish-docker run)
just nix-docker-build-amd64               # arch-pinned image + sipi-debug
just test-smoke-ci                        # Rust testcontainer smoke tests
just nix-docker-extract-debug amd64       # produces sipi-amd64.debug for Sentry

# Release archive (what publish.yml publish-static-release runs)
just nix-build-release-archive-amd64

# Darwin build + audit (what test.yml nix-macos-audit runs)
just nix-build-default
just nix-macos-dylib-audit result/bin/sipi
```

**CI invokes justfile only:** if a CI step is not a `just <recipe>`
invocation, that's a drift signal — either the step is non-build glue
(e.g. artifact upload, Codecov upload, Sentry push) or the justfile is
missing a recipe and should grow one.
