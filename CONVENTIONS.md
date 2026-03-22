# SIPI Architectural Conventions

Project-specific architectural context for the sipi repository. Used by planning, working, and compounding workflows to understand the codebase structure.

For review-specific rules, see `REVIEW.md` in the sipi repository root.
For the full C++23 style guide, see `docs/src/development/cpp-style-guide.md`.
For commit and PR conventions, see `docs/src/development/commit-conventions.md`.
For reviewer guidelines, see `docs/src/development/reviewer-guidelines.md`.

## Stack

- C++23, Clang 15+ / GCC 13+, CMake 3.28+
- Build toolchains: Zig (local dev), Nix (reproducible), Docker (CI)
- HTTP framework: custom `shttps/` library (threading, SSL, connection pooling)
- Image formats: libtiff, libpng, libjpeg, libwebp, Kakadu (JPEG 2000)
- Scripting: Lua (routes, preflight checks, image manipulation)

## Naming Conventions

The codebase has mixed naming styles. For new code, prefer the C++23 style guide (`camelCase` functions, `trailing_underscore_` members). When modifying existing code, match the surrounding style.

| Entity | Existing Convention | Example |
|---|---|---|
| Types / Classes | `PascalCase` with `Sipi` prefix | `SipiImage`, `SipiCache`, `SipiRateLimiter` |
| Functions / Methods | Mixed `camelCase` / `snake_case` | `imgroot()`, `send_error()`, `get_canonical_url()` |
| Private members | `_leading_underscore` | `_imgroot`, `_nthreads` |
| Namespaces | `PascalCase` | `Sipi::`, `shttps::` |

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

- Base image: Alpine (musl, minimal attack surface)
- Init: `pid1-rs` (PID 1 zombie reaping, signal forwarding)
- Port: 1024 (non-privileged)
- Config mount: `/sipi/config/`
- Image root: `/sipi/images/`
- Cache: `/sipi/cache/`

## Prometheus Metrics

Singleton at `SipiMetrics::instance()` (`include/SipiMetrics.h`). Exposed at `GET /metrics`.

```cpp
prometheus::Counter &my_counter_total =
  prometheus::BuildCounter()
    .Name("sipi_my_counter_total")
    .Help("Description")
    .Register(*SipiMetrics::instance().registry)
    .Add({});
```
