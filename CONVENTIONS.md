# SIPI Architectural Conventions

Project-specific architectural context for the sipi repository. Used by planning, working, and compounding workflows to understand the codebase structure.

For review-specific rules, see `REVIEW.md` in the sipi repository root.
For the full C++23 style guide, see `docs/src/development/cpp-style-guide.md`.
For commit and PR conventions, see `docs/src/development/commit-conventions.md`.
For reviewer guidelines, see `docs/src/development/reviewer-guidelines.md`.

## Production surface vs oracle

The **Rust axum shell** (`src/server-rs` + `src/cli-rs`) is the production server. It drives the C++ **image engine** (`libsipi`, the FFI callee) over the seam in `src/server-rs/src/ffi.rs`. The retained C++ **server** (`src/shttps` + `src/cli`) is **oracle-only**: kept solely as the reference in the differential parity test (`test/e2e/tests/differential.rs`), never deployed.

Consequences for production (Rust) code:

- Comments describe current, working Rust behavior on its own terms. Do **not** frame the Rust shell relative to the C++ server / oracle / transport ("matches the oracle", "the transport's X", "reconstructs shttps' Y"). Referencing the C++ **engine** (the production FFI callee) is fine — that is what the shell calls into.
- Parity observations belong in the differential test, not in shell code comments.
- Do not describe roadmap or in-flight history ("not yet wired", "previously", "now uses"); state what the code does today.

## Stack

- C++23, Clang 15+ / GCC 13+
- Build orchestrator: Bazel (single source of truth for CI; reproducible action graph)
- Reproducible dev environment: Nix dev shells (`flake.nix` `devShells` only — no Nix-side build derivations)
- HTTP framework: Rust axum shell (`src/server-rs`, `src/cli-rs`) — the production server; the retained C++ `shttps/` library is oracle-only (see "Production surface vs oracle")
- Image formats: libtiff, libpng, libjpeg, libwebp, Kakadu (JPEG 2000)
- Scripting: Lua (routes, preflight checks, image manipulation)

## Build Reproducibility Invariant

Every build/test/coverage step in CI invokes one of the `just bazel-*`
recipes — no inline `bazel` calls in workflow YAML, no `nix build` calls.
The consequences:

- Recipes are contracts: every `bazel-*` recipe is a promise that CI runs
  the same command. Adding ad-hoc bazel invocations in a workflow would
  violate that contract and create a drift surface.
- Tests are part of the action graph. `just bazel-coverage` builds sipi
  with coverage instrumentation and runs unit + approval + e2e in one
  pass; the lcov report at `bazel-out/_coverage/_coverage_report.dat` is
  what Codecov consumes.
- The inner-loop edit/rebuild cycle IS `just bazel-build`. Bazel's per-
  action cache rebuilds only the affected compile + link, not the full
  closure. No separate dev-shell-only path is needed.

If a new build configuration is genuinely needed across the team (non-
trivial compiler flags, specialized toolchain), that configuration
belongs in `.bazelrc` (a `--config=<name>` block) or a new Bazel target
in `MODULE.bazel` / `BUILD.bazel`, not an imperative shell recipe.

## Build Completeness Invariant

Every build target must succeed on every supported platform:
macOS (darwin-aarch64), linux-x86_64, and linux-aarch64. Linux-only
outputs (`//src:image` and the `bazel-docker-*` recipes built on top of
it) are gated by host-CPU `target_compatible_with`; everything else
must build on every platform. CI runs the test matrix on all three
platforms, so a green CI run verifies macOS as well as Linux. Before
shipping changes to `flake.nix`, `MODULE.bazel`, `BUILD.bazel`, or a
justfile build recipe, run `just bazel-build` and `just bazel-coverage`
locally on macOS at minimum.

## Scope Discipline

These rules govern *what* to build (mirrored from CLAUDE.md so
contributors and Claude share the same contract):

- **No backwards-compatibility shims.** Update every caller in the same
  change. Rebase-merge preserves history; deprecated aliases,
  re-exports, and "kept for now" pointers are not needed.
- **No defense-in-depth.** Validate at system boundaries only — HTTP
  request handlers, FFI boundaries, CLI parsers. See
  `REVIEW.md` § "Security (input validation)" for what qualifies.
- **No enterprise abstractions — KISS.** Three similar lines beat one
  parameterised helper. Introduce an abstraction only for a *second
  real* caller, not a hypothetical one.
- **Ask when in doubt.** Surface ambiguous decisions to the maintainer
  before acting. "Suggest, don't decide" is the default.

## Naming Conventions

The codebase has mixed naming styles. For new code, prefer the C++23 style guide (`camelCase` functions, `trailing_underscore_` members). When modifying existing code, match the surrounding style.

| Entity | Existing Convention | Example |
|---|---|---|
| Types / Classes | `PascalCase` with `Sipi` prefix | `SipiImage`, `SipiCache`, `SipiRateLimiter` |
| Functions / Methods | Mixed `camelCase` / `snake_case` | `imgroot()`, `send_error()`, `get_canonical_url()` |
| Private members | `_leading_underscore` | `_imgroot`, `_nthreads` |
| Namespaces | `PascalCase` | `Sipi::`, `shttps::` |

## Module Layout

### Canonical modules (commit-scope vocabulary)

These are SIPI's modules. Each name is also the canonical [commit
scope](docs/src/development/commit-conventions.md#scopes). A module is a
unit of responsibility, not necessarily a directory yet — the migration
to per-module co-located directories is tracked by
[ADR-0003](docs/adr/0003-module-co-located-source-and-tests.md).

| Module (scope) | Path | Responsibility |
|---|---|---|
| `image` | `src/SipiImage.*`, `include/SipiImage.hpp` | Image read/write pipeline; orchestrates decode → process → encode |
| `formats` | `src/formats/`, `include/formats/` | Per-format codecs: TIFF, JP2 (Kakadu), PNG, JPEG |
| `metadata` | `src/metadata/` | EXIF, IPTC, XMP, ICC profile handling |
| `iiifparser` | `src/iiifparser/`, `include/iiifparser/` | IIIF URL parsing (identifier, region, size, rotation, quality, format) |
| `handlers` | `src/handlers/` | HTTP request handlers |
| `shttps` | `src/shttps/` | Internal HTTP framework (threading, TLS, connection pooling, JWT) |
| `cache` | `include/SipiCache.h` | File-based LRU cache with dual-limit eviction |
| `memory-budget` | `include/SipiMemoryBudget.h` | Lock-free decode memory budget |
| `observability` | `src/observability/` | Prometheus metrics, tracing |
| `logging` | `src/logging/` | Structured logging |
| `cli` | `src/cli/` | C++ CLI app, arg parsing, subcommand dispatch |
| `ffi` | `src/ffi/` | Rust↔C++ FFI seam and Lua bindings |
| `lua` | `scripts/`, `config/*.lua` | Lua route/preflight scripts and config |
| `server-rs` | `src/server-rs/` | Rust server shell (strangler-fig subject) |
| `cli-rs` | `src/cli-rs/` | Rust CLI shell |

The C++ `SipiHttpServer` orchestration (`src/SipiHttpServer.*`) is being
strangled by `server-rs`; new server work lands under `server-rs`, and
route/handler changes use `handlers` or `shttps`.

Beyond modules, commits use **test-layer scopes** (`e2e`, `approval`) and
**cross-cutting scopes** (`deps`, `bazel`, `ci`, `nix`, `docker`). If none
of the enumerated scopes genuinely fits a change, ask the maintainer
before inventing a new one. The full scope rules live in
[commit-conventions.md § Scopes](docs/src/development/commit-conventions.md#scopes).

### Directory layouts

The codebase has two coexisting layouts:

- **Historical (current default):** `src/<mod>/Foo.cpp` paired with
  `include/<mod>/Foo.h`, unit tests under `test/unit/<mod>/`.
- **Module-co-located ([ADR-0003](docs/adr/0003-module-co-located-source-and-tests.md), proposed):**
  `src/<mod>/{Foo.cpp, Foo.h, foo_test.cpp}` with flat-style includes
  (`#include "metadata/Foo.h"` cross-module, `#include "Foo.h"`
  intra-module). `shttps/` and `src/handlers/` already follow this.
  Migration is staged behind the Bazel build-tool migration and lands
  as five mechanical per-module PRs.

Until ADR-0003 is accepted and a module is migrated, follow the
historical layout for that module. After migration, follow the new
layout. ADR-0003 is the source of truth for migration order and
per-module diff shape.

## Route Registration

Routes are registered in `SipiHttpServer::run()` via `add_route()`:

```cpp
void SipiHttpServer::run() {
  add_route(Connection::GET, "/metrics", metrics_handler);
  add_route(Connection::GET, "/health", health_handler);  // example
  add_route(Connection::GET, "/", iiif_handler);           // catch-all last
  Server::run();
}
```

The catch-all `/` route must be registered **last** — it matches everything.

## HTTP Status Codes

Available in `Connection::StatusCodes` enum (`shttps/transport/Connection.h`):

| Code | Enum | Use for |
|---|---|---|
| 400 | `BAD_REQUEST` | Invalid IIIF parameters, path traversal attempts |
| 403 | `FORBIDDEN` | Access denied by preflight Lua script |
| 404 | `NOT_FOUND` | Image file not found |
| 429 | `TOO_MANY_REQUESTS` | Rate limiter triggered |
| 500 | `INTERNAL_SERVER_ERROR` | Unexpected failures, OOM recovery |
| 503 | `SERVICE_UNAVAILABLE` | Server overloaded / shutting down / memory budget exhausted |

## Configuration Pattern

Config flows: CLI args (CLI11) → Lua config file → `SipiConf` struct → `SipiHttpServer` accessors.

New config options need:
1. Field in `SipiConf.h` / `SipiConf.cpp` (Lua table read)
2. CLI option in `src/cli/cli_app.cpp` (CLI11)
3. Accessor in `SipiHttpServer.hpp`
4. Documentation in `config/sipi.config.lua`
5. Environment variable override (optional, for Docker)

## Error Handling Pattern

| Situation | Mechanism |
|---|---|
| Fallible operations (parsing, I/O, validation) | `std::expected<T, E>` (new code) or `SipiError` (existing) |
| Truly unrecoverable errors | `throw SipiError(...)` |
| HTTP errors to clients | `send_error(conn_obj, Connection::STATUS_CODE, "message")` |
| Resource exhaustion (OOM) | Catch `std::bad_alloc`, return HTTP 500, log, continue serving |

## Docker

- Build: `bazel run //src:image_load` (per-arch); `crane index append`
  assembles the multi-arch manifest (see `src/BUILD.bazel`).
- Base image: `gcr.io/distroless/base-debian12` (glibc, pinned by digest)
- Init: `tini` (PID 1 zombie reaping, signal forwarding)
- Runtime user: `root` (NFS uid/gid coordination is a known constraint;
  documented inline in `src/BUILD.bazel` near the image rule)
- Port: 1024 (non-privileged)
- Config mount: `/sipi/config/`
- Image root: `/sipi/images/`
- Cache: `/sipi/cache/`

## Prometheus Metrics

SIPI-side singleton at `Sipi::observability::Metrics::instance()`
(`src/observability/metrics.h`); exposed at `GET /metrics`.

```cpp
prometheus::Counter &my_counter_total =
  prometheus::BuildCounter()
    .Name("sipi_my_counter_total")
    .Help("Description")
    .Register(*Sipi::observability::Metrics::instance().registry)
    .Add({});
```

shttps-side instrumentation goes through the
`shttps::ConnectionMetrics` Strategy interface (see
`shttps/transport/ConnectionMetrics.h`). At startup, `src/cli/cli_app.cpp` installs a
`Sipi::observability::ConnectionMetricsAdapter` (Adapter pattern) that bridges
shttps events into the `Sipi::observability::Metrics` singleton. **Do not call
`Sipi::observability::Metrics::instance()` from `shttps/` code** — that
direction is the SIPI ← shttps leak that
[ADR-0001](docs/adr/0001-shttps-as-strangler-fig-target.md)'s
strangler-fig is closing (commit `f2ee8cfd`, Apr 30 2026). New
shttps-emitted events go on the `ConnectionMetrics` interface and
are implemented in the adapter.
