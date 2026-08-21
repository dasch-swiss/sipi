# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> [!IMPORTANT]
> **No commit or push without the lint gates passing.** For any Rust change
> (`src/server-rs`, `src/cli-rs`, `test/e2e`), run **`just bazel-rustfmt-check`**
> and **`just bazel-clippy-check`** before committing (fix with `just bazel-rustfmt`).
> Both are CI gates (`-Dwarnings`, run as `rules_rust` lint aspects on the `test`
> job) that are **not** exercised by `just bazel-test`, `bazel-test-e2e`, or
> `bazel-coverage` — so a fully green local test run can
> still fail CI on a formatting or clippy finding. This is a hard rule, not a
> suggestion.

## Project Overview

SIPI (Simple Image Presentation Interface) is a multithreaded, high-performance, IIIF-compatible media server written in C++23. It implements IIIF Image API 3.0 and provides efficient image format conversions while preserving metadata. The server can be used both as a command-line tool and as a web server with Lua scripting support.

## Domain Model

Read these before reasoning about names, boundaries, or architectural decisions:

- [`UBIQUITOUS_LANGUAGE.md`](UBIQUITOUS_LANGUAGE.md) — canonical SIPI glossary. Defines Image vs Bitstream, the IIIF pipeline terms (Region / Size / Rotation / Quality / Format / Decode level / Canonical URL / Cache key), Preservation metadata, the three Lua entry points, the seven Permission types, and more. Use these terms in code comments, commit messages, and PR descriptions; aliases listed there are *avoid*.
- [`CONTEXT.md`](CONTEXT.md) — SIPI is the IIIF subdomain implementation of the **Access Area** bounded context in the wider [`dsp-repository`](https://github.com/dasch-swiss/dsp-repository) system. Defines the Published Language inherited from Access Area (**Preservation File** / **Service File** / **Access File**) and points at the SIPI-local glossary.
- [`docs/adr/`](docs/adr/) — architectural decision records. Start with [`0020-oracle-removal.md`](docs/adr/0020-oracle-removal.md) (the C++ oracle is removed; the Rust shell is the sole server — completing the strangler-fig migration that [`0013-shttps-as-internal-module.md`](docs/adr/0013-shttps-as-internal-module.md) prepared) and [`0017-extensibility-lua-and-rust.md`](docs/adr/0017-extensibility-lua-and-rust.md) (Lua and Rust extensions are both first-class, permanently).
- Architecture map: see [`ARCH-MAP.md`](ARCH-MAP.md); load on demand for blast-radius and boundary questions (never auto-loaded).

## Build System and Common Commands

All targets are in a single `justfile`. Run `just` for a complete list.
For full build instructions, see [`docs/src/development/building.md`](docs/src/development/building.md) and [`docs/src/development/bazel.md`](docs/src/development/bazel.md).

**Build reproducibility invariant:** Bazel is the build system; Nix provisions only the dev shell. Every build/test/coverage step in CI invokes one of the `just bazel-*` recipes — no inline `bazel` calls, no `nix build` calls. Bazel's own incremental rebuild IS the inner-loop edit/rebuild cycle (`just bazel-build` after a single-file edit completes in seconds via the action cache). CI runs on a self-hosted NativeLink Remote Build Execution backend — remote cache (AC + CAS) + executor on `:50051` (mTLS), plus a `bazel-remote` download cache on `:50052` for `http_archive` tarballs. The connection, mTLS, and cross-compile flags are assembled by the `.github/actions/bazel-rbe` composite action and injected per CI step; see [`docs/src/development/rbe.md`](docs/src/development/rbe.md). The backend (`dasch-remotebuild-prod-01`) is DaSCH-hosted and defined outside this repo: the libvirt VM in [`ops-tf`](https://github.com/dasch-swiss/ops-tf) (`live/virt-03/prod/vms/`), the NativeLink/bazel-remote service config in [`infra`](https://github.com/dasch-swiss/infra) (`OS/ansible/roles/dasch.nativelink`). Local dev never contacts the backend — the remote flags are injected only by CI workflow steps; Bazel doesn't expand env vars in `.bazelrc`, so they live in the workflow, not the rc file. **RBE write pressure** (why the store disk writes terabytes/day, that it is worker-side materialization and not client uploads, and the client-side levers that reduce it) is analyzed once and for all in [`docs/src/development/rbe-write-pressure.md`](docs/src/development/rbe-write-pressure.md) — read it before re-investigating RBE disk-write behavior. Every `ci.yml` test leg attaches its Bazel BEP JSON as a `bep-*` artifact for offline analysis.

**Build completeness invariant:** every build target must succeed on every supported platform — macOS (darwin-aarch64), linux-x86_64, and linux-aarch64. The Docker image (`//src:image` via `rules_oci`) is Linux-only — `bazel-docker-build-{amd64,arm64}` is gated by host-CPU `target_compatible_with`. The sanitized variant is `bazel build --config=asan --config=ubsan //src/cli:sipi`. CI runs the test matrix on all three platforms, so a green CI run verifies macOS as well as Linux. Before shipping any change to `flake.nix`, `MODULE.bazel`, `BUILD.bazel`, or a `justfile` build recipe, run `just bazel-build` and `just bazel-coverage` locally on macOS at minimum.

**First-time setup:** Bazel builds (including `just bazel-docker-build-${arch}`) fetch Kakadu directly via Bazel's `gh_release_archive` repository_rule (no `vendor/` step). Requires `gh auth login` and `dasch-swiss` org membership. See [`docs/src/development/kakadu.md`](docs/src/development/kakadu.md).

**ICC determinism invariant:** [`Icc::iccBytes()`](src/metadata/icc.cpp) is the single chokepoint that converts an `cmsHPROFILE` into raw bytes for codec consumption — every TIFF, JPEG, PNG, and JP2 emission funnels through it. Any new format handler must route through `iccBytes()`; bypassing it (calling `cmsSaveProfileToMem` directly) breaks the approval-test gate. Approval tests run with `SOURCE_DATE_EPOCH=946684800` and `SIPI_WORKSPACE_ROOT="."` injected by `test/approval/BUILD.bazel` so the wall-clock-stamped ICC creation date is overwritten with a fixed value and goldens stay byte-deterministic. Production never sets the env var; deployed binaries continue to embed wall-clock-stamped ICC headers. See [`docs/adr/0002-icc-profile-determinism-test-only.md`](docs/adr/0002-icc-profile-determinism-test-only.md).

### Quick Reference

```bash
# Build sipi (fastbuild — fast incremental for inner-loop edits)
just bazel-build                 # bazel build --stamp //src/cli:sipi
just bazel-build --config=release  # production build (-c opt + hardening; matches Docker image)
just bazel-build --config=asan   # ASan+UBSan; same flag form for ad-hoc variants
just bazel-build-server          # bazel build //src/cli-rs:sipi (the Rust shell binary; wraps //src/server-rs:lib)

# Tests
just bazel-test-unit             # bazel test //test/unit/...  (12 components)
just bazel-test-approval         # bazel test //test/approval:approvaltests
just bazel-test-e2e              # Rust e2e tests via rules_rust
just bazel-test-smoke            # Docker smoke test (OCI tarball loaded by the test)

# Coverage (canonical CI build — what ci.yml invokes on every PR)
just bazel-coverage              # unit + approval + e2e under instrumentation; lcov
                                 # at bazel-out/_coverage/_coverage_report.dat

# Rust formatting + rust-analyzer
just bazel-rustfmt               # format the Rust crates per rustfmt.toml (rules_rust runner)
just bazel-rustfmt-check         # CI gate: rustfmt --check via the rules_rust rustfmt aspect
just bazel-rust-project          # (re)generate rust-project.json for rust-analyzer (git-ignored)

# Run / debug
just run                         # run sipi with the localdev config
just valgrind                    # run sipi under Valgrind

# Microbenchmarks (local dev loop, never CI-gated)
just bench <tier>                # tier ∈ parse|decode|process|encode; -c opt build + direct exec
just bench-compare before after  # U-test deltas + geomean via //tools/benchmark:compare

# Sanitizer
just bazel-build-sanitized       # bazel build --config=asan --config=ubsan //src/cli:sipi  (ci.yml sanitizer job)

# Docker (Bazel rules_oci)
just bazel-docker-build-amd64            # build + load amd64 image as daschswiss/sipi:latest (CI on amd64 runner)
just bazel-docker-build-arm64            # build + load arm64 image (CI on arm64 runner)
just bazel-docker-push-amd64             # push amd64 image as :v<version>-amd64 + :latest-amd64
just bazel-docker-push-arm64             # push arm64 image as :v<version>-arm64 + :latest-arm64
just bazel-docker-publish-manifest       # crane index append → daschswiss/sipi:v<version> multi-arch
just bazel-docker-extract-debug <arch>   # surface sipi-<arch>.debug for sentry-cli upload

# Documentation
just docs-serve                  # serve docs locally
```

### Inner-loop development (incremental rebuilds)

`bazel build` IS the inner loop. The first build is slow (cold action cache,
the native `cc_library` deps kakadu/libtiff/exiv2/etc compile from source), but
subsequent edits to a single `.cpp` file rebuild in seconds — Bazel's per-action cache only
re-runs the affected compile + link.

```bash
nix develop                                   # dev shell with build deps + bazelisk
just bazel-build                              # first build: cold action cache
./bazel-bin/src/cli/sipi server --config config/sipi.localdev-config.lua
# subsequent edits:
just bazel-build                              # incremental, sub-second through link
```

`just run` wraps this: it depends on `bazel-build` and starts sipi with the
localdev config in one step.

## High-Level Architecture

**Production surface.** The **Rust axum shell** (`src/server-rs` + `src/cli-rs`, built by `just bazel-build-server`) is the production server. It drives the C++ **image engine** (`libsipi`) over the FFI seam in `src/server-rs/src/ffi.rs`. There is no C++ server: the retained shttps oracle was removed (ADR-0020); the C++ `//src/cli:sipi` binary now provides only the offline verbs (`convert`/`verify`/`query`/`compare`/`health`). In production Rust code, describe current behavior on its own terms; do not frame it relative to the removed C++ server / oracle / transport (referencing the C++ *engine*, the FFI callee, is fine). See [`CONVENTIONS.md` § Production surface](CONVENTIONS.md).

### Core Components

| Component | Path | Purpose |
|-----------|------|---------|
| Main Application | `src/cli/cli_app.cpp` | CLI11 arg parsing + offline-verb dispatch, behind the `sipi_cli_main` FFI entry; `src/cli-rs/src/main.rs` owns `main` and Sentry init |
| SipiImage | `src/SipiImage.h` | Image processing: TIFF, JP2, PNG, JPEG; metadata (EXIF, IPTC, XMP); ICC profiles |
| Rust HTTP shell | `src/server-rs/` | The production server: axum routes, IIIF endpoints, caching, Lua request-shaping; drives the C++ engine over FFI |
| IIIF Parser | `src/iiifparser/cpp/` (C++ engine) / `src/iiifparser/rust/` (production, `//src/iiifparser/rust:iiif_parser`) | IIIF URL parsing: identifier, region, size, rotation, quality/format. Production parses in Rust and emits domain types; `server-rs` flattens them into the seam struct the C++ engine consumes (ADR-0021) |
| Format Handlers | `src/formats/` | SipiIO base class + SipiIOTiff, SipiIOJ2k, SipiIOJpeg, SipiIOPng |
| Caching | `src/SipiCache.h` | File-based LRU cache with dual-limit eviction (size + file count), crash recovery |
| Metrics | `src/observability/metrics.h` | Metrics singleton (`Sipi::observability::Metrics`) — plain atomic counters/gauges; scalar fields cross the FFI seam as `SipiMetricsSnapshot` and export over OTLP via `src/server-rs/src/metrics.rs` |
| Memory Budget | `src/SipiMemoryBudget.h` | Lock-free decode memory budget with RAII guard — prevents OOM from concurrent large decodes |
| Lua Runtime | `src/scripting/rust/` | Rust-hosted mlua runtime (ADR-0023): hardened per-request VM (stdlib whitelist, memory cap, deadline), bytecode cache, all `server.*`/`SipiImage`/sqlite bindings, Lua-flavor config parse |

### Image Processing Pipeline

1. HTTP server receives IIIF URL
2. IIIF parameters extracted and validated
3. Cache check (SipiCache)
4. Image loaded via appropriate SipiIO handler
5. Processing: region, scaling, rotation, quality
6. Serve processed image or write to cache

### Configuration

- Main config: `config/sipi.config.lua`
- Test config: `config/sipi.test-config.lua`
- Local dev config: `config/sipi.localdev-config.lua`
- Lua scripts: `scripts/` directory

### Dependencies

**External Libraries.** Each is either a BCR `bazel_dep` (drop-in) or a
hand-written native `cc_library` over an `http_archive`/release fetch
(`bazel/<lib>.BUILD.bazel`) — never `rules_foreign_cc` (see
[`docs/adr/0015-native-cc_library-over-foreign_cc.md`](docs/adr/0015-native-cc_library-over-foreign_cc.md)).
BCR drop-ins: libpng, libjpeg_turbo, libwebp, libdeflate, zlib, bzip2, xz, zstd,
sqlite3, libexpat, libmagic, Lua, curl, OpenSSL,
protobuf. Native `cc_library`: libtiff (codecs re-enabled + JBIG via jbigkit),
exiv2, lcms2, jansson, jbigkit, and Kakadu (requires license).

**System Dependencies:** Threads (pthread), iconv (macOS only).

### Important Files

- `MODULE.bazel` — Bazel module + BCR `bazel_dep`s + `http_archive` pins for the native-`cc_library` deps
- `BUILD.bazel` (root + `src/`, `test/`) — target graph; `bazel/<lib>.BUILD.bazel` — native dep build files
- `justfile` — all build targets (run `just` to list)
- `flake.nix` — dev-shell only (`default` clang+libc++, `gcc` diagnostic)
- `version.txt` — version information; baked in via `tools/workspace_status.sh`
  + `expand_template` substitution into `include/SipiVersion.h.in`

## Testing

For the authoritative testing strategy (pyramid, layer definitions, decision tree, IIIF coverage matrix, feature inventory), see [`docs/src/development/testing-strategy.md`](docs/src/development/testing-strategy.md).

For test framework details (how to run tests, directory layout, adding tests), see [`docs/src/development/developing.md`](docs/src/development/developing.md).

- **Unit tests** (`test/unit/`): GoogleTest — `just bazel-test-unit` (or via `bazel-coverage` in CI)
- **Approval tests** (`test/approval/`): snapshot-based regression — `just bazel-test-approval` (or via `bazel-coverage` in CI). `SOURCE_DATE_EPOCH=946684800` and `SIPI_WORKSPACE_ROOT="."` are injected by `test/approval/BUILD.bazel`.
- **E2E tests** (`test/e2e/`): Rust (reqwest + `rules_rust`'s hermetic rustc). Run via `just bazel-test-e2e`, or a single target with `bazel test //test/e2e:<name> --test_output=streamed`.
- **Smoke tests** (`test/e2e/tests/docker_smoke.rs`): against Docker image. Run via `just bazel-test-smoke` — the `:docker_smoke` rust_test consumes the OCI tarball from `//src:image_load` and `docker load`s it before probing endpoints.

Run a single unit-test target with `bazel test //test/unit/<component>:<component>_test --test_output=streamed`.

**Hot-path changes require a benchmark.** Before changing any image decode/encode hot path (`src/formats/*`, `SipiImage` read/write, `iiifparser`), a Google Benchmark microbench must exist, co-located with the module (per ADR-0003) as a manual-tagged `*_benchmark.cpp` `cc_binary`. Add one if it doesn't. Justify the change with a before/after `just bench` + `just bench-compare` run on the same `-c opt` binary and machine (trust a delta only if the U-test is green AND the median shift exceeds the baseline CV; sub-3% is noise). See [`docs/src/development/benchmarking.md`](docs/src/development/benchmarking.md).

## CI, Release, and Commit Messages

For CI pipeline details (Docker publishing, release automation), see [`docs/src/development/ci.md`](docs/src/development/ci.md).

**Releases are automated via release-please.** Correct [Conventional Commit](https://www.conventionalcommits.org/) prefixes are required — they drive SemVer bumps and changelog generation. See [`docs/src/development/ci.md`](docs/src/development/ci.md) for the full prefix-to-release mapping.

Valid types (exactly eight): `feat`, `fix`, `perf`, `refactor`, `docs`, `test`, `build`, `chore`. `revert`, `style`, and `ci` are **not** valid — use `chore(ci): ...`, fold formatting in, and `fix`/`chore` for reverts. Breaking changes use `!` suffix: `feat(scope)!: ...`. **A scope is mandatory** (`type(scope): subject`, no catch-all) — a module name from [`CONVENTIONS.md` § Module Layout](CONVENTIONS.md); if none fits, ask the maintainer before coining a new one. A CI gate (`commitlint-rs`, `just commit-lint`) enforces this. Full schema single-sourced in [`docs/src/development/commit-conventions.md`](docs/src/development/commit-conventions.md).

**A PR lands as one commit by default.** Rebase-merge puts every branch commit on `main` verbatim — there is no squash safety net. Clean up the branch before merge; split into multiple commits only when the work is genuinely several independent, self-contained changes. A `fix:` corrects behavior already on `main`; a bug introduced earlier in the same branch is folded into its introducing commit, never a standalone `fix:`.

For the git workflow, commit message schema, scope vocabulary, and PR description format (all single-sourced), see [`docs/src/development/commit-conventions.md`](docs/src/development/commit-conventions.md). PRs are scaffolded by [`.github/PULL_REQUEST_TEMPLATE.md`](.github/PULL_REQUEST_TEMPLATE.md).

**Code review:** Use [`docs/src/development/reviewer-guidelines.md`](docs/src/development/reviewer-guidelines.md) as the review checklist for all PRs.

## C++ Style Guide

Follow [`docs/src/development/cpp-style-guide.md`](docs/src/development/cpp-style-guide.md) for all new and modified C++ code. Key rules:

- **Ownership:** No raw owning `new`/`delete` — use `std::unique_ptr`, `std::make_unique`, or value semantics
- **Error handling:** `std::expected<T, E>` for fallible operations, exceptions for truly unrecoverable conditions
- **Input validation:** Validate all user input at HTTP handler boundaries before any file I/O or header construction
- **`[[nodiscard]]`:** Apply to all functions where ignoring the return value is a bug
- **const correctness:** Apply `const` everywhere it is valid
- **Legacy code:** When modifying existing code, apply modernization opportunistically (see style guide Section 4)

## Infrastructure boundary

**All infrastructure changes are operator-only. Claude Code has read-only access to infrastructure; the maintainer applies every change. No exceptions.**

- NEVER run `tofu apply` / `tofu destroy` / `tofu import` / state edits, Ansible plays, or any command that mutates deployed infrastructure or its state — the RBE backend VM (`dasch-remotebuild-prod-01`, defined in the `ops-tf` / `infra` repos), buckets, IAM, firewall, secrets. This includes SSH-and-mutate (an `ssh … --command` that installs/restarts/writes), cloud-CLI create/update/delete, `kubectl apply`, and remote `docker` mutations.
- Read-only analysis IS allowed and expected: `tofu plan`/`validate`, Grafana/Loki queries, describe/list-style CLI reads, and read-only SSH diagnostics (probing service status, reading logs/configs).
- Workflow: diagnose, read state, and propose the exact change (the file diff and/or the precise command/apply plan). The maintainer applies it. Verify afterwards read-only. Authoring commits/PRs in the ops repos is fine; *deploying* them is not.

## Scope discipline

Rules for what to build (not how). Follow unless the user explicitly asks to override.

- **No backwards-compatibility shims.** Update every caller in the same change. Do not leave deprecated aliases, renamed-variable pointers, re-exports, or "keep the old name for now" comments. This repo uses rebase-merge, not squash; history preserves the reasoning.
- **No defense-in-depth.** Validate at system boundaries only — HTTP request handlers, FFI boundaries (C library calls), user-facing CLI parsers. See `REVIEW.md` §"Security (input validation)" for what qualifies. No redundant null checks, double validation, or `try`/`catch` around code that can't fail under the current contract.
- **No enterprise abstractions — KISS.** Prefer three similar lines over one parameterized helper. Prefer a concrete type over a trait/interface with a single implementation. Introduce an abstraction only for a second *real* caller, not a hypothetical one.
- **Ask when in doubt.** If a task is ambiguous, if two reasonable approaches exist, or if a new pattern/file/dep feels load-bearing, surface the decision to the user before acting. "Suggest, don't decide" is the default; autonomy is granted explicitly per-task.

These are not style preferences — they are contract with the maintainer. Code that violates them is code the maintainer did not ask for.

## Development Notes

**Compiler Requirements:** C++23. Bazel selects a hermetic LLVM 22.1.7 toolchain via the BCR `llvm` (hermetic-llvm) module pinned at 0.8.10; the host compiler does not need to be Clang. libc++ is the default stdlib and a single toolchain serves all platforms including the fuzz platforms. See [`docs/adr/0014-toolchain-provider-swap.md`](docs/adr/0014-toolchain-provider-swap.md).

**Build configurations:**
- `bazel build //src/cli:sipi` — fastbuild (`-O0 -g`, fast incremental)
- `bazel build -c opt //src/cli:sipi` — optimized (`-O3 -DNDEBUG`)
- `bazel build --config=release //src/cli:sipi` — production: `-c opt` + `_FORTIFY_SOURCE=2` hardening; what the Docker image ships
- `bazel build -c dbg //src/cli:sipi` — Debug (`-O0 -g`)
- `bazel build --config=asan --config=ubsan //src/cli:sipi` — sanitizers

**Error Reporting:** Optional Sentry integration (Rust `sentry` crate, `cli-rs/src/main.rs`) via `SIPI_SENTRY_DSN`, `SIPI_SENTRY_ENVIRONMENT`, `SIPI_SENTRY_RELEASE` environment variables — panics and handled image errors for every verb, plus an out-of-process minidump reporter for native crashes on `server` (see [`docs/adr/0018-minidump-crash-memory-accepted-risk.md`](docs/adr/0018-minidump-crash-memory-accepted-risk.md)).
