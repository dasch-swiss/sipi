# Ubiquitous Language

Canonical terminology for the SIPI repository. SIPI is a IIIF Image API 3.0 server first; the rest of the surface (file streaming, embedded webserver, Lua extensibility) is layered on top.

## Resources and identification

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Image** | A pixel-bearing artefact processed through the IIIF pipeline (region, size, rotation, quality, format). The domain-level term. The *code-level* `Image` class is a narrow value type (geometry + photometric + RAII pixel buffer + metadata composite); image-processing behaviour lives in free functions in `image_processing/` per ADR-0007. | resource, media, asset |
| **Bitstream** | An opaque byte stream served as-is via the `/file` endpoint, bypassing IIIF processing. | file (as a domain noun), blob, payload |
| **Identifier** | The per-resource string carried in the URL between `{prefix}` and the IIIF parameters. Embeds an optional page ordinal for multi-page resources (PDF, multi-page TIFF). | id, fileid, file_id |
| **Prefix** | The URL segment in front of the identifier. Routes the request to a directory subtree under the image root and namespaces preflight resolution. | path prefix, route |
| **Image root** | The filesystem directory tree that identifiers and `/file` requests resolve into, with traversal validation. | imgroot (variable name only), storage root, repository root |
| **Document root** | The filesystem directory the embedded webserver serves static pages from. Distinct from the image root. | docroot (variable name only), web root |

## IIIF processing pipeline

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Region** | The rectangle of the image to be returned, expressed in IIIF form (`full` / `square` / `x,y,w,h` / `pct:x,y,w,h`). The same term covers the parsed form and the form clamped to image bounds. | crop, ROI, crop coords |
| **Size** | The output dimensions, expressed in IIIF form (`max` / `pct:n` / `w,` / `,h` / `w,h` / `!w,h`, optionally `^`-prefixed for upscale). | scale, dimensions |
| **Rotation** | The IIIF rotation parameter: a non-negative decimal `n`, optionally `!`-prefixed to mirror before rotating. | rotate, angle |
| **Quality** | The IIIF quality parameter: `default` / `color` / `gray` / `bitonal`. Independent of format. | colorspace |
| **Format** | The IIIF output format: `jpg` / `tif` / `png` / `jp2`. Independent of quality. Also reachable as `jpx` (alias of `jp2`). | output type, encoding |
| **Decode level** | The log2 downsampling factor applied at decode time so a smaller output can be produced without decoding full-resolution pixels. Negotiated by the size parser with the codec; meaningful for JPEG2000 and pyramid TIFF. | reduce, reduce factor, decimation |
| **Canonical URL** | The IIIF Image API canonical URL for a request. The IIIF spec form. | canonical-with-watermark |
| **Cache key** | The string SIPI uses to key the cache. Extends the canonical URL with a `/0` or `/1` watermark suffix, since watermark presence affects bytes but is not in the IIIF spec. | canonical URL (in cache contexts) |

## Image processing

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Image processing** | Umbrella term for the free-function module (`src/image_processing/`) over `const Image&`: crop, scale, rotate, colour conversion, channel ops, bit-depth reduction, dithering, watermark application, comparison, arithmetic. Replaces the ~12 image-processing methods on the legacy `SipiImage` god-object per ADR-0007. Free-function-over-value-type maps cleanly to Rust traits in the eventual port. | (none) |
| **Image shape** | The intrinsic shape of a source image: `(img_w, img_h, tile_w, tile_h, clevels, numpages, nc, bps)`. Read by a format handler from the master file via `read_shape()` (formerly `getDim()`). Stored in the *Essentials packet* so server-mode shape lookup can read at a known offset rather than parsing the codestream / TIFF tags. Per ADR-0004. | size record, dimensions |
| **Watermark** | Overlay image applied to an `Image` before serving when `Permission.watermark` is set. The path on `Permission.watermark` is loaded into a regular `Image` via `format_handlers/SipiIOTiff::read()`, then applied via free function `apply_watermark(Image& target, const Image& watermark)` in `image_processing/watermark.{h,cpp}`. Watermark presence extends the *Canonical URL* into the *Cache key* (`/0` or `/1` suffix). | overlay |

## Format handling

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Format handler** | A SipiIO subclass that adapts a codec to SIPI's read/write contract (SipiIOJ2k, SipiIOTiff, SipiIOPng, SipiIOJpeg). Lives in `src/format_handlers/`. | IO backend, format driver |
| **Codec** | A third-party library that performs the actual encode/decode. SIPI uses four: Kakadu (JP2), libtiff (TIFF), libpng (PNG), libjpeg (JPEG). A format handler *uses* a codec. (`webp` is in the project's external-deps set but no `SipiIOWebp` class exists today.) | library, backend |
| **Output sink** | Typed sum type for write-path I/O destinations: `std::variant<FilePath, StdoutSink, HttpSink, TeeSink>`. Format-handler `write()` API takes one, replacing magic-string sentinels (`"-"` for stdout, `"HTTP"` for HTTP server). `HttpSink` carries opaque write/finalize callbacks, so `format_handlers/` does not depend on `shttps/`. Per ADR-0006. | (none) |
| **Input source** | Typed sum type for read-path I/O sources: `std::variant<FilePath, RangeSource>`. Symmetric to *Output sink*. Format-handler `read()` and `read_shape()` API takes one, enabling the S3 transition per ADR-0004 without changing handler signatures. Per ADR-0006. | (none) |
| **Range source** | Variant alternative of *Input source* covering any backend that supports byte-range reads via callback: S3, Azure Blob, GCS, in-memory buffers. Names the *capability* (range reads), not the location (remote). | (none) |
| **Tee sink** | Composition primitive in the *Output sink* variant: `TeeSink { std::vector<OutputSink> outputs; }` broadcasts each output chunk to multiple sub-sinks. Preserves SIPI's existing dual-write optimization (encoder writes simultaneously to HTTP socket + cache file). Generalises to write-through to S3 / other sinks. | (none) |
| **ICC normalization** | The byte-level rewrite of bytes 24-35 (creation date) and 84-99 (Profile ID) inside `SipiIcc::iccBytes()`, gated by the *Reproducibility flag*. Test-only — production iccBytes() is the identity. | ICC scrubbing, ICC stripping (those imply removing profiles, not normalizing them) |
| **Reproducibility flag** | The `SOURCE_DATE_EPOCH` environment variable. When set, every ICC profile emitted by SipiIcc::iccBytes() has its creation date overwritten with the supplied epoch and its Profile ID zeroed; codec-bound emissions become byte-deterministic. CMake injects it for `sipi.approvaltests` only. | deterministic mode, test-only mode (the env var is the contract; "modes" obscure that) |

## Storage and preservation

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Service master** | The on-disk file under *Image root* that SIPI's server reads to fulfill IIIF requests. Codebase variables `infile` / `origpath`. Currently produced by SIPI CLI mode; future workflow has it derived from an *Archival master* by a separate conversion step. | master file |
| **Service master format** | The format of *service masters*. Optimized for fast random-access IIIF serving. Currently JP2; pyramidal TIFF is the planned successor. | master format |
| **Archival master** | The format-stable, lossless preservation copy of an image, stored in the OAIS-compliant external archive. Distinct from a *service master* by *purpose*: archival masters prioritize preservation properties (format stability, lossless or near-lossless compression, no service-side optimizations); service masters prioritize fast random access. SIPI CLI mode produces archival masters in the future workflow; SIPI server mode does not read them. | (none) |
| **Archival master format** | The format of *archival masters*. Per archival policy, plain (non-pyramidal) TIFF for format-stability reasons — pyramids are a service-side optimization, not a preservation property, and archival policy explicitly rejects them. | (none) |
| **Pyramidal TIFF** | Multi-resolution TIFF variant storing the same image at multiple decode levels in a single file. Supports efficient decode-level selection without full-resolution decoding. **A service-master-format only** — pyramids are a service-side optimization rejected by the archival master format. Planned successor to JP2 as the sole service master format. | (none) |
| **Object storage** | The production access model for service masters: SIPI server reads service masters via S3 range GETs over HTTP. Today's transitional state is NFS-mounted ZFS spinning disk (still network-accessed; round-trip costs already exist). Local-filesystem *Image root* becomes the dev/test scenario only. The *Cache* stays local in both states (performance optimization; cached representations on the hot path can't pay remote-access cost). | (none) |
| **Range GET** | An HTTP `GET` on an S3 object with a `Range:` header bounding the byte range to fetch. The unit of S3 access. Each range GET is a network round-trip (~1-10ms typical); minimizing them is the load-bearing perf goal once SIPI moves to S3. Per ADR-0004: pre-decode reads aim for *one* range GET to fetch the *Essentials packet* (with shape + file-structure offsets), then *one* targeted range GET for the data SIPI actually needs. | byte-range read |
| **Preservation metadata** | Umbrella term for all metadata SIPI manages across format conversions for long-term preservation. Comprises *Embedded metadata* and the *Essentials packet*. | sidecar metadata, image metadata |
| **Embedded metadata** | Third-party metadata standards SIPI carries through unchanged where possible: EXIF, IPTC, XMP, and ICC color profiles. | header metadata |
| **Essentials packet** | The SIPI-specific record embedded in image headers. **Role:** shape + S3-access file-structure index. **Contents:** *Image shape* (8 fields), per-format file-structure offsets (TIFF: per-IFD offset/size; JP2: codestream + per-resolution offsets), ICC profile (when destination format cannot embed natively), original filename / mimetype / hash type / *Pixel checksum*. **Wire format:** versioned CBOR per ADR-0005 (legacy: pipe-delimited text, brittle). **Position:** a known fixed prefix offset in the file (TIFF tag in the first IFD; JP2 UUID box near the start) so SIPI can fetch with one *Range GET* of the prefix. C++ class: `SipiEssentials`. | preservation packet, sipi metadata |
| **Pixel checksum** | A checksum (e.g. SHA-256) over the *uncompressed* pixel values, stored in the *Essentials packet* to verify that a format conversion did not alter image content. | data checksum, content hash |

## Endpoints and documents

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Image Information document** | The IIIF-spec JSON returned at `{prefix}/{identifier}/info.json` for an *Image*. Advertises supported region/size/rotation/quality/format forms via `extraFeatures` and `extraFormats`. | info.json (the file name), info doc |
| **Bitstream Information document** | The SIPI-specific JSON returned at `{prefix}/{identifier}/info.json` for a *Bitstream*: `@context`, `id`, `internalMimeType`, `fileSize`. Same URL shape as the *Image Information document*, distinct schema (`http://sipi.io/api/file/3/context.json`). | file info, bitstream info |
| **`/file` endpoint** | The URL form `{prefix}/{identifier}/file` that streams the underlying *Bitstream* as-is, bypassing IIIF processing. | file pass-through, raw endpoint |

## Lua extensibility

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Init script** | A Lua script (`sipi.init.lua`) executed once at server startup. Sets up global state shared across requests. | startup script, bootstrap |
| **Preflight script** | A Lua script invoked per request before serving. Returns a *Permission* and resolves the on-disk path of the resource. Implemented by the Lua function `pre_flight` (Image / IIIF) or `file_pre_flight` (Bitstream). | pre-flight script, authorization hook |
| **Route handler** (umbrella) | URL-pattern-bound request logic. Two sub-types depending on implementation language: *C++ route handler* and *Lua route handler*. | route, custom endpoint |
| **C++ route handler** | A `shttps::RequestHandler` callback registered at server startup. Examples: `iiif_handler`, `file_handler`, `health_handler`, `metrics_handler`, `favicon_handler`. Compiled in. Lives in `src/route_handlers/`. Routes are registered via the `register_routes(shttps::Server&, const ServerContext&)` free function in `route_handlers/route_handlers.h`, not by code inside the server lifecycle. Adding a new C++ route is a code change inside `route_handlers/`, not inside `server/`. Note: `shttps::RequestHandler` remains the framework type; *C++ route handler* is the SIPI-side domain term for instances of it. | (none) |
| **Lua route handler** | A Lua script bound to a URL pattern, loaded dynamically. Examples: `upload.lua`, `token.lua`, `orientation.lua`. **Role:** request-shaping only — preflight permission decisions, custom content endpoints. **Server-state mutation** (cache management, server lifecycle, config reload) is implemented as a *C++ route handler*, not a Lua script. See *Mutation script* (anti-pattern). | route, custom endpoint |
| **Lua bindings** | Umbrella term for SIPI's FFI clusters exposing C++ to Lua: `helper.*` (utility — `filename_hash`), `SipiImage` (datatype + 12 image-processing methods), and the preflight callbacks (`pre_flight` / `file_pre_flight`). Lives in `src/lua_bindings/`. | (none) |
| **Lua context** | Server-scope typed dependency bundle passed to Lua-binding C functions via shttps's `add_lua_globals_func(func, user_data=&lua_context)`. Replaces the historical `sipiserver` Lua lightuserdata global pointing at `SipiHttpServer*` (god-pointer). Carries the typed slice of server state Lua bindings actually use. **Server-scope** (set once at registration). Parallels *Server context* (the server-scope bundle for C++ route handlers). Lives in `lua_bindings/lua_context.h`. | (none) |
| **Permission** | The verdict and shaping output returned by a *Preflight script*. C++ representation: `Permission = std::variant<AllowPermission, LoginPermission, ClickthroughPermission, KioskPermission, ExternalPermission, RestrictPermission, DenyPermission>` — per-type structs for compile-time exhaustiveness. Each carries the appropriate sub-fields (`infile`, `watermark`, size caps, login-redirect URLs, etc.). The "DenyPermission with infile" anti-state is unrepresentable. Maps 1:1 to a Rust enum at port time. Lua-side parsing (LuaValstruct → Permission) lives in `lua_bindings/preflight.cpp`; route handlers consume only the typed value, never `LuaValstruct`. Lives in own package `permission/permission.h`. | access policy, ACL result |
| **Mutation script** (anti-pattern) | A *Lua route handler* that mutates **server state** (cache eviction, server lifecycle, filesystem cleanup, config reload, …). **Forbidden in SIPI.** The canonical surface for server-state mutation is a *C++ route handler* (or a signal handler for lifecycle). Cache state inspection is exposed exclusively through *Metrics*, never through Lua. Past examples that have been removed: `cache.lua`, `exit.lua`, `clean_temp_dir.lua`, `admin_upload.lua`, `debug.lua`. | admin script (when used to mutate state) |

### Permission types

The seven valid permission-type strings a preflight script may return:

| Type | Meaning |
| --- | --- |
| **allow** | Full access. Serve the requested representation. |
| **login** | Require user authentication, then serve. |
| **clickthrough** | Require an explicit user gesture (e.g. terms acceptance), then serve. |
| **kiosk** | Unauthenticated public-terminal mode. |
| **external** | Defer authorization to an external service. |
| **restrict** | Serve a degraded representation (size cap and/or watermark) instead of the requested one. |
| **deny** | Refuse the request (HTTP 401/403). |

## Throttling

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Throttling** | Umbrella term for SIPI's load-driven request-rejection policies. Comprises three sub-policies: *Decode memory budget* (process-wide instantaneous decode RAM), *Rate limiter* (per-client sliding-window pixel rate), *Output size guard* (intrinsic max-output-pixels ceiling). All three fire at one post-cache gate site per ADR-0008. Lives in `src/throttling/`. | backpressure (technically denotes upstream feedback flow control, which SIPI does not do — it rejects with HTTP 429/503/400), flow control, admission control (collides with *Permission*) |
| **Decode memory budget** | A process-wide, lock-free accounting of memory currently committed to in-flight image decodes, with an RAII guard. Rejects requests that would push concurrent decode memory over a configured ceiling. Returns HTTP 503 with `Retry-After`. Lives in `throttling/memory_budget.{h,cpp}`. Helper `estimate_peak_memory()` lives in `throttling/internal/peak_memory.h` (Test seam). | memory budget, decode budget |
| **Rate limiter** | The per-client sliding-window pixel-rate ceiling enforced post-cache (per ADR-0008). Returns HTTP 429 with `Retry-After`. Cache-hit responses are not rate-limited — the rate limiter exists to mitigate harvest bots, which sweep unique URLs (cache-miss-dominant). Client identity resolved via XFF-rightmost / peer-IP. Lives in `throttling/rate_limiter.{h,cpp}`; helper in `throttling/client_id.{h,cpp}`. | throttle |
| **Output size guard** | Stateless rejection of requests whose IIIF output dimensions exceed `max_pixel_limit` (i.e. `requested_w * requested_h > max_pixel_limit`). Returns HTTP 400 Bad Request. Distinct in *kind* from the other two Throttling sub-policies — its trigger is intrinsic (the request's output is too big), not load-dependent — but shares the gate-site location, the OFF/ENFORCE shape, and the protection-against-oversized-work purpose. Lives in `throttling/output_size_guard.{h,cpp}`. | output cap |
| **Cache** | A file-based LRU of generated representations, keyed by *Cache key*, with dual-limit eviction (total size **and** file count) and crash recovery. Cache state is exposed exclusively through *Metrics* (Prometheus), not through Lua bindings. Cache-hit responses **bypass all three Throttling policies entirely** (per ADR-0008): no rate-limit accounting, no memory-budget acquire, no output-size check. | response cache, output cache |
| **Cache pin** | Per-cachefile in-use refcount preventing eviction while a representation is being served. RAII type `BlockedScope` replaces manual `cache->check(infile, canonical, true)` paired with explicit `cache->deblock(cachefile)` calls. | cache lock |
| **Client abort** | An HTTP response write that fails because the peer is gone (FIN, RST, or write timeout). Surfaces in code as `Sipi::SipiImageClientAbortError`, raised when `shttps::OUTPUT_WRITE_FAIL` is thrown from a socket write. Logged at info, **not** captured to Sentry — these are peer-side events, not server faults. | broken pipe error, peer disconnect error |

## Observability

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Observability** | Umbrella term for the operational telemetry surface. Comprises three sub-concerns: *Metrics* (Prometheus instrumentation), *Sentry context* (per-image-error capture), and *Connection metrics adapter* (the canonical inversion-of-control bridge between `shttps::ConnectionMetrics` and SIPI's metrics registry). Lives in `src/observability/`. Distinct from *Logger* (which handles SIPI's structured-log primitives) — observability is the umbrella for what crosses the SIPI/shttps boundary as telemetry; logging is a SIPI-side utility. | telemetry |
| **Logger** | Basic logging primitives + level / mode control, used across both SIPI and shttps. Public API: `log_debug` / `log_info` / `log_warn` / `log_err`, `set_log_level` / `get_log_level`, plus four SIPI-only mode flags (`set_cli_mode`, `is_cli_mode`, `set_json_mode`, `is_json_mode`) that route logs to stderr when CLI mode emits a JSON document on stdout. Lives in `src/logging/`. The shttps consumption of Logger is documented as a known layering leak in `CONTEXT.md`. | logging |
| **Metrics** | The Prometheus instrumentation surface. ~25 metrics: counters (cache hits/misses/evictions/skips, image-too-large, client-disconnects, memory-alloc-failures, rate-limit decisions, decode-memory decisions, rejected-connections), gauges (waiting-connections, cache size/files, decode-memory-budget/used, rate-limit-clients-tracked, build-info), histograms (request duration, decode-memory estimate). Exposed at `GET /metrics` (`text/plain; version=0.0.4`). Singleton (the `shttps/Server.cpp → Metrics::instance()` consumption is documented as the first known layering leak in `CONTEXT.md`). Lives in `observability/metrics.{h,cpp}`. | telemetry, prom registry |
| **Sentry context** | The error-capture payload sent to Sentry from SIPI. Comprises an `ImageContext` struct (12 fields: `input_file`, `output_file`, `output_format`, `width`, `height`, `channels`, `bps`, `colorspace`, `icc_profile_type`, `orientation`, `file_size_bytes`, `request_uri`) plus the `capture_image_error(error_message, phase, ctx, mode)` entry point. `phase` is `"read" / "convert" / "write" / "cli_args"`. `mode` is `SipiMode::CLI` (blocking flush, 2s) or `SipiMode::Server` (non-blocking flush). Thread-safe (tags attached to event, not global scope). Lives in `observability/sentry.{h,cpp}`. | error context |
| **Connection metrics adapter** | The canonical inversion-of-control bridge for cross-context telemetry. shttps owns the `shttps::ConnectionMetrics` strategy interface (3 methods: `onConnectionsRejected`, `onWaitingConnectionsChanged`, `onRequestComplete`); SIPI installs a `Sipi::observability::ConnectionMetricsAdapter` instance on `shttps::Server` at startup that translates events into Prometheus updates on the *Metrics* singleton. shttps holds no reverse dependency on `Sipi::` symbols. **This is the prescribed pattern for resolving cross-boundary observability without violating one-way SIPI → shttps direction**. | (none) |

## Server architecture

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Server (Sipi side)** | `Sipi::Server` in `src/server/`, *composing* (not inheriting) `shttps::Server`. Composition aligns with the strangler-fig direction in ADR-0001: when shttps moves to Rust, only the composition target changes; SIPI's server class stays. Paper-thin (~180 lines): constructor, `run()` (realpath imgroot validation + `register_routes()` + delegation), runtime-resource owners. | (none) |
| **Server config** | Immutable C++ value type (`Sipi::config::ServerConfig`) carrying SIPI runtime configuration: 38 fields in 7 logical groups (Network/TLS, Image storage, Encoding, Cache, Lua, Webserver/Auth/Logging, Concurrency, Throttling). Constructed by `Sipi::config::to_server_config(const SipiConf&) → ServerConfig` after Lua parsing. Replaces the legacy 30-setter half-built-state pattern on `SipiHttpServer`. The C++ counterpart of `SipiConf` (Lua-bound parser); the runtime sees only `ServerConfig`, never `SipiConf`. Lives in `config/server_config.{h,cpp}`. | config struct |
| **Server context** | Typed, server-scope dependency bundle passed to `register_routes()` and stored as the `user_data` argument of shttps's `add_route()`. Contains the const-references and pointers each route handler actually needs (`Cache&`, `RateLimiter*`, `MemoryBudget*`, `const ServerConfig&`, etc.). Replaces the legacy `static_cast<SipiHttpServer*>(user_data)` god-pointer. **Server-scope** (set once at registration, shared across requests); request-scope state stays in function arguments. Parallels *Lua context*. Lives in `route_handlers/server_context.h`. | route context |
| **Operating mode** | Umbrella for the two ways SIPI runs: *CLI mode* and *Server mode*. The asymmetry between which format handler dominates read vs. write is architecturally load-bearing for the service-master-format fast path. The C++ enum `SipiMode { CLI, Server }` is the runtime tag (used by Sentry flush behaviour, error-reporting paths, and conversion-pipeline asymmetries). | mode |
| **CLI mode** | One-shot invocation. **Today**: reads arbitrary source format; writes a *Service master* in *Service master format* with full *Essentials packet*. **Future direction**: writes *Archival masters* for OAIS-compliant external archive storage. **Characteristics:** blocking Sentry flush, optional `--json` *CLI report* on stdout, `set_cli_mode(true)` redirects logs to stderr. Code-level boundary: `src/cli/cli_mode.{h,cpp}` (`run_query`, `run_compare`, `run_convert`). | one-shot mode |
| **Server mode** | Long-running HTTP server. Reads service masters in *Service master format* from *Image root*; writes IIIF representations to the *Cache*. The hot path for service-master-format shape reads. **Characteristics:** *Server config* value type at construction, three Throttling sub-policies at the post-cache gate, *Connection metrics adapter* installed on shttps at startup, non-blocking Sentry flush. Code-level boundary: `src/cli/server_mode.{h,cpp}` (`run_server`). | daemon mode, http mode |
| **CLI report** | The structured JSON document `emit_json_report` writes to stdout when the `--json` CLI flag is set. Schema mirrors *Sentry context*'s `ImageContext` so environments without a Sentry DSN still get the full diagnostic payload. Top-level keys: `status` (`"ok"` / `"error"`), `phase` (`"cli_args"` / `"read"` / `"convert"` / `"write"`), `error_message`, and an `image` object populated from `ImageContext`. On `phase == "cli_args"` the `image` object is omitted (no image was loaded yet). Lives in `cli/report.{h,cpp}`. | json output, json report |

## Code organization

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Test seam** | A header deliberately kept in a module's `internal/` subdirectory with `visibility` restricted to that module + that module's tests. Used to expose pure helpers for explicit testing without broadening production coupling. Canonical example: `metadata/internal/icc_normalization.h`. The pattern replaces comment-as-policy ("No production code outside X should include this header") with a build-graph invariant. | test backdoor, friend header |

## Platform context

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **IIIF** | International Image Interoperability Framework. SIPI implements IIIF Image API 3.0 at conformance Level 2. | (none) |
| **DaSCH** | The Swiss National Data and Service Center for the Humanities. The organisation that develops and maintains SIPI. | DASCH |
| **DSP** | DaSCH Service Platform. The broader platform SIPI is a component of. | DaSCH platform |

## Deprecated / legacy

These terms still appear in shipping surface but are not intended for new use.

| Term | Status | Note |
| --- | --- | --- |
| **Knora** | Deprecated | Legacy name for the data layer. Replaced by DSP. Do not coin new uses. |
| **knora.json document** | Deprecated | Legacy DSP-specific information document at `{prefix}/{identifier}/knora.json`. New consumers should use the *Image Information document*. |
| **`--knorapath` / `--knoraport`** | Deprecated | CLI flags / config keys (`knora_path`, `knora_port`) retained for compatibility. |

## Relationships

- An **Image** is served through the IIIF pipeline; a **Bitstream** is served via the `/file` endpoint.
- An **Identifier** plus a **Prefix** locates exactly one Image *or* Bitstream under the **Image root**.
- A request resolves to a **Permission** via the **Preflight script** before any decode happens.
- A **Format handler** uses exactly one **Codec** to read or write its format.
- Format handlers consume an **Input source** (read path) and emit to an **Output sink** (write path); the **Tee sink** composes multiple output sinks for SIPI's dual-write-to-HTTP-and-cache optimization.
- **Preservation metadata** = **Embedded metadata** ∪ **Essentials packet**; both travel with an Image across format conversions. The Essentials packet additionally indexes file-structure offsets so an **Object storage** read takes one **Range GET** of the prefix.
- **Service master** ⊂ **Service master format** is what server mode reads; **Archival master** ⊂ **Archival master format** is the OAIS preservation copy.
- The **Cache key** extends the **Canonical URL** with a *Watermark* bit; the **Cache** is keyed by it.
- **Throttling** = **Decode memory budget** + **Rate limiter** + **Output size guard**; all three fire at one post-cache gate. The **Cache** short-circuits before any of them.
- **Observability** = **Metrics** + **Sentry context** + **Connection metrics adapter**.
- **Operating mode** = **CLI mode** + **Server mode**; **CLI report** is exclusive to CLI mode.

## Example dialogue

> **Dev:** A request comes in for `/iiif/abc123/full/!500,500/0/default.jpg` — what's the first thing that runs?

> **Maintainer:** The **preflight script**. It receives the **prefix** (`iiif`), the **identifier** (`abc123`), and the request headers, and returns a **permission**. If the type is `restrict`, the permission carries a size cap and possibly a **watermark** path, both of which shape what we eventually serve.

> **Dev:** And the resolved on-disk file?

> **Maintainer:** Same permission — the `infile` field. The preflight script may rewrite it; we then validate the resolved path stays inside the **image root** before any I/O.

> **Dev:** What about `/iiif/abc123/file`?

> **Maintainer:** That's the **bitstream** surface. We treat `abc123` as a **bitstream**, not an **image**: no IIIF pipeline, no **decode level**, no cache. The `file_pre_flight` script runs instead of `pre_flight`, and `info.json` for that resource returns a **bitstream information document**, not an **image information document**.

> **Dev:** When does the **decode memory budget** come in?

> **Maintainer:** At the post-cache gate, alongside the **rate limiter** and the **output size guard** — the three **throttling** sub-policies all fire there. If the cache hits, we serve and the gate never runs. Otherwise: output-size guard first (cheapest, stateless, returns 400 if too big), then rate limiter (per-client, returns 429), then memory budget (returns 503 if admitting the decode would push us over). Only after all three pass does the request reach the **format handler** and the **codec**.

> **Dev:** And the **essentials packet** — that's only on write?

> **Maintainer:** Mostly, yes. We embed it when SIPI produces a converted file, so the **pixel checksum** and **image shape** travel with the new artefact. On read, we extract any existing essentials packet — that's how we recover an ICC profile when a JPEG2000 source couldn't natively embed one, and how server mode answers shape queries with one **range GET** instead of parsing the full codestream.

## Flagged ambiguities

- **"canonical URL"** was used in code for two different things: the IIIF-spec form and the SIPI cache-keying string with a watermark suffix. Resolved: **Canonical URL** = IIIF spec form only; **Cache key** = the SIPI extension. Code variables like `cannonical_watermark` (sic) are implementation; not part of the language.
- **"info.json"** is a single URL shape with two distinct response schemas depending on whether the resource is an Image or a Bitstream. Resolved by introducing two domain terms: **Image Information document** vs **Bitstream Information document**. Refer to them by their domain term, not the file name.
- **"preservation metadata"** was sometimes used narrowly (the Essentials packet) and sometimes broadly (everything embedded). Resolved: **Preservation metadata** is the umbrella; **Essentials packet** is the SIPI-specific subset; **Embedded metadata** is the third-party-standards subset.
- **"file"** is overloaded between the URL endpoint (`/file`), the on-disk artefact (`infile`, `imgroot`), and the served byte stream. Resolved: as a domain noun, **Bitstream** names the served byte stream; *file* survives only in URL paths and filesystem-level discussion.
- **"reduce"** appears as both a JPEG2000 codestream parameter and a TIFF resolution-level concept. Resolved: **Decode level** is the canonical domain term; *reduce* survives only as a codec-API parameter name.
- **"backpressure"** was used as the umbrella for SIPI's load-driven rejection policies. Resolved: technically denotes upstream feedback flow control (TCP windows, Reactive Streams), which SIPI does not do — SIPI rejects with HTTP 429/503/400. Renamed to **Throttling** to describe the rejection-style mechanism accurately and avoid colliding with **Permission** (the identity-driven authorization decision, also a form of admission control).
- **"master"** was used unqualified for both the SIPI-served file and the preservation copy. Resolved: **Service master** (in-server-path) vs **Archival master** (out-of-server-path; OAIS preservation); each with its own format term.
- **"route handler"** was overloaded between Lua scripts bound to URL patterns and `shttps::RequestHandler` C++ callbacks. Resolved: **Route handler** is the umbrella; **Lua route handler** and **C++ route handler** are the sub-types.
- **Knora** terms still ship in CLI flags, config, and the `knora.json` endpoint. Resolved: kept under the *Deprecated / legacy* section. Do not coin new uses.
