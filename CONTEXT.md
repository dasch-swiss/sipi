# SIPI image server

SIPI is the **IIIF subdomain implementation** of the **Access Area** bounded context in the wider [`dsp-repository`](https://github.com/dasch-swiss/dsp-repository) system. It serves IIIF Image API 3.0 and IIIF Presentation API requests over the OCFL-backed Preservation Files held by the `dsp-repository` Archive context — producing Access Files (image tiles, IIIF Manifests) for IIIF clients.

For the system-level view of where SIPI fits — the Archive context, the Access Area subdomain shapes (IIIF, HTML/DPE, Custom Presentation, Asset/Download, SPARQL), the Producer-side flow from VRE through RDU-Tooling, and the Access Area's other subdomain implementations — see the upstream [`dsp-repository` Context Map](https://github.com/dasch-swiss/dsp-repository/blob/main/CONTEXT-MAP.md).

## Published Language (from Access Area)

These three terms are **shared Published Language across contexts** at the system level. SIPI consumes them on input and produces them on output:

- **Preservation File** — owned by the Archive context. The canonical, long-term-stable byte stream stored in OCFL. SIPI reads these via the Archive's Binary retrieval API during Service File derivation.
- **Service File** — owned by the Access Area context. The derivative form optimised for fast access (e.g. pyramidal TIFF or JP2 with an `Essentials` carrier for SIPI). Materialised by an Access Area subdomain (this one) on subscription to Archive events.
- **Access File** — owned by the Access Area subdomain that serves the request. The on-the-wire form a Consumer actually receives — for SIPI, an IIIF tile (JPEG/PNG/TIFF/WebP) carved out of a Service File by the IIIF pipeline.

Use these terms in code, commits, ADRs, and PR descriptions when crossing the seam to the Archive or to another Access Area subdomain. Do not synonymize them with SIPI-internal vocabulary.

## Subdomain language (SIPI-local)

The canonical SIPI glossary is in [UBIQUITOUS_LANGUAGE.md](./UBIQUITOUS_LANGUAGE.md). It defines: Image vs Bitstream, Identifier (with embedded Page) + Prefix, Image root vs Document root, the IIIF pipeline terms (Region / Size / Rotation / Quality / Format / Decode level / Canonical URL / Cache key), Format handler vs Codec, Preservation metadata (umbrella) over Embedded metadata + Essentials packet, Image / Bitstream Information document, the three Lua entry points (Init script / Preflight script / Route handler), the seven Permission types, and the Throttling umbrella over Decode memory budget + Output size guard.

Prefer the glossary's canonical terms over the variant spellings in older code.

## Internal module: shttps

`src/shttps/` is an internal HTTP-framework module — multithreaded HTTP/HTTPS server, route registration, per-request embedded Lua scripting, JWT helpers. It is **not** a peer bounded context. SIPI subclasses `shttps::Server` (`Sipi::SipiHttpServer`), registers `shttps::RequestHandler`s (`iiif_handler`, `file_handler`), and runs SIPI's three Lua entry points inside the request-scoped `shttps::LuaServer`.

Module API surface (the four seam types — the documented strangler seam, ADR-0013): `Server`, `Connection`, `RequestHandler`, `LuaServer`. Full module documentation in [`src/shttps/README.md`](./src/shttps/README.md).

**Dependency direction:** strictly SIPI → shttps. shttps depends on the rest of `src/` only through `//src/logging:logging` — a generic levelled-logging primitive (not domain code), visibility-allowlisted as a shared support library. Enforcement: Bazel `visibility`; broader Bazel `--features=layering_check` rollout tracked under DEV-6353.

**Rust shell + retained oracle:** the HTTP server is the Rust shell (`//src/cli-rs:sipi` over the `//src/server-rs:lib` library), which drives the C++ image engine through the FFI seam. The C++ `shttps` transport + `SipiHttpServer` are retained in-tree as the differential-parity oracle — the reference binary the Rust shell is diffed against — and are removed after deploy. See [ADR-0013](docs/adr/0013-shttps-as-internal-module.md).

### Cross-module naming gotchas (kept verbatim from prior docs)

- shttps' **`RequestHandler`** is the C++ function-pointer that dispatches a request. SIPI's **Route handler** (in `UBIQUITOUS_LANGUAGE.md`) is a *Lua script* bound to a URL pattern. They are not the same thing: a SIPI Route handler is *invoked by* a `RequestHandler` that loads and runs the script. Keep both terms when discussing the seam.
- **"file_handler" is overloaded.** `shttps::file_handler` is the built-in static-file + Lua-in-HTML handler that SIPI registers on the configured `wwwroute` URL prefix; it reads from the **document root**. SIPI's IIIF `/file` endpoint (the **Bitstream** path-through) is the `FILE_DOWNLOAD` case inside `Sipi::iiif_handler` and reads from the **image root**. Same word, two unrelated handlers; always qualify which side.
- The **document root**, the **init script**, and the **connection-pool knobs** (`keep_alive_timeout`, `queue_timeout`, `max_waiting_connections`) are shttps concepts. SIPI sets their values from its config (`sipi.config.lua`) but does not own the concepts. See [`src/shttps/README.md`](./src/shttps/README.md).

## Extracted domain modules

The cutover survivors that used to live under `src/shttps/` — the generic
utilities (`shttps::Hash` / `HashType`, `shttps::Parsing`, `shttps::Error`,
`shttps::Global`), the JWT leaf, and the connection-less Lua runtime
(`LuaServer` + the `server.db` sqlite bindings) — have been extracted to
top-level packages: `src/util/`, `src/jwt/`, and `src/scripting/`. Their C++
symbols stay in `namespace shttps` (only the file/package location moved). No
production file includes anything under `src/shttps/` anymore; what remains
there is the oracle transport (`transport/`, `SipiHttpServer`), which the oracle
removal deletes wholesale. The retained transport still depends *back* on
`src/util` and `src/scripting` — interim reverse edges, visibility-allowlisted
and commented, that disappear with the oracle. See
[ADR-0013](docs/adr/0013-shttps-as-internal-module.md).
