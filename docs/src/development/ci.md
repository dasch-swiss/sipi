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

1. `nix build .#static-${arch}` — builds via `mkStaticBuild` in
   `flake.nix`, using Zig as the C/C++ cross-compiler targeting
   `{x86_64,aarch64}-linux-musl`. Kakadu is fetched by the FOD
   (`GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}`, `configurable-impure-env`
   enabled on the daemon).
2. Static linkage assertion: `readelf -d` must not report any
   `NEEDED` entries.
3. `nix develop --command ... cargo test` in `test/e2e-rust` with
   `SIPI_BIN=$GITHUB_WORKSPACE/result/bin/sipi` — proves the
   Nix-built static musl binary runs on a glibc host.

**`nix-macos / arm64 dylib-audit`** (`macos-14`):

- `nix build .#default` on Darwin (clang + libc++).
- `otool -L` audit: only `/usr/lib/libSystem.B.dylib` and
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
   - `publish-docker / {arch}` — builds, tests, pushes Docker images.
   - `publish-static-release / {arch}` — builds release archive via
     Nix (`nix build .#release-archive-${arch}`), uploads to GitHub
     Release, pushes debug symbols to Sentry.
   - `manifest` — multi-arch Docker manifest.
   - `docs` — mkdocs deploy.
4. `sentry` finalises the release after manifest + static publishes.

### Static artifact flow

A single per-arch `publish-static-release` job produces everything:

- `nix build .#release-archive-${arch}` emits in `result/`:
  - `sipi-v<version>-linux-${arch}.tar.gz` (binary + config + scripts + server)
  - `sipi-v<version>-linux-${arch}.tar.gz.sha256`
  - `sipi-linux-${arch}.debug` (split debug symbols)
- `gh release upload` attaches the three files to the tag release.
- `sentry-cli debug-files upload` pushes the `.debug` file to Sentry.

## Local Reproduction

```bash
# Native dev build + unit + e2e (inside nix develop)
nix develop --command bash -c "just nix-build && just nix-test && just rust-test-e2e"

# Static musl binaries (no shell needed)
nix build .#static-amd64
nix build .#static-arm64

# Validation
file result/bin/sipi
! readelf -d result/bin/sipi | grep NEEDED

# Darwin build (on macOS)
nix build .#default
otool -L result/bin/sipi
```
