# shttps embedded HTTP framework

A small, multithreaded HTTP/HTTPS server with route registration and embedded Lua scripting. Generic — knows nothing about IIIF, images, or SIPI-specific concepts. Lives in namespace `shttps::`. SIPI is a consumer; the dependency is one-way.

## Language

**Server**:
The HTTP/HTTPS lifecycle owner. Owns the listening socket, thread pool, route table, SSL context, and global Lua functions. Subclassed by consumers (e.g. `Sipi::SipiHttpServer`) to add domain configuration.
_Avoid_: app, listener (too vague), HTTP server (use the canonical type name).

**Connection**:
The I/O surface for one HTTP request/response. Carries request headers, query, body, route parameters, and the response stream. Every request handler receives one. Single-use per request.
_Avoid_: request, response, context, ctx (Connection is request *and* response, not either).

**RequestHandler**:
The function-pointer shape `void (Connection &, LuaServer &, void *user_data, void *handler_data)` registered against a `(method, path)` pair via `Server::add_route`. The unit of dispatch.
_Avoid_: handler (overloaded — see *Lua route handler* in the SIPI context), callback, endpoint.

**LuaServer**:
A per-request Lua interpreter. Wraps a `lua_State`, exposes the current `Connection` to Lua, and executes either inline Lua code or a `.lua` file. Despite the name it is *not* a server; it is created and torn down per request.
_Avoid_: Lua VM (correct conceptually but the type is `LuaServer`), Lua context.

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

## Primary seam (load-bearing across the planned Rust rewrite)

These four types are the *contract* SIPI consumes. The strangler-fig replacement must reproduce them semantically (not necessarily symbol-for-symbol):

- **Server** — for lifecycle and route registration.
- **Connection** — for request/response I/O.
- **RequestHandler** — for the C++/native dispatch shape.
- **LuaServer** — for embedded Lua scripting (preflight, init, route scripts).

## Enforcement

The one-way direction is enforced by `just shttps-context-check`, which fails CI on any new `#include "Sipi*.h"` or `Sipi*::` symbol use inside `shttps/`. Allowlist entries live in `scripts/shttps-context-check.sh`.

## Secondary surface (scheduled for re-homing into SIPI before the Rust migration)

These currently live in shttps but are not seam types. They are utilities or domain-flavoured types that ended up in shttps for historical reasons. The migration plan re-homes them into SIPI so the Rust replacement does not need to reproduce them:

- `Hash` / `HashType` — used by `SipiEssentials::pixel_checksum`. Belongs to SIPI's preservation domain, not the HTTP framework.
- `Parsing` — URL decoding, mime-type magic, header parsing helpers. Generic utilities; can move to a SIPI-side `support/` module.
- `Error` — base class of `SipiError`. Re-home as part of SIPI's error hierarchy.
- `Global`, `makeunique` — pure utilities. Inline or move to SIPI-side support.

## Relationships

- A **Server** owns many **Routes**. Each **Route** binds a method+path to one **RequestHandler** *or* one **LuaRoute**.
- A **Server** dispatches each accepted request on a worker thread, constructs a **Connection** for it, and (when the route is a Lua route) spins up a **LuaServer** scoped to that request.
- A **RequestHandler** receives the **Connection** and a **LuaServer** by reference. Both die when the request completes.
- A **LuaRoute** is resolved by loading and executing its script inside the request-scoped **LuaServer**.

## Example dialogue

> **Dev:** "I want to add a `/health` endpoint. What do I touch in shttps?"

> **Maintainer:** "Nothing. Register a **Route** from the SIPI side: pass a **RequestHandler** to `add_route(GET, '/health', my_handler)`. Your handler reads from / writes to the **Connection** it gets. shttps shouldn't grow knowledge of `/health`."

> **Dev:** "And if I want to expose request count to Prometheus?"

> **Maintainer:** "Same answer at the boundary. shttps must not call `SipiMetrics`. The clean shape is: shttps exposes a metrics-callback hook on **Server**; SIPI installs a callback that drives `SipiMetrics`. Today shttps reaches into `SipiMetrics` directly — that is a tracked layering leak."

> **Dev:** "Why is `LuaServer` called a server if it isn't one?"

> **Maintainer:** "Historical. It is a per-request Lua interpreter — created when the route fires, torn down when the **Connection** closes. The name predates the current architecture. Don't read 'server' into it."

## Flagged ambiguities

- **`LuaServer` is not a server.** It is a per-request Lua interpreter (a VM wrapper). The name predates the current model and cannot be renamed without breaking consumers; in conversation, prefer "Lua interpreter" or "Lua VM" and reserve "server" for `shttps::Server`.
- **"Handler" is overloaded across the boundary.** In shttps, **RequestHandler** is a C++ function pointer. In SIPI's user-facing language ("Route handler" in `UBIQUITOUS_LANGUAGE.md`), it is a *Lua script* bound to a URL. They are distinct concepts: a SIPI Lua route handler is *invoked by* a shttps `RequestHandler` that loads and runs the script.
- **"file_handler" is overloaded.** `shttps::file_handler` serves arbitrary files from the **document root** (generic static-file + Lua-in-HTML). SIPI's IIIF `/file` endpoint (`handlers::iiif_handler::FILE_DOWNLOAD`, sipi-side) serves a **Bitstream** keyed by an IIIF identifier under the **image root**. Same word, two unrelated handlers; never use "file_handler" without qualifying which side.
- **`SipiMetrics` is referenced from `shttps/Server.cpp`.** This violates the one-way dependency direction and is a tracked layering leak, not a sanctioned pattern.
