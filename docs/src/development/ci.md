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

- Existing Nix/GCC test matrix still runs on:
  - `ubuntu-24.04`
  - `ubuntu-24.04-arm`

### Zig/static PR checks (native per-arch Docker-in-Ubuntu)

Each architecture gets a combined build+test job on its native runner. The build
runs inside an Alpine Docker container, then tests run on the bare Ubuntu host —
proving the static musl binaries are portable.

**`zig-static / {arch}`** — combined job per architecture:

1. **Host (Ubuntu):** JS actions run checkout and setup-zig.
2. **Alpine Docker:** `docker run alpine:3.21` with source bind-mounted:
    - Installs build prerequisites via `apk`.
    - Zig binary (statically linked) is bind-mounted from host.
    - CMake configure + build produces a static ELF binary.
3. **Host (Ubuntu):** Verification and testing:
    - Static linkage verification (`ldd`, `readelf -d`).
    - Unit tests (GoogleTest executables run directly).
    - E2e dependencies installed via `apt-get` and `pip3`.
    - Full e2e test suite (`test/e2e`).

This proves Alpine-built static binaries run on a glibc host that had no part
in building them, and each architecture builds and tests natively.

**`zig-macos / arm64 dylib-audit`:**

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
1. `validate-static / {arch}` builds, tests, and packages each architecture
   natively (same Docker-in-Ubuntu pattern as PR workflow).
2. `validate-docker` must pass.
3. `release-gate` requires `validate-docker` and `validate-static`.
4. Publish side effects run only after `release-gate` succeeds.

### Static artifact flow

Each architecture is built, tested, and packaged in its own `validate-static`
job:

- Build static binary (native on each arch's runner).
- Verify static linkage.
- Run unit tests and e2e tests.
- Split debug symbols (`objcopy --only-keep-debug`).
- Strip binary.
- Add debug link (`objcopy --add-gnu-debuglink`).
- Package `.tar.gz` + `.sha256`.
- Upload `static-linux-release-{arch}` artifact.

### Release attachment and symbols

- Static archives/checksums are attached to the existing tag release.
- Static debug symbols are uploaded to Sentry per architecture.
- Docker debug symbols and SBOM flow continue in parallel.

## Local Reproduction

### Zig local workflow (native build + e2e on host)

```bash
make zig-build-local
make zig-test
make zig-test-e2e
```

### Static Zig build in Docker (mirrors CI build job)

```bash
make zig-static-docker-arm64   # Alpine build + ctest for arm64
make zig-static-docker-amd64   # Alpine build + ctest for amd64
```

These targets mirror the `validate-static` CI job: Alpine 3.21 container,
Zig toolchain, cmake build, and unit tests. They do **not** run e2e tests.

The CI's portability proof (e2e on bare Ubuntu) is not reproduced locally
because the Docker targets produce a Linux ELF binary that cannot run on
macOS. On a Linux workstation you could extract the binary and run e2e
manually, but this is not wrapped in a Make target — CI is the authoritative
portability check.

### Linux static validation commands

```bash
# (local Makefile targets use build-static/; CI uses build/)
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
