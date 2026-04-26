# Context Map

This repository has two bounded contexts. The boundary is real (separate namespace, library, build target, config template, test executable) and is being kept clean ahead of a planned Rust rewrite of the HTTP layer (strangler-fig).

## Contexts

- [SIPI image server](./CONTEXT.md) — IIIF Image API 3.0 implementation, image processing pipeline, cache, preservation metadata, IIIF/file route handlers. Lives under `src/`, `include/`, `include/iiifparser/`, `include/formats/`, `include/metadata/`. Namespace `Sipi::`.
- [shttps embedded HTTP framework](./shttps/CONTEXT.md) — generic multithreaded HTTP+SSL server, route registration, connection lifecycle, embedded Lua scripting, JWT helpers. Lives under `shttps/`. Namespace `shttps::`. Builds as the standalone `shttp` static library; ships a standalone `shttp-test` executable that proves it can stand alone.

## Direction of dependency

**SIPI → shttps. Strictly one-way.**

SIPI is a *consumer* of shttps: it subclasses `shttps::Server`, registers route handlers (`iiif_handler`, `file_handler`), and uses shttps types (`Connection`, `LuaServer`, `Parsing`, `Hash`) inside its own code. shttps must not name any `Sipi::` symbol or include any `Sipi*.h` header. Today there is one known violation (`shttps/Server.cpp` reaches into `SipiMetrics`); it is a bug, tracked as a layering leak.

## Long-term direction

The medium-term plan is to replace the C++ shttps with a Rust-based HTTP layer using the strangler-fig pattern: route by route, the Rust process takes over traffic until shttps is empty and can be deleted. Until then:

- The seam stays narrow and well-named so the migration can swap implementations route by route.
- New shttps features should be considered carefully — they expand what the Rust replacement must reproduce.
- New SIPI code that touches HTTP should go through documented seam types (`Connection`, `LuaServer`, route handlers), not through shttps internals.

See [docs/adr/0001-shttps-as-strangler-fig-target.md](./docs/adr/0001-shttps-as-strangler-fig-target.md).
