# SIPI Architectural Conventions

Project-specific architectural context for the sipi repository. Used by planning, working, and compounding workflows to understand the codebase structure.

For review-specific rules, see `REVIEW.md` in the sipi repository root.
For the full C++23 style guide, see `docs/src/development/cpp-style-guide.md`.
For commit and PR conventions, see `docs/src/development/commit-conventions.md`.
For reviewer guidelines, see `docs/src/development/reviewer-guidelines.md`.

## Production surface

The **Rust axum shell** (`src/server-rs` + `src/cli-rs`) is the sole production server. It drives the C++ **image engine** (`libsipi`, the FFI callee) over the seam in `src/server-rs/src/ffi.rs`. There is no C++ server: the `shttps` transport and `SipiHttpServer` were removed with the oracle ([ADR-0020](docs/adr/0020-oracle-removal.md)). `src/cli` is production too: `sipi_cli_main` and the offline verbs (`convert`/`verify`/`query`/`compare`/`health`) the Rust CLI shell drives.

Consequences for production code:

- Comments describe current, working behavior on their own terms. Do **not** frame the Rust shell relative to the removed C++ server / oracle / transport ("matches the oracle", "the transport's X", "at the cutover", "reconstructs shttps' Y"). Referencing the C++ **engine** (the production FFI callee) is fine — that is what the shell calls into.
- Do not describe roadmap or in-flight history ("not yet wired", "previously", "now uses"); state what the code does today.

## Stack

- C++23, Clang 15+ / GCC 13+
- Build orchestrator: Bazel (single source of truth for CI; reproducible action graph)
- Reproducible dev environment: Nix dev shells (`flake.nix` `devShells` only — no Nix-side build derivations)
- HTTP framework: Rust axum shell (`src/server-rs`, `src/cli-rs`) — the production server (see "Production surface")
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
| Types / Classes | `PascalCase` with `Sipi` prefix | `SipiImage`, `SipiCache` |
| Functions / Methods | Mixed `camelCase` / `snake_case` | `imgroot()`, `send_error()`, `build_canonical_url()` |
| Private members | `_leading_underscore` | `_imgroot`, `_nthreads` |
| Namespaces | `PascalCase` | `Sipi::`, `shttps::` |

## Module Layout

### Canonical modules (commit-scope vocabulary)

These are SIPI's modules. Each name is also the canonical [commit
scope](docs/src/development/commit-conventions.md#scopes). A module is a
unit of responsibility, not necessarily a directory yet — the migration
to per-module co-located directories is tracked by
[ADR-0003](docs/adr/0003-module-co-located-source-and-tests.md).

Scope a commit by the responsibility it serves, not the directory the edited
files happen to sit in. A metrics or tracing change is `observability` even
when the code lives under `src/server-rs/` or rides the `ffi` seam; a change
is scoped `ffi` only when the seam mechanism itself is the point.

| Module (scope) | Path | Responsibility |
|---|---|---|
| `image` | `src/SipiImage.{h,cpp}` | Image read/write pipeline; orchestrates decode → process → encode |
| `formats` | `src/formats/` | Per-format codecs: TIFF, JP2 (Kakadu), PNG, JPEG |
| `metadata` | `src/metadata/` | EXIF, IPTC, XMP, ICC profile handling |
| `iiifparser` | `src/iiifparser/` | IIIF URL parsing (identifier, region, size, rotation, quality, format) and the `parse_iiif_uri` request classifier (`:iiif_handler`, testonly — the Rust shell parses in production) |
| `scripting` | `src/scripting/` | Connection-less Lua runtime: `LuaServer` + `request_context.h` + the `server.db` sqlite bindings |
| `util` | `src/util/` | Generic SIPI-domain helpers: MIME/string parsing, file hashing, the `shttps::Error`/`Global` types |
| `jwt` | `src/jwt/` | JWT (JWS) sign/verify leaf over OpenSSL + jansson |
| `cache` | `src/SipiCache.{h,cpp}` | File-based LRU cache with dual-limit eviction |
| `memory-budget` | `src/SipiMemoryBudget.{h,cpp}` | Lock-free decode memory budget |
| `memory` | `bazel/mimalloc.BUILD.bazel`, `_ALLOCATOR` in `src/cli-rs/BUILD.bazel`, `tools/allocator-replay/` | Process memory behavior: the production allocator, RSS/retention measurement |
| `observability` | `src/observability/` | Metrics (atomic counters/gauges), tracing |
| `logging` | `src/logging/` | Structured logging |
| `cli` | `src/cli/` | C++ CLI app (offline verbs) behind the `sipi_cli_main` FFI entry |
| `ffi` | `src/ffi/` | Rust↔C++ FFI seam and Lua bindings |
| `lua` | `scripts/`, `config/*.lua` | Lua route/preflight scripts and config |
| `server-rs` | `src/server-rs/` | Rust server shell — the production server |
| `cli-rs` | `src/cli-rs/` | Rust CLI shell |

All server work lands under `server-rs`; there is no C++ server (the `shttps`
transport and `SipiHttpServer` were removed with the oracle,
[ADR-0020](docs/adr/0020-oracle-removal.md)).

Beyond modules, commits use **test-layer scopes** (`e2e`, `approval`, for a
test layer's own harness or fixtures) and **cross-cutting scopes** (`deps`,
`bazel`, `ci`, `nix`, `docker`, `docs`). `docs` is for the agent-context /
domain documentation that is not tied to one module — the root `CLAUDE.md` /
`CONVENTIONS.md` / `CONTEXT.md` / `UBIQUITOUS_LANGUAGE.md` and the ADRs; a doc
change scoped to one module still takes that module's scope. A test *about* a concern takes that concern's
scope — `test(observability)`, not `test(e2e)` — since the `test` type already
says it is a test. If none of the enumerated scopes genuinely fits a change,
ask the maintainer before inventing a new one. The full scope rules live in
[commit-conventions.md § Scopes](docs/src/development/commit-conventions.md#scopes).

### Directory layouts

The codebase has two coexisting layouts:

- **Historical (current default):** `src/<mod>/Foo.cpp` paired with
  `include/<mod>/Foo.h`, unit tests under `test/unit/<mod>/`.
- **Module-co-located ([ADR-0003](docs/adr/0003-module-co-located-source-and-tests.md), proposed):**
  `src/<mod>/{Foo.cpp, Foo.h, foo_test.cpp}` with flat-style includes
  (`#include "metadata/Foo.h"` cross-module, `#include "Foo.h"`
  intra-module). `src/util/`, `src/scripting/`, `src/jwt/`, and `src/iiifparser/`
  already follow this. Migration is staged behind the Bazel build-tool migration
  and lands as mechanical per-module PRs.

Until ADR-0003 is accepted and a module is migrated, follow the
historical layout for that module. After migration, follow the new
layout. ADR-0003 is the source of truth for migration order and
per-module diff shape.

## Route Registration

Built-in routes are registered on the axum `Router` in the Rust shell
(`src/server-rs/src/routes.rs`). Scripted routes are Lua scripts bound to URL
patterns in the config ([ADR-0017](docs/adr/0017-extensibility-lua-and-rust.md)):
a `Route handler` is a Lua script the shell dispatches to, run inside the
request-scoped `shttps::LuaServer`. IIIF requests are classified in
`src/server-rs/src/iiif.rs` (`parse_request`), which owns region/size/rotation/
quality/format parsing; the flattened params cross the FFI seam to the C++ engine.

## HTTP Status Codes

The Rust shell returns axum `http::StatusCode`s. Common cases:

| Code | Enum | Use for |
|---|---|---|
| 400 | `BAD_REQUEST` | Invalid IIIF parameters, path traversal attempts |
| 403 | `FORBIDDEN` | Access denied by preflight Lua script |
| 404 | `NOT_FOUND` | Image file not found |
| 500 | `INTERNAL_SERVER_ERROR` | Unexpected failures, OOM recovery |
| 503 | `SERVICE_UNAVAILABLE` | Server overloaded / shutting down / memory budget exhausted |

## Configuration Pattern

Production config for an engine-behaviour knob flows **Rust → FFI → C++ engine**:
the Rust shell parses CLI/env (clap) and TOML, layers them into a
`ServerOverrides` bag, hands it across the FFI seam as the `#[repr(C)]`
`SipiServerConfig`, and `sipi_init` applies it onto the Lua-parsed `SipiConf`
before the engine reads it. Adding one option touches every link below; most
links **fail to compile** if you forget them (DUNE-006), except the last C++
apply block, which is the one hand-mirrored seam.

Rust production side:
1. **clap flag** — a field in the right `src/cli-rs/src/commands/server/args/<group>.rs`
   group (network/paths/cache/limits/tls_auth/knora/logging/concurrency), with a
   colocated `env = "SIPI_X"` (clap owns CLI-over-env precedence).
2. **`ServerOverrides` field** — `src/server-rs/src/config.rs` (the Rust-native bag).
3. **forward from clap** — `From<&ServerArgs> for ServerOverrides`
   (`src/cli-rs/src/commands/server/mod.rs`). *Exhaustively destructures every
   clap group → a new flag that is not forwarded (or explicitly `field: _`) fails
   to compile.*
4. **TOML base** — a `Config` field + its `Config::base()` mapping
   (`src/server-rs/src/config_file.rs`). *Exhaustive `ServerOverrides` literal →
   a missing map fails to compile.*
5. **merge** — `ServerOverrides::layered_over` (`config.rs`). *Exhaustive literal.*

FFI seam:
6. **`SipiServerConfig` struct** — a field (plus a `has_*` presence flag for a
   scalar) in `src/ffi/sipi_ffi.h`, mirrored by the Rust `#[repr(C)]
   SipiServerConfig` in `config.rs`. Both sides are layout-guarded by the paired
   `static_assert`/`offset_of!` blocks (a drift fails the build/tests).
7. **forward to the FFI struct** — `OverridesHolder::new` (`config.rs`).
   *Exhaustively destructures `ServerOverrides` → a field never forwarded to the C
   struct fails to compile (unused binding under `-D warnings`).*

C++ engine:
8. **`include/SipiConf.h` + `src/SipiConf.cpp`** — the getter/setter and the Lua
   `config.*` table read (the engine's own config surface).
9. **`config/sipi.config.lua`** — document the option.
10. **THE ONE UNMECHANIZED LINK — the `sipi_init` apply block**
    (`src/ffi/init.cpp`): a hand-written `if (o.newfield != nullptr)
    conf.setNewfield(...)` per override. **Nothing checks this for
    completeness** — a forgotten line compiles clean and silently drops the
    override before the engine sees it. Always add the apply line here when you
    add an FFI field (step 6).

## Error Handling Pattern

| Situation | Mechanism |
|---|---|
| Fallible operations (parsing, I/O, validation) | `std::expected<T, E>` (new code) or `SipiError` (existing) |
| Truly unrecoverable errors | `throw SipiError(...)` |
| Engine errors to the client | The engine returns a `SipiStatus` over the FFI seam; the Rust shell maps it to an axum HTTP response |
| Resource exhaustion (OOM) | Catch `std::bad_alloc`, return an error status over the seam, log, continue serving |

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

## Metrics

Engine-internal singleton at `Sipi::observability::Metrics::instance()`
(`src/observability/metrics.h`) — plain lock-free atomics (`Counter` / `Gauge`).
The engine bumps them on the decode/cache/serve paths. Production exports over OTLP:
the scalar counters and gauges cross the seam as the flat `SipiMetricsSnapshot`
(`src/ffi/sipi_ffi.cpp`) and are re-registered as OTel observable instruments in
`src/server-rs/src/metrics.rs`. **A new counter or gauge here does not reach
production until it is added to that snapshot and that module.** Distributions
cannot cross the flat snapshot at all; record them as OTel histograms shell-side
(see `record_http_duration` and `record_decode_estimate`).

Two tests enforce this, because the seam is easy to forget and a metric that stops
at it fails silently — it simply never appears in Grafana:

- `//src/observability:metrics_registry_test` classifies every metric field as
  bridged-to-OTLP or engine-internal. Adding a field fails it until you say which.
- `every_snapshot_field_is_accounted_for` in `src/server-rs/src/metrics.rs` fails
  unless each snapshot field is exported or explicitly listed as unexported.

**Engine-internal (not bridged)** — incremented in production, observable by
nobody, recorded in the first test's `kEngineInternalNotBridged`:

| Metric | Why | Cost to fix |
|---|---|---|
| `read_shape_fast_path_*` (ADR-0004) | Label-fanned `format` × `outcome`, 8 counters; the flat snapshot holds scalars | 8 scalar fields |
| `essentials_hash_mismatch_*` (ADR-0010) | Same, 5 counters. This is the **corruption tripwire** — nothing in production can see a detected corruption; `sipi verify service-file` is the only read path | 5 scalar fields |

Both label sets are static, so bridging them is mechanical rather than a design
problem. Shrinking that list is a welcome change; growing it needs an argument in
review.

```cpp
// Declared as a member of Sipi::observability::Metrics (metrics.h):
Counter my_counter_total;
// Bumped on the hot path:
Sipi::observability::Metrics::instance().my_counter_total.Increment();
```

To reach production OTLP, also read the field into `SipiMetricsSnapshot`
(`src/ffi/sipi_ffi.cpp`) and map it in `src/server-rs/src/metrics.rs`.
