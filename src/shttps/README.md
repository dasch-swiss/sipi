# shttps embedded HTTP framework

An embedded multithreaded HTTP/HTTPS server with route registration and per-request embedded Lua scripting. Internal SIPI module — depended on one-way by the rest of `src/`. Namespace `shttps::`.

See [ADR-0013](../../docs/adr/0013-shttps-as-internal-module.md) for the framing decision (shttps is an internal module, not a peer bounded context) and [ADR-0001](../../docs/adr/0001-shttps-as-strangler-fig-target.md) (superseded) for the historical "second context" framing.

## Replacement plan

shttps is the **first strangler-fig slice** of a planned Rust rewrite of the whole of SIPI. Until that lands, the four seam types below must stay semantically stable so the eventual Rust replacement can reproduce them (not necessarily symbol-for-symbol).

## Module layout

The sources are split into five sub-packages, one Bazel package per module, so the relocation that the Rust cutover performs is a whole-directory delete rather than a file-by-file extraction:

| Sub-package | Contents | Fate at the Rust cutover |
|-------------|----------|--------------------------|
| `transport/` | `Server`, `Connection`, `SocketControl`, `SockStream`, `ChunkReader`, `ThreadControl`, `ConnectionMetrics`, `Shttp` | **Deleted** → axum/hyper/tokio |
| `lua/` | `LuaServer` (the per-request interpreter) | Kept, fed by a buffered request context, then ported to mlua |
| `lua_sqlite/` | `LuaSqlite` (SQLite Lua bindings) | Kept, then ported |
| `jwt/` | `jwt.{c,h}` | Kept (or → Rust `jsonwebtoken`) |
| `util/` | `Parsing`, `Hash`, `Error`, `Global`, `makeunique` | Kept, re-homed SIPI-side |

`transport` and `lua` are mutually dependent (`Server` holds a `LuaServer`; `LuaServer` needs the full `Connection` type), so they share one Bazel package (`:shttps_headers` + `:shttps`); `util` and `jwt` are leaf packages and `lua_sqlite` depends one-directionally on `:shttps`. Cross-module includes are written in the explicit `shttps/<module>/X.h` form; same-directory siblings stay bare (ADR-0003).

## Module API surface — four seam types

**Server**:
The HTTP/HTTPS lifecycle owner. Owns the listening socket, thread pool, route table, SSL context, and global Lua functions. Subclassed by consumers (e.g. `Sipi::SipiHttpServer`) to add domain configuration.
_Avoid_: app, listener (too vague), HTTP server (use the canonical type name).

**Connection**:
The I/O surface for one HTTP request/response. Carries request headers, query, body, route parameters, and the response stream. Every request handler receives one. Single-use per request.
_Avoid_: request, response, context, ctx (Connection is request *and* response, not either).

**RequestHandler**:
The function-pointer shape `void (Connection &, LuaServer &, void *user_data, void *handler_data)` registered against a `(method, path)` pair via `Server::add_route`. The unit of dispatch.
_Avoid_: handler (overloaded — see *Lua route handler* in the SIPI `UBIQUITOUS_LANGUAGE.md`), callback, endpoint.

**LuaServer**:
A per-request Lua interpreter. Wraps a `lua_State`, exposes the current `Connection` to Lua, and executes either inline Lua code or a `.lua` file. Despite the name it is *not* a server; it is created and torn down per request.
_Avoid_: Lua VM (correct conceptually but the type is `LuaServer`), Lua context.

## Additional module vocabulary

**Route**:
A `(HTTP method, path)` pair bound to either a `RequestHandler` (C++ function pointer) or a `LuaRoute` (path → Lua script). Owned by the `Server`.
_Avoid_: endpoint, mapping.

**LuaRoute**:
A route whose handler is a Lua script file rather than a C++ `RequestHandler`. Resolved through the per-request `LuaServer`.
_Avoid_: script route, Lua endpoint.

**`shttps::file_handler`**:
A built-in `RequestHandler` shipped by shttps that serves files from the *document root* and renders Lua-in-HTML pages. Generic — knows nothing about IIIF or images. Consumers register it on a chosen URL prefix (e.g. `wwwroute` in SIPI's config).
_Avoid_: confusing this with SIPI's IIIF `/file` endpoint (which serves a Bitstream by IIIF identifier, not a docroot path).

**Document root**:
The filesystem directory tree that `shttps::file_handler` resolves URL paths into. Owned by the consumer; supplied as the second element of the `(wwwroute, docroot)` pair passed via the `handler_data` argument of `Server::add_route(...)`.
_Avoid_: web root.

**Init script**:
The Lua source file loaded once at server startup, registered via `Server::initscript(path)`. shttps owns the lifecycle (load-once-at-boot); the script's *content* is the consumer's responsibility.
_Avoid_: bootstrap, startup script.

**Connection-pool knobs**:
The set of `Server` configuration points that shape admission and lifecycle of inbound HTTP connections: `keep_alive_timeout`, `queue_timeout`, `max_waiting_connections`, the worker-thread count. Owned by shttps.
_Avoid_: tuning, throttling (those are SIPI-side **Backpressure** concerns, separate concept).

## Route ownership

shttps is **route-blind**. It ships handlers (`shttps::file_handler` and any other future built-ins) and the registration mechanism (`Server::add_route`), but never registers a route by itself. Consumers choose every path and method. This keeps domain policy out of the framework: paths like `/iiif`, `/health`, `/metrics`, `/favicon.ico`, and the configured `wwwroute` are SIPI policy decisions, not framework defaults.

## Dependency direction

Strictly SIPI → shttps. shttps depends on the rest of `src/` only through `//src/logging:logging` — a generic levelled-logging primitive (global free functions, no `Sipi::` namespace, no domain code), explicitly visibility-allowlisted as a shared support library. shttps does **not** consume any SIPI-namespace header or symbol.

Enforcement today:
- Bazel `visibility` — only `//src:__subpackages__` and `//src/shttps:__subpackages__` can depend on `//src/shttps:shttps`.
- Curated direct-deps list in `src/shttps/BUILD.bazel`.

Bazel `--features=layering_check` is **deferred** to the broader Y+8 layering rollout (DEV-6353) — the third-party native `cc_library` deps (libtiff, exiv2, …) are not layering-clean (they freely include each other's internal headers), so enabling layering_check globally fails their compiles. Bazel `visibility` plus the curated direct-deps list are the active enforcement until DEV-6353.

## Utilities scheduled for re-homing

A handful of files in this module are SIPI domain concerns or generic helpers, not HTTP-framework code. They are grouped in the `util/` sub-package but still live under `shttps/` for historical reasons. Re-homing them fully SIPI-side is a backlog **consistency cleanup**:

- `Hash` / `HashType` — used by `Essentials::pixel_checksum`. Belongs to SIPI's preservation domain.
- `Parsing` — URL decoding, mime-type magic, header parsing helpers. Generic utilities; SIPI-side support module.
- `Error` — base class of `SipiError`. Fold into SIPI's error hierarchy.
- `Global`, `makeunique` — pure utilities. Inline or move to SIPI-side support.

Per ADR-0013, the re-homing is no longer a *Rust precondition* — the whole codebase is going to Rust, so the destination on the C++ side does not affect the Rust shape.

## Relationships (with SIPI)

- A SIPI **Image** or **Bitstream** request enters through an `shttps::RequestHandler` (`iiif_handler` / `file_handler`) registered on `shttps::Server`.
- The handler reads request data from the `shttps::Connection`, runs SIPI's **Preflight script** through the request-scoped `shttps::LuaServer`, and uses the resulting **Permission** to decide what to serve.
- SIPI's **Route handler** (Lua) is wired through a generic `RequestHandler` that loads the script into the same `LuaServer`.
- Response bytes (cache hit, decoded image, streamed bitstream) are written back through the same `Connection`.
- Telemetry crosses the boundary via the `ConnectionMetrics` strategy interface owned by this module (`shttps/transport/ConnectionMetrics.h` + `Server::setMetrics`). SIPI installs `Sipi::observability::ConnectionMetricsAdapter` at startup, which bridges connection lifecycle events to `Sipi::observability::Metrics`. shttps does not name any `Sipi::` type.

## Naming gotchas

- **`LuaServer` is not a server.** It is a per-request Lua interpreter (a VM wrapper). The name predates the current model and cannot be renamed without breaking consumers; in conversation, prefer "Lua interpreter" or "Lua VM" and reserve "server" for `shttps::Server`.
- **"Handler" is overloaded across the seam.** In shttps, **RequestHandler** is a C++ function pointer. In SIPI's user-facing language ("Route handler" in `UBIQUITOUS_LANGUAGE.md`), it is a *Lua script* bound to a URL. They are distinct concepts: a SIPI Lua route handler is *invoked by* a shttps `RequestHandler` that loads and runs the script.
- **"file_handler" is overloaded.** `shttps::file_handler` serves arbitrary files from the **document root** (generic static-file + Lua-in-HTML). SIPI's IIIF `/file` endpoint (`handlers::iiif_handler::FILE_DOWNLOAD`, sipi-side) serves a **Bitstream** keyed by an IIIF identifier under the **image root**. Same word, two unrelated handlers; never use "file_handler" without qualifying which side.

## Example dialogue

> **Dev:** "I want to add a `/health` endpoint. What do I touch in shttps?"

> **Maintainer:** "Nothing. Register a **Route** from the SIPI side: pass a **RequestHandler** to `add_route(GET, '/health', my_handler)`. Your handler reads from / writes to the **Connection** it gets. shttps shouldn't grow knowledge of `/health`."

> **Dev:** "And if I want to expose request count to Prometheus?"

> **Maintainer:** "Same answer at the boundary. shttps must not call `Sipi::observability::Metrics`. The clean shape — and the one we have today — is: shttps owns a `ConnectionMetrics` strategy interface on **Server**; SIPI installs `Sipi::observability::ConnectionMetricsAdapter` at startup, which bridges those events to `Sipi::observability::Metrics`. shttps holds no reverse dependency on any `Sipi::` type."

> **Dev:** "Why is `LuaServer` called a server if it isn't one?"

> **Maintainer:** "Historical. It is a per-request Lua interpreter — created when the route fires, torn down when the **Connection** closes. The name predates the current architecture. Don't read 'server' into it."
