# SIPI Architectural Conventions

Project-specific architectural context for the sipi repository. Used by planning, working, and compounding workflows to understand the codebase structure.

For review-specific rules, see `REVIEW.md` in the sipi repository root.
For the full C++23 style guide, see `docs/src/development/cpp-style-guide.md`.
For commit and PR conventions, see `docs/src/development/commit-conventions.md`.
For reviewer guidelines, see `docs/src/development/reviewer-guidelines.md`.

## Stack

- C++23, Clang 15+ / GCC 13+, CMake 3.28+
- Build toolchains: Nix (reproducible, single source of truth for CI), Docker (production image)
- HTTP framework: custom `shttps/` library (threading, SSL, connection pooling)
- Image formats: libtiff, libpng, libjpeg, libwebp, Kakadu (JPEG 2000)
- Scripting: Lua (routes, preflight checks, image manipulation)

## Build Reproducibility Invariant

Every justfile recipe that builds sipi goes through `nix build .#<variant>`.
CI invokes only `just <recipe>` — no inline cmake or `nix build` calls in
workflow YAML. The consequences:

- Recipes are contracts: every `nix-*` recipe is a promise that CI runs the
  same command. Adding an imperative cmake recipe would violate that contract
  and create a drift surface.
- Unit tests run inside the Nix sandbox via `doCheck = enableTests` in
  `package.nix`. A `nix build .#dev` that succeeds is, by construction, a
  tested build — the derivation graph enforces "tested before cached."
- Incremental inner-loop development (edit one `.cpp` file, see a
  seconds-fast rebuild) is a documented dev-shell pattern — `nix develop`
  followed by `cmake --build build` by hand. It is intentionally NOT a
  recipe. See `CLAUDE.md` "Inner-loop development" and
  `docs/src/development/developing.md`.

If a new build configuration is genuinely needed across the team (non-trivial
CMake flags, specialized toolchain), that configuration deserves a Nix
variant in `flake.nix`, not a recipe wrapping imperative cmake.

The Bazel migration (DEV-6343..DEV-6348) adds a parallel `cc_test` inner
loop that is currently local-only. CMake/ctest via `just nix-build`
remains the CI canonical path. Until DEV-6348 cuts CI over, treat
`just bazel-test-unit` and `just bazel-test-approval` as a local
fast-feedback path — not a substitute for `nix-build`.

## Build Completeness Invariant

Every build target must succeed on every supported platform:
macOS (darwin-aarch64), linux-x86_64, and linux-aarch64. Linux-only
outputs (`docker`, `docker-stream`, `sipi-debug`) are gated by
`pkgs.lib.optionalAttrs isLinux` in `flake.nix`; everything else must
build on every platform. CI is Linux-only — **a green CI run does not
verify macOS**. Before shipping changes to `flake.nix`, `package.nix`,
or a justfile build recipe, run every affected variant locally on
macOS (`just nix-build-<variant>`) and dispatch Linux variants via
Determinate's native-linux-builder
(`nix build .#packages.{x86_64,aarch64}-linux.<variant>`). See CLAUDE.md
"Build completeness invariant" for the full checklist.

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

The codebase has two coexisting layouts:

- **Historical (current default):** `src/<mod>/Foo.cpp` paired with
  `include/<mod>/Foo.h`, unit tests under `test/unit/<mod>/`.
- **Module-co-located ([ADR-0003](docs/adr/0003-module-co-located-source-and-tests.md), proposed):**
  `src/<mod>/{Foo.cpp, Foo.h, foo_test.cpp}` with flat-style includes
  (`#include "metadata/Foo.h"` cross-module, `#include "Foo.h"`
  intra-module). `shttps/` and `src/handlers/` already follow this.
  Migration is staged behind the Bazel build-tool migration
  (DEV-6343..DEV-6348) and lands as five mechanical per-module PRs
  (Y+8a..Y+8e).

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

Available in `Connection::StatusCodes` enum (`shttps/Connection.h`):

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
2. CLI option in `src/sipi.cpp` (CLI11)
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

- Build: `nix build .#docker-stream` via `dockerTools.streamLayeredImage`
- Base image: nixpkgs userland (glibc, not musl/Alpine)
- Init: `tini` from nixpkgs (PID 1 zombie reaping, signal forwarding)
- Runtime user: `root` (NFS uid/gid coordination deferred to DEV-5920;
  `flake.nix` documents the constraint near the unset `config.User`)
- Port: 1024 (non-privileged)
- Config mount: `/sipi/config/`
- Image root: `/sipi/images/`
- Cache: `/sipi/cache/`

## Prometheus Metrics

SIPI-side singleton at `Sipi::SipiMetrics::instance()`
(`include/SipiMetrics.h`); exposed at `GET /metrics`.

```cpp
prometheus::Counter &my_counter_total =
  prometheus::BuildCounter()
    .Name("sipi_my_counter_total")
    .Help("Description")
    .Register(*SipiMetrics::instance().registry)
    .Add({});
```

shttps-side instrumentation goes through the
`shttps::ConnectionMetrics` Strategy interface (see
`shttps/ConnectionMetrics.h`). At startup, `src/sipi.cpp` installs a
`SipiConnectionMetricsAdapter` (Adapter pattern) that bridges shttps
events into the `SipiMetrics` singleton. **Do not call
`SipiMetrics::instance()` from `shttps/` code** — that direction is
the SIPI ← shttps leak that
[ADR-0001](docs/adr/0001-shttps-as-strangler-fig-target.md)'s
strangler-fig is closing (commit `f2ee8cfd`, Apr 30 2026). New
shttps-emitted events go on the `ConnectionMetrics` interface and
are implemented in the adapter.
