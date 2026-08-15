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

## The HTTP server: Rust shell over the C++ engine

The HTTP server is the Rust shell (`//src/cli-rs:sipi` over the `//src/server-rs:lib`
library, axum-based), which drives the C++ image engine through the FFI seam. There
is no C++ HTTP server: the retained `shttps` transport and `SipiHttpServer` — kept
in-tree through the strangler migration as the differential-parity oracle — have
been removed ([ADR-0020](docs/adr/0020-oracle-removal.md), which completes the
migration [ADR-0013](docs/adr/0013-shttps-as-internal-module.md) prepared). The
Rust shell owns the connection pool and its knobs (`max_waiting`, `queue_timeout`,
`nthreads`), route registration (axum for built-in endpoints, Lua for scripted
routes per [ADR-0017](docs/adr/0017-extensibility-lua-and-rust.md)), and IIIF URI
parsing (the standalone `//src/iiifparser/rust:iiif_parser` crate).

## Extracted domain modules (namespace `shttps`)

The C++ domain modules that used to live under `src/shttps/` survive the oracle
removal as ordinary top-level packages:

- `src/util/` — generic utilities (`shttps::Hash` / `HashType`, `shttps::Parsing`,
  `shttps::Error`, `shttps::Global`, `shttps::urldecode`).
- `src/jwt/` — the JWT verify/sign leaf.
- `src/scripting/` — the connection-less Lua runtime (`shttps::LuaServer` +
  `request_context.h` + the `server.db` sqlite bindings), the C++ side of SIPI's
  three Lua entry points.

Their C++ symbols keep `namespace shttps` (only the file/package location moved),
so a `shttps::` qualifier in code refers to one of these surviving modules, not to
a deleted HTTP transport. `src/shttps/` no longer exists.

### Naming clarification

SIPI's **Route handler** (in `UBIQUITOUS_LANGUAGE.md`) is a *Lua script* bound to a
URL pattern, run inside the request-scoped `shttps::LuaServer`. SIPI's IIIF `/file`
endpoint (the **Bitstream** path-through) is the `FILE_DOWNLOAD` case of the Rust
IIIF classifier (`//src/iiifparser/rust:iiif_parser`), which reads from the **image root**.
