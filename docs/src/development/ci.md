# CI and Release

This page documents SIPI's CI and release hardening for Zig/static builds and
how it runs in parallel with Docker during rollout.

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
- Native-per-architecture runners are used (no single-runner cross build).
- LTO is disabled for musl static builds (`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF`).

## Pull Request CI

Workflow: `.github/workflows/test.yml`

### Standard test matrix

- Existing Nix/GCC test matrix still runs on:
  - `ubuntu-24.04`
  - `ubuntu-24.04-arm`

### Required Zig/static PR checks

- `zig-static-linux / amd64`
- `zig-static-linux / arm64`
- `zig-macos / arm64 dylib-audit`

Linux Zig/static jobs run:
- Release configure/build with Zig toolchain.
- Unit tests (`ctest`).
- Full e2e tests (`test/e2e`).
- Static verification:
  - `ldd` must report static.
  - `readelf -d build-static/sipi` must contain no `NEEDED` entries.

macOS Zig job runs:
- Native Release Zig build.
- `otool -L` audit on `build-zig-macos/sipi`.
- Exactly one allowed dependency:
  - `/usr/lib/libSystem.B.dylib`

### Forked PR behavior

Zig static jobs are intentionally skipped for forked PRs because private inputs
(for example Kakadu/private dependency paths) are not available there.
Standard CI behavior remains active for forks.

## Tag Release CI/CD

Workflow: `.github/workflows/publish.yml`

Trigger:
- Tag push matching `v*`

Gate model:
1. `validate-docker` and `validate-static` must pass.
2. `release-gate` requires both validations.
3. Publish side effects run only after `release-gate` succeeds.

### Static artifact flow

For each Linux architecture:
- Build static binary.
- Split debug symbols (`objcopy --only-keep-debug`).
- Strip binary.
- Add debug link (`objcopy --add-gnu-debuglink`).
- Package `.tar.gz` + `.sha256`.

### Release attachment and symbols

- Static archives/checksums are attached to the existing tag release.
- Static debug symbols are uploaded to Sentry per architecture.
- Docker debug symbols and SBOM flow continue in parallel.

## Local Reproduction

### Zig local workflow

```bash
make zig-build-local
make zig-test
make zig-test-e2e
```

### Linux static validation commands

```bash
file build-static/sipi
ldd build-static/sipi
readelf -d build-static/sipi | grep NEEDED
```

Expected:
- `ldd` indicates static.
- `readelf` returns no `NEEDED` entries.

### macOS dylib audit command

```bash
otool -L build-zig-macos/sipi
```

Expected:
- Only `/usr/lib/libSystem.B.dylib`.

