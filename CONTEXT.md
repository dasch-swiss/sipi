# SIPI image server

The IIIF Image API 3.0 implementation: identifier resolution, the IIIF processing pipeline (region, size, rotation, quality, format), the file-based cache, preservation metadata, and the Lua-driven preflight / init / route-handler extensibility surface. Lives under `src/`, `include/`, `include/iiifparser/`, `include/formats/`, `include/metadata/`. Namespace `Sipi::`.

## Language

The canonical SIPI glossary is in [UBIQUITOUS_LANGUAGE.md](./UBIQUITOUS_LANGUAGE.md). It defines: Image vs Bitstream, Identifier (with embedded Page) + Prefix, Image root vs Document root, the IIIF pipeline terms (Region / Size / Rotation / Quality / Format / Decode level / Canonical URL / Cache key), Format handler vs Codec, Preservation metadata (umbrella) over Embedded metadata + Essentials packet, Image / Bitstream Information document, the three Lua entry points (Init script / Preflight script / Route handler), the seven Permission types, and the Backpressure umbrella over Decode memory budget + Rate limiter + Cache.

Prefer the glossary's canonical terms over the variant spellings in older code.

## Boundary with shttps

This context **consumes** [shttps](./shttps/CONTEXT.md) one-way. The four primary seam types are `shttps::Server`, `shttps::Connection`, `shttps::RequestHandler`, and `shttps::LuaServer`; SIPI subclasses `Server` (`SipiHttpServer`), registers `RequestHandler`s (`iiif_handler`, `file_handler`), and runs SIPI's three Lua entry points inside the request-scoped `LuaServer`.

### Cross-boundary naming gotchas

- shttps' **`RequestHandler`** is the C++ function-pointer that dispatches a request. SIPI's **Route handler** (in `UBIQUITOUS_LANGUAGE.md`) is a *Lua script* bound to a URL pattern. They are not the same thing: a SIPI Route handler is *invoked by* a `RequestHandler` that loads and runs the script. Keep both terms when discussing the seam.
- **"file_handler" is overloaded.** `shttps::file_handler` is the built-in static-file + Lua-in-HTML handler that SIPI registers on the configured `wwwroute` URL prefix; it reads from the **document root**. SIPI's IIIF `/file` endpoint (the **Bitstream** path-through) is the `FILE_DOWNLOAD` case inside `Sipi::iiif_handler` and reads from the **image root**. Same word, two unrelated handlers; always qualify which side.
- The **document root**, the **init script**, and the **connection-pool knobs** (`keep_alive_timeout`, `queue_timeout`, `max_waiting_connections`) are shttps concepts. SIPI sets their values from its config (`sipi.config.lua`) but does not own the concepts. See `shttps/CONTEXT.md`.

### Re-homing schedule (precondition for the Rust migration)

The following types live in shttps today but are SIPI domain concerns or generic utilities; they are scheduled to move SIPI-side so the planned Rust replacement of shttps does not need to reproduce them:

- `shttps::Hash` / `shttps::HashType` → SIPI preservation module (used by `SipiEssentials::pixel_checksum`).
- `shttps::Parsing` → SIPI-side support module.
- `shttps::Error` → folded into SIPI's error hierarchy.
- `shttps::Global`, `shttps::makeunique` → inline or SIPI-side support.

### Known layering leak

`shttps/Server.cpp` calls `SipiMetrics::instance()`. This violates the one-way SIPI → shttps direction and is a tracked bug, not a sanctioned pattern. The intended shape is a metrics-callback hook on `shttps::Server` that SIPI populates at startup.

## Relationships with shttps

- A SIPI **Image** or **Bitstream** request enters through an `shttps::RequestHandler` (`iiif_handler` / `file_handler`) registered on `shttps::Server`.
- The handler reads request data from the `shttps::Connection`, runs SIPI's **Preflight script** through the request-scoped `shttps::LuaServer`, and uses the resulting **Permission** to decide what to serve.
- SIPI's **Route handler** (Lua) is wired through a generic `RequestHandler` that loads the script into the same `LuaServer`.
- Response bytes (cache hit, decoded image, streamed bitstream) are written back through the same `Connection`.
