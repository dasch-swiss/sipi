# Ubiquitous Language

Canonical terminology for the SIPI repository. SIPI is a IIIF Image API 3.0 server first; the rest of the surface (file streaming, embedded webserver, Lua extensibility) is layered on top.

> **Current vs. target.** The tables below describe the code **as it exists today** — every path and symbol named resolves in the tree. Two *current* ADRs commit to a structure that is not yet built: a free-function image-processing module (ADR-0007) and a typed read-path `Input source` (ADR-0006, ADR-0004). Those are collected under [Target shape (not yet built)](#target-shape-not-yet-built) at the end, never mixed into the current-state definitions. Note: the deployed server is the Rust shell; the C++ `SipiHttpServer` transport was removed (ADR-0020), and the older ADR-0001 C++-refactor targets for it (a composed `Sipi::Server`, `route_handlers/` / `permission/` packages) never happened — ADR-0013 repointed the rewrite at Rust.

## Resources and identification

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Image** | A pixel-bearing artefact processed through the IIIF pipeline (region, size, rotation, quality, format). The domain-level term. The *code-level* class is `Sipi::SipiImage` (`src/SipiImage.h`): today a god-object carrying geometry + photometric + RAII pixel buffer + metadata composite **and** ~12 image-processing methods (`crop`, `scale`, `rotate`, `convertToIcc`, `add_watermark`, …). ADR-0007 commits to narrowing it to a value type with the behaviour moved to a free-function module (see [Target shape](#target-shape-not-yet-built)). | resource, media, asset |
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
| **Parsed IIIF parameters** | The parsed Region/Size/Rotation/Quality/Format tuple, in two representations that must not be conflated (ADR-0021). The **domain** form is what the parser emits: idiomatic Rust value types `RegionKind` / `SizeKind` / `QualityKind` / `FormatKind` + `IiifParams` (`bool` flags) in the standalone `//src/iiifparser/rust:iiif_parser` crate; the **seam** (wire) form is the `#[repr(C)]` `SipiRegionType` / … / `SipiIiifParams` mirrors in `src/server-rs/src/ffi.rs`, guarded against the C++ header. `server-rs` is the sole place the domain form is flattened into the seam form (the `From<iiif_parser::IiifParams>` impls); nothing else uses the `Sipi*Type` family. | `SipiIiifParams` (as the in-process type), IIIF params (unqualified) |
| **Decode level** | The log2 downsampling factor applied at decode time so a smaller output can be produced without decoding full-resolution pixels. Negotiated by the size parser with the codec; meaningful for JPEG2000 and pyramid TIFF. | reduce, reduce factor, decimation |
| **Canonical URL** | The IIIF Image API canonical URL for a request. The IIIF spec form. | canonical-with-watermark |
| **Cache key** | The string SIPI uses to key the cache. Extends the canonical URL with a `/0` or `/1` watermark suffix, since watermark presence affects bytes but is not in the IIIF spec. | canonical URL (in cache contexts) |

## Image processing

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Image processing** | Umbrella term for SIPI's pixel operations over an image: crop, scale, rotate, colour conversion, channel ops, bit-depth reduction, dithering, watermark application, comparison, arithmetic. Today these are ~12 methods on the `Sipi::SipiImage` class (`src/SipiImage.{h,cpp}`). ADR-0007 commits to extracting them into a free-function module over a narrowed value type (see [Target shape](#target-shape-not-yet-built)). | (none) |
| **Image shape** | The intrinsic shape of a source image: `(img_w, img_h, tile_w, tile_h, clevels, numpages, nc, bps)`. Read by a format handler from a *Service File* via `SipiIO::read_shape()` (`src/SipiIO.h`; the file-based, no-full-decode probe — distinct from `SipiImage::getDim()`, which returns the dims of an already-loaded in-memory image). Stored in the *Essentials packet* so server-mode shape lookup can read at a known offset rather than parsing the codestream / TIFF tags. Per ADR-0004. | size record, dimensions |
| **Watermark** | Overlay image applied to an *Image* before serving when a `restrict` *Permission* carries a watermark path. Applied today via the method `SipiImage::add_watermark(const std::string& wmfilename)` (`src/SipiImage.{h,cpp}`), which loads the watermark file through the TIFF *Format handler* (`src/formats/SipiIOTiff`). Watermark presence extends the *Canonical URL* into the *Cache key* (`/0` or `/1` suffix). | overlay |

## Format handling

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Format handler** | A SipiIO subclass that adapts a codec to SIPI's read/write contract (SipiIOJ2k, SipiIOTiff, SipiIOPng, SipiIOJpeg). Lives in `src/formats/`. | IO backend, format driver |
| **Codec** | A third-party library that performs the actual encode/decode. SIPI uses four: Kakadu (JP2), libtiff (TIFF), libpng (PNG), libjpeg (JPEG). A format handler *uses* a codec. (`webp` is in the project's external-deps set but no `SipiIOWebp` class exists today.) | library, backend |
| **Output sink** | Typed sum type for write-path I/O destinations: `using OutputSink = std::variant<FilePath, CallbackSink, TeeSink>` in `src/formats/output_sink.h`. Format-handler `write()` API (`src/SipiIO.h`) takes one, replacing magic-string sentinels (`"-"` for stdout, `"HTTP"` for HTTP server). `CallbackSink` carries opaque write/finalize callbacks, so `src/formats/` does not depend on `shttps/`. Per ADR-0006. | (none) |
| **Tee sink** | Composition primitive in the *Output sink* variant: `TeeSink { std::vector<OutputSink> sinks; }` (`src/formats/output_sink.h`) broadcasts each output chunk to multiple sub-sinks. Preserves SIPI's existing dual-write optimization (encoder writes simultaneously to HTTP socket + cache file). Generalises to write-through to S3 / other sinks. Per ADR-0006. | (none) |
| **ICC normalization** | The byte-level rewrite of bytes 24-35 (creation date) and 84-99 (Profile ID) inside `Icc::iccBytes()`, gated by the *Reproducibility flag*. Test-only — production iccBytes() is the identity. | ICC scrubbing, ICC stripping (those imply removing profiles, not normalizing them) |
| **Reproducibility flag** | The `SOURCE_DATE_EPOCH` environment variable. When set, every ICC profile emitted by Icc::iccBytes() has its creation date overwritten with the supplied epoch and its Profile ID zeroed; codec-bound emissions become byte-deterministic. CMake injects it for `sipi.approvaltests` only. | deterministic mode, test-only mode (the env var is the contract; "modes" obscure that) |

## Files in the preservation pipeline

The three Files below are stages of the *Preservation pipeline* (see
that entry below). A file's stage is the outcome of going through the
corresponding `sipi convert *` subcommand and meeting its
prerequisites; it is recorded in the file's metadata (Essentials
packet presence for Service Files; future preservation-metadata
schema per ADR-0012 for Preservation Files). The stage is orthogonal
to file format — a JP2 may be a Service File (with packet) or an
Access File (without).

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Preservation File** | The ultimate, authoritative, bit-level data stream used for long-term bit preservation. Plain (non-pyramidal) lossless TIFF per archival policy. Carries rich preservation metadata (rights, provenance, PREMIS-shaped data; specified in future ADR-0012) through the *Embedded metadata* channel per ADR-0011. Stored in the OAIS-compliant external archive. SIPI server does not read these. Produced by future `sipi convert preservation-file <in> <out>` subcommand. *Renamed from: Archival master.* | archival master, preservation copy, archival copy |
| **Service File** | The high-quality "mezzanine" baseline read by SIPI server to fulfill IIIF requests. Pyramidal TIFF or JP2 — formats optimized for random-access IIIF serving. Carries an *Essentials packet* (SIPI-internal: identity + image shape + future file-structure offsets per ADR-0004, ADR-0005). Codebase variables `infile` / `origpath`. Derived from a *Preservation File* or directly from an arbitrary source. Produced by `sipi convert service-file <in> <out>` subcommand (per ADR-0010). *Renamed from: Service master.* | service master, master file, service copy |
| **Service File format** | The format of *Service Files*: pyramidal TIFF or JP2. Optimized for fast random-access IIIF serving. Currently JP2; pyramidal TIFF is the planned successor. *Renamed from: Service master format.* | service master format, master format |
| **Preservation File format** | The format of *Preservation Files*: plain (non-pyramidal) lossless TIFF per archival policy. Pyramids are a service-side optimization rejected for preservation. *Renamed from: Archival master format.* | archival master format |
| **Access File** | The highly compressed, end-user-facing derivative produced for web delivery, streaming, or download. Any format the operator or IIIF client requests (JPEG, PNG, plain TIFF, JP2, etc.). **No Essentials packet** (per ADR-0009). Carries the *Embedded metadata* subset propagated from its source *Service File*, plus an IIIF-server-emitted provenance event for the transformation that produced this file (per ADR-0011). Produced either by (a) the IIIF server in response to an Image API request, or (b) the `sipi convert access-file <in> <out>` subcommand (offline batch; input must be a Service File). Bare `sipi convert <in> <out>` also produces an Access File — the generic ImageMagick-style conversion path. | IIIF derivative, representation (in generic contexts), output file, access copy |
| **Pyramidal TIFF** | Multi-resolution TIFF variant storing the same image at multiple decode levels in a single file. Supports efficient decode-level selection without full-resolution decoding. **A Service File format only** — pyramids are a service-side optimization rejected by the Preservation File format. Planned successor to JP2 as the sole Service File format. | (none) |

## Storage and access

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Object storage** | The production access model for Service Files: SIPI server reads them via S3 range GETs over HTTP. Today's transitional state is NFS-mounted ZFS spinning disk (still network-accessed; round-trip costs already exist). Local-filesystem *Image root* becomes the dev/test scenario only. The *Cache* stays local in both states (performance optimization; cached representations on the hot path can't pay remote-access cost). | (none) |
| **Range GET** | An HTTP `GET` on an S3 object with a `Range:` header bounding the byte range to fetch. The unit of S3 access. Each range GET is a network round-trip (~1-10ms typical); minimizing them is the load-bearing perf goal once SIPI moves to S3. Per ADR-0004: pre-decode reads aim for *one* range GET to fetch the *Essentials packet* (with shape + file-structure offsets), then *one* targeted range GET for the data SIPI actually needs. | byte-range read |
| **Preservation metadata** | Umbrella term for all metadata SIPI manages across format conversions for long-term preservation. Comprises *Embedded metadata* (the standards-based XMP/IPTC/EXIF channel) and the *Essentials packet* (SIPI-internal). Per ADR-0011: rights / provenance / PREMIS-shaped data travel through the Embedded metadata side of the union (XMP), not through the Essentials packet. | sidecar metadata, image metadata |
| **Embedded metadata** | Third-party metadata standards SIPI carries through unchanged where possible: EXIF, IPTC, XMP, and ICC color profiles. The propagation path for *rights / provenance / PREMIS* fields per ADR-0011 — SIPI's format-handler writers emit these via each format's native carrier (JPEG XMP marker, TIFF `XMLPACKET`, JP2 UUID box, PNG iTXt). | header metadata |
| **Essentials packet** | The SIPI-specific record embedded in *Service File* headers. **Role:** identity + shape + S3-access file-structure index. **Contents:** *Image shape* (8 fields), per-format file-structure offsets (TIFF: per-IFD offset/size; JP2: codestream + per-resolution offsets — future, per ADR-0004), ICC profile (when destination format cannot embed natively), original filename / mimetype / hash type / *Pixel checksum*. **Scope:** SIPI-internal — does **not** carry rights / provenance / PREMIS (those ride the *Embedded metadata* channel per ADR-0011). **Wire format:** versioned protobuf per ADR-0005 (legacy: pipe-delimited text, brittle). **Position:** a known fixed prefix offset in the file (TIFF tag in the first IFD; JP2 UUID box near the start) so SIPI can fetch with one *Range GET* of the prefix. **Presence:** Service Files only; Access Files do not carry one. C++ class: `Essentials`. | preservation packet, sipi metadata |
| **Pixel checksum** | A checksum (e.g. SHA-256) over the *uncompressed* pixel values, stored in the *Essentials packet* to verify that a format conversion did not alter image content. Computed on the post-transformation pixel buffer by the `convert service-file` command (per ADR-0010). | data checksum, content hash |
| **Preservation pipeline** | The chain of intentional SIPI subcommands that move a file through preservation stages: an arbitrary source → `convert preservation-file` → *Preservation File* (future, ADR-0012); a Preservation File → `convert service-file` → *Service File*; a Service File → `convert access-file` → *Access File*. Each step has documented prerequisites the command checks at entry, and produces a specific output with the metadata appropriate to its stage. The architectural property: each File's stage in the pipeline is the *outcome* of going through the corresponding subcommand and meeting its prerequisites, not a label the file carries. See `docs/preservation-pipeline.md` for the prerequisites + effects matrix. Per ADR-0010. | role-creating command, master-creation command, master-creation orchestrator |
| **Corruption tripwire** | The repurposed pixel-hash-verify branch in `SipiImage::readSource`: when a read happens to encounter a file with an existing *Essentials packet*, the recomputed pixel hash is compared against `data_chksum`; on mismatch, log ERROR (and increment `sipi_essentials_hash_mismatch_total{format}`) and continue. Not a hard gate — the operator's deliberate integrity check is `sipi verify service-file <file>`. Per ADR-0010. | preservation guard |
| **`sipi_essentials_uuid`** | The fixed RFC 4122 v4 UUID `7B28A646-B9C3-4FB2-900B-B6855DF23882` that identifies SIPI's *Essentials packet* inside a JP2 UUID box. Compile-time constant in `src/formats/SipiIOJ2k.cpp`. JP2 readers walk top-level UUID boxes and pattern-match the first 16 bytes against this constant; JP2 writers emit the UUID followed by the protobuf payload when the in-memory *Essentials packet* is set on the image (the writer's emit gate per ADR-0010; only the `convert service-file` command sets the packet in production). Distinct from JP2's other in-spec UUIDs (XMP, IPTC, EXIF). Per ADR-0005. | SIPI UUID |
| **JP2 UUID-box carrier** | The transport mechanism by which SIPI's *Essentials packet* rides inside a JP2 Service File: a top-level UUID box at **slot 4** (after JP2 Signature → FTYP → `jp2h`, before the `jp2c` codestream box). Layout = `[sipi_essentials_uuid (16 bytes)][protobuf payload]`. Replaces the pre-DEV-6537 codestream-comment carrier (`SIPI:` 5-byte prefix), which remains read-only via `Essentials::parse_legacy`. The 64 KB-prefix invariant means the UUID box is reachable from a single bounded *Range GET*. Per ADR-0005. | SIPI UUID box, JP2 essentials box |
| **`format_version`** | A `uint32` field at proto field number 1 of the *Essentials packet* schema, used as the dispatcher discriminator in `Essentials::parse`. The current writer emits `format_version=1`; readers reject `0` (proto3 default — field never set) as `ParseError::MissingVersion` and `>1` as `ParseError::UnknownVersion`. The format-level evolution lever — additive schema changes don't bump it; breaking semantic redefinitions do. Per ADR-0005 schema discipline. | version, schema version |
| **`read_shape` fast path** | The optimisation in *Service File* format handlers (`SipiIOJ2k`, `SipiIOTiff` pyramidal) whereby `read_shape()` reads the *Essentials packet* from a known fixed prefix, parses it, and returns *Image shape* directly when both `img_w` and `img_h` are non-zero — skipping the codestream-create / IFD-walk that would otherwise be needed. Activates incrementally as files are re-processed by `sipi convert service-file`. Falls through to format-native parsing otherwise. Outcomes attributed to `sipi_read_shape_fast_path_total{format, outcome}` where outcome ∈ {`hit`, `miss`, `partial`, `fallback`}. Per ADR-0004. | shape fast path, packet shape lookup |

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
| **Route handler** (umbrella) | URL-pattern-bound request logic. Built-in endpoints are Rust axum routes on the shell's `Router` (`src/server-rs/src/routes.rs`); scripted endpoints are *Lua route handlers*. | route, custom endpoint |
| **Lua route handler** | A Lua script bound to a URL pattern, loaded dynamically. Examples: `upload.lua`, `token.lua`, `orientation.lua`. **Role:** request-shaping only — preflight permission decisions, custom content endpoints. **Server-state mutation** (cache management, server lifecycle, config reload) is implemented as a *C++ route handler*, not a Lua script. See *Mutation script* (anti-pattern). | route, custom endpoint |
| **Lua bindings** | Umbrella term for SIPI's FFI clusters exposing C++ to Lua: the `helper_methods` table (utility — `filename_hash`) and the `SipiImage` Lua datatype (`SImage_new`, `SImage_dims`, …), plus the preflight callbacks (`pre_flight` / `file_pre_flight`). Live in `src/ffi/SipiLua.{h,cpp}` (entry point `sipiGlobals`); the framework-level Lua machinery (`LuaServer`, `LuaValstruct`, `RequestContext`) lives in `src/scripting/`. | (none) |
| **Permission** | The verdict and shaping output returned by a *Preflight script*. Valid types: allow / login / clickthrough / kiosk / external / restrict / deny (see *Permission types*). C++ representation today is the enum `SipiPermType` (`src/ffi/sipi_ffi.h`); the Lua-returned permission map is parsed in `src/ffi/preflight.cpp` (`perm_from_string`), and the resolved on-disk path + shaping (`infile`, `watermark`, size caps) travel the FFI seam's key/value channel. (A typed `std::variant<AllowPermission, …>` in a `permission/` package was an ADR-0001 target that lapsed under ADR-0013.) | access policy, ACL result |
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
| **Throttling** | Umbrella term for SIPI's load-driven request-rejection policies. Comprises two sub-policies: *Decode memory budget* (process-wide instantaneous decode RAM) and *Output size guard* (intrinsic max-output-pixels ceiling). Both fire inline at one post-cache gate in `src/ffi/serve_image.cpp`, the placement ADR-0008 established (there is no dedicated `throttling/` module — the gates live in the serve path). | backpressure (technically denotes upstream feedback flow control, which SIPI does not do — it rejects with HTTP 503/400), flow control, admission control (collides with *Permission*) |
| **Decode memory budget** | A process-wide, lock-free accounting of memory currently committed to in-flight image decodes, with an RAII guard. Rejects requests that would push concurrent decode memory over a configured ceiling. Returns HTTP 503 with `Retry-After`. `class Sipi::SipiMemoryBudget` + `MemoryBudgetGuard` + `enum class MemoryBudgetMode { OFF, MONITOR, ENFORCE }` in `src/SipiMemoryBudget.{h,cpp}`; the peak-memory helper is `estimate_peak_memory()` in `src/SipiPeakMemory.h`. The acquire site is `src/ffi/serve_image.cpp`. | memory budget, decode budget |
| **Output size guard** | Stateless rejection of requests whose IIIF output dimensions exceed `max_pixel_limit` (i.e. `requested_w * requested_h > max_pixel_limit`). Returns HTTP 400 Bad Request. Distinct in *kind* from the other Throttling sub-policy — its trigger is intrinsic (the request's output is too big), not load-dependent — but shares the gate-site location and the protection-against-oversized-work purpose. Implemented inline at the post-cache gate in `src/ffi/serve_image.cpp` (against the engine-context `max_pixel_limit`). | output cap |
| **Cache** | A file-based LRU of generated representations, keyed by *Cache key*, with dual-limit eviction (total size **and** file count) and crash recovery. `class SipiCache` in `src/SipiCache.{h,cpp}`. Cache state is exposed exclusively through *Metrics*, not through Lua bindings. Cache-hit responses **bypass both Throttling policies entirely** (per ADR-0008): no memory-budget acquire, no output-size check. | response cache, output cache |
| **Preflight cache** | A burst-coalescing cache of the Lua `pre_flight` access decision (the *Permission* + resolved path), so a repeated request serves the recorded decision instead of re-running the hook — a deep-zoom viewer fires `pre_flight` once per tile, all sharing one *Identifier*. Distinct from the *Cache* (which stores generated representations, not access decisions). Opt-in (`--preflight-cache-ttl <secs>` / `SIPI_PREFLIGHT_CACHE_TTL`, default 0 = off); the TTL bounds staleness on a permission change. `mod preflight_cache` in `src/server-rs`. Correct **only** for a hook whose decision is a pure function of its **Preflight cache key** = `(prefix, identifier, Cookie, Authorization)`; a hook that reads any unkeyed request field (other headers, host, client IP) may be served a wrong cached decision. | pre-flight cache, auth cache |
| **Cache pin** | Keeping a cache file in use so it is not evicted while a representation is being served. Today this is manual and non-RAII: `SipiCache::check(origpath, canonical, /*block_file=*/true)` (`src/SipiCache.h`) inserts into the `blocked_files` map, and `SipiCache::deblock(res)` removes it — the caller must remember the `deblock`. (Wrapping this in an RAII `BlockedScope` was an ADR-0001 target that lapsed under ADR-0013.) | cache lock |
| **Client abort** | An HTTP response write that fails because the peer is gone (FIN, RST, or write timeout). Surfaces in code as `Sipi::SipiImageClientAbortError` (`src/SipiImageError.h`), raised when `shttps::OUTPUT_WRITE_FAIL` is thrown from a socket write. Logged at info, **not** captured to Sentry — these are peer-side events, not server faults. | broken pipe error, peer disconnect error |

## Observability

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Observability** | Umbrella term for the operational telemetry surface. Comprises two sub-concerns: *Metrics* (atomic-counter instrumentation exported over OTLP) and *Sentry context* (per-image-error capture). Lives in `src/observability/`. Distinct from *Logger* (which handles SIPI's structured-log primitives). | telemetry |
| **Logger** | Basic logging primitives + level / mode control, used across the codebase. Public API: `log_debug` / `log_info` / `log_warn` / `log_err`, `set_log_level` / `get_log_level`, plus four SIPI-only mode flags (`set_cli_mode`, `is_cli_mode`, `set_json_mode`, `is_json_mode`) that route logs to stderr when CLI mode emits a JSON document on stdout. Lives in `src/logging/`, a generic primitive any module may depend on. | logging |
| **Metrics** | The instrumentation surface. The engine's singleton in `observability/metrics.{h,cpp}` is plain lock-free atomics (`Counter` / `Gauge`): counters (cache hits/misses/evictions/skips, image-too-large, client-disconnects, memory-alloc-failures, decode-memory decisions, rejected-connections, the label-fanned read-shape-fast-path and essentials-hash-mismatch families, tiff-pyramid-reduced-decodes) and gauges (waiting-connections, cache size/files/limits, decode-memory budget/used). **Production is OTLP:** the 20 scalar fields cross the FFI seam as `SipiMetricsSnapshot` (`ffi/sipi_ffi.cpp`) and re-register as OTel observable instruments in `server-rs/src/metrics.rs`. Distributions are recorded shell-side as OTel histograms (`http.server.request.duration`, `sipi.decode_memory.estimate_bytes`); the build stamp travels as the resource attributes `service.version` + `vcs.ref.head.revision`. The label-fanned read-shape / essentials counters stay engine-internal (not snapshotted). | telemetry, snapshot bridge |
| **Sentry context** | The error-capture payload for a handled (non-crash) image error. The engine itself calls no Sentry SDK: it populates an `ImageContext` struct (11 fields: `input_file`, `output_file`, `output_format`, `width`, `height`, `channels`, `bps`, `colorspace`, `icc_profile_type`, `orientation`, `file_size_bytes`; lives in `src/populate_from_image.h`), flattens it into the FFI seam's `SipiImageErrorReport` struct (`ffi/sipi_ffi.h`), and hands it across via the `report_error`/`report_ctx` callback pair on `SipiServeRequest`. `Sipi::ffi::report_image_error` (`server-rs/src/ffi.rs`) is what actually builds and captures the `sentry::Event`, tagged `sipi.phase` (`"read"` / `"convert"` / `"write"`) and `sipi.mode=server`. Only *Server mode* reports handled image errors this way — *CLI mode* stays log-only + the *CLI report* JSON document (D1); native crashes go through a separate out-of-process minidump reporter (`sentry-rust-minidump`), not this seam. | error context |

## Server architecture

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Server** | The production HTTP server is the Rust shell (`//src/cli-rs:sipi` over `//src/server-rs:lib`), axum-based, which drives the C++ image engine over the FFI seam. There is no C++ server: the `SipiHttpServer` class that inherited `shttps::Server` was removed with the oracle ([ADR-0020](docs/adr/0020-oracle-removal.md)). | (none) |
| **Server config** | SIPI runtime configuration read from the Lua config. Today this is `class SipiConf` (`include/SipiConf.h`, impl `src/SipiConf.cpp`) — a settings-holder the server reads accessors off. (The FFI CLI/env override channel is a separate `SipiServerConfig` struct, `src/server-rs/src/config.rs` ↔ `src/ffi/sipi_ffi.h`.) | config struct |
| **Operating mode** | Umbrella for the two ways SIPI runs: *CLI mode* and *Server mode*. The asymmetry between which format handler dominates read vs. write is architecturally load-bearing for the Service-File-format fast path. Distinguished by the `set_cli_mode`/`is_cli_mode` Logger flag (`src/logging/logger.{h,cpp}`) — there is no runtime `SipiMode` enum, and no dedicated mode-module pair; the `sipi.mode=server` Sentry tag (see *Sentry context*) is hardcoded on the one path that reports handled image errors at all. | mode |
| **CLI mode** | One-shot invocation. Verb-noun subcommand surface (per ADR-0010): generic verbs (`convert`, `verify`, `query`, `compare`) are ImageMagick-style utilities usable by anyone; DSP-specific verbs (`convert service-file`, `convert access-file`, future `convert preservation-file`; `verify service-file`, `verify access-file`, future `verify preservation-file`) enforce preservation-chain semantics. **Characteristics:** optional `--json` *CLI report* on stdout, `set_cli_mode(true)` redirects logs to stderr; handled image errors are not reported to Sentry on this path (D1) — only panics are, via the Rust shell's process-wide panic integration. Code-level boundary: the CLI11 subcommand callbacks in `src/cli/cli_app.cpp` and the handlers in `src/cli/commands/`. | one-shot mode |
| **Server mode** | Long-running HTTP server. Reads *Service Files* in *Service File format* from *Image root*; writes *Access Files* (via the IIIF pipeline) to the *Cache* and HTTP response. The hot path for *Service File*-format shape reads. Conceptually `convert access-file` over HTTP — the IIIF server applies IIIF Image API parameters to a Service File and emits an Access File response. **Characteristics:** Throttling at the post-cache gate, handled image errors reported to Sentry via the FFI seam (see *Sentry context*). Code-level boundary: the Rust shell's `server` subcommand (`src/cli-rs`) over the axum app in `src/server-rs`. | daemon mode, http mode |
| **CLI report** | The structured JSON document `emit_json_report` writes to stdout when the `--json` CLI flag is set. Schema mirrors *Sentry context*'s `ImageContext` so environments without a Sentry DSN still get the full diagnostic payload. Top-level keys: `status` (`"ok"` / `"error"`), `phase` (`"cli_args"` / `"read"` / `"convert"` / `"write"`), `error_message`, and an `image` object populated from `ImageContext`. On `phase == "cli_args"` the `image` object is omitted (no image was loaded yet). Declared in `include/SipiReport.h`, implemented in `src/SipiReport.cpp`; the mirrored struct is `observability::ImageContext` (`src/populate_from_image.h`). | json output, json report |

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

## Target shape (not yet built)

Shapes a **current** ADR commits to but which do not exist in the tree yet. The
current-state definitions above describe how each concept is implemented **today**;
these entries describe where the cited ADR intends it to go. Superseded-ADR targets
(the ADR-0001 C++-server refactors — a composed `Sipi::Server`, `route_handlers/`,
`permission/`, `ServerContext`, `BlockedScope`) are **not** listed here: ADR-0013
repointed the rewrite at Rust and the C++ transport has since been removed
(ADR-0020), so those refactors are no longer intended.

| Target | Committing ADR | Shape |
| --- | --- | --- |
| **Image-processing free-function module** | ADR-0007 | Extract the ~12 image-processing methods off the `SipiImage` god-object into free functions over a narrowed `Image` value type in `src/image_processing/` (`crop`, `scale`, `rotate`, colour conversion, `apply_watermark(Image& target, const Image& watermark)` in `image_processing/watermark.{h,cpp}`, …). Free-function-over-value-type maps cleanly to Rust traits at port time. Today: methods on `Sipi::SipiImage`. |
| **Input source** (typed read-path) | ADR-0006, ADR-0004 | A read-path sum type `using InputSource = std::variant<FilePath, RangeSource>`, symmetric to the built *Output sink*, taken by `SipiIO::read()` / `read_shape()` so the S3 transition (ADR-0004) needs no handler-signature change. **Range source** is its variant alternative for any byte-range-read backend (S3, Azure Blob, GCS, in-memory) — names the *capability*, not the location. Today: `read()`/`read_shape()` take a plain `const std::string& filepath`; no read-path variant exists. |

## Relationships

- An **Image** is served through the IIIF pipeline; a **Bitstream** is served via the `/file` endpoint.
- An **Identifier** plus a **Prefix** locates exactly one Image *or* Bitstream under the **Image root**.
- A request resolves to a **Permission** via the **Preflight script** before any decode happens.
- A **Format handler** uses exactly one **Codec** to read or write its format.
- Format handlers read from a filepath and emit to an **Output sink** (write path); the **Tee sink** composes multiple output sinks for SIPI's dual-write-to-HTTP-and-cache optimization. (A symmetric typed **Input source** read path is a [target shape](#target-shape-not-yet-built), not yet built.)
- **Preservation metadata** = **Embedded metadata** ∪ **Essentials packet**; both travel with an Image across format conversions. The Essentials packet additionally indexes file-structure offsets so an **Object storage** read takes one **Range GET** of the prefix. Per ADR-0011, rights / provenance / PREMIS fields travel on the **Embedded metadata** side of the union (via XMP), not in the Essentials packet.
- The **Preservation pipeline** chains: **Preservation File** (long-term bit-level preservation) → **Service File** (mezzanine baseline read by SIPI server) → **Access File** (end-user delivery). Each step is intentional (per ADR-0010): a file's stage is the outcome of the operator's CLI invocation at creation time, never inferred from format. **Service File** ⊂ **Service File format** is what server mode reads; **Preservation File** ⊂ **Preservation File format** is the OAIS preservation copy.
- The **Cache key** extends the **Canonical URL** with a *Watermark* bit; the **Cache** is keyed by it.
- **Throttling** = **Decode memory budget** + **Output size guard**; both fire at one post-cache gate. The **Cache** short-circuits before any of them.
- **Observability** = **Metrics** + **Sentry context**.
- **Operating mode** = **CLI mode** + **Server mode**; **CLI report** is exclusive to CLI mode.

## Example dialogue

> **Dev:** A request comes in for `/iiif/abc123/full/!500,500/0/default.jpg` — what's the first thing that runs?

> **Maintainer:** The **preflight script**. It receives the **prefix** (`iiif`), the **identifier** (`abc123`), and the request headers, and returns a **permission**. If the type is `restrict`, the permission carries a size cap and possibly a **watermark** path, both of which shape what we eventually serve.

> **Dev:** And the resolved on-disk file?

> **Maintainer:** Same permission — the `infile` field. The preflight script may rewrite it; we then validate the resolved path stays inside the **image root** before any I/O.

> **Dev:** What about `/iiif/abc123/file`?

> **Maintainer:** That's the **bitstream** surface. We treat `abc123` as a **bitstream**, not an **image**: no IIIF pipeline, no **decode level**, no cache. The `file_pre_flight` script runs instead of `pre_flight`, and `info.json` for that resource returns a **bitstream information document**, not an **image information document**.

> **Dev:** When does the **decode memory budget** come in?

> **Maintainer:** At the post-cache gate, alongside the **output size guard** — the two **throttling** sub-policies both fire there. If the cache hits, we serve and the gate never runs. Otherwise: output-size guard first (cheapest, stateless, returns 400 if too big), then memory budget (returns 503 if admitting the decode would push us over). Only after both pass does the request reach the **format handler** and the **codec**.

> **Dev:** And the **essentials packet** — that's only on write?

> **Maintainer:** Only on intentional write — specifically `sipi convert service-file`. Plain `sipi convert` (the generic ImageMagick-style verb) doesn't emit one, even if the output happens to be a *Service File format* like JP2 — that output would be an *Access File* by intent. On read, we extract any existing essentials packet — that's how we recover an ICC profile when a JPEG2000 source couldn't natively embed one, and how server mode answers shape queries with one **range GET** instead of parsing the full codestream. The **corruption tripwire** in `readSource` logs ERROR if the packet's checksum doesn't match the recomputed pixel hash — that's the passive integrity signal; the active deliberate check is `sipi verify service-file <file>`.

## Flagged ambiguities

- **"canonical URL"** was used in code for two different things: the IIIF-spec form and the SIPI cache-keying string with a watermark suffix. Resolved: **Canonical URL** = IIIF spec form only; **Cache key** = the SIPI extension. Code variables like `cannonical_watermark` (sic) are implementation; not part of the language.
- **"info.json"** is a single URL shape with two distinct response schemas depending on whether the resource is an Image or a Bitstream. Resolved by introducing two domain terms: **Image Information document** vs **Bitstream Information document**. Refer to them by their domain term, not the file name.
- **"preservation metadata"** was sometimes used narrowly (the Essentials packet) and sometimes broadly (everything embedded). Resolved: **Preservation metadata** is the umbrella; **Essentials packet** is the SIPI-specific subset; **Embedded metadata** is the third-party-standards subset.
- **"file"** is overloaded between the URL endpoint (`/file`), the on-disk artefact (`infile`, `imgroot`), and the served byte stream. Resolved: as a domain noun, **Bitstream** names the served byte stream; *file* survives only in URL paths and filesystem-level discussion.
- **"reduce"** appears as both a JPEG2000 codestream parameter and a TIFF resolution-level concept. Resolved: **Decode level** is the canonical domain term; *reduce* survives only as a codec-API parameter name.
- **"backpressure"** was used as the umbrella for SIPI's load-driven rejection policies. Resolved: technically denotes upstream feedback flow control (TCP windows, Reactive Streams), which SIPI does not do — SIPI rejects with HTTP 503/400. Renamed to **Throttling** to describe the rejection-style mechanism accurately and avoid colliding with **Permission** (the identity-driven authorization decision, also a form of admission control).
- **"master"** was used unqualified for both the SIPI-served file and the preservation copy, and the IIIF-derivative concept was left implicit. **Resolved** (per ADR-0009): three-tier *Preservation pipeline* — **Preservation File** (long-term bit-level preservation; was: Archival master), **Service File** (mezzanine baseline read by SIPI server; was: Service master), **Access File** (end-user delivery; previously implicit). Each stage has its own format set; the stage discriminator is the *Essentials packet*'s presence (Service File) or the future preservation-metadata schema (Preservation File), not the file format. A file's stage is the outcome of the operator's intentional CLI invocation (per ADR-0010), never inferred. The "master" terminology is retired with this rename.
- **"preservation metadata channel"** was ambiguous about whether rights / provenance / PREMIS-shaped data belongs in the *Essentials packet* (SIPI-internal) or in *Embedded metadata* (standards-based XMP/IPTC/EXIF). **Resolved** (per ADR-0011): preservation metadata propagates via the *Embedded metadata* channel — specifically XMP, using established schemas (XMP-Rights, XMP-PLUS, XMP-PROV / C2PA, PREMIS-XMP). The *Essentials packet* stays scoped to SIPI-internal concerns (technical identity, image shape, future file-structure offsets) and does not grow to carry preservation-metadata fields.
- **"route handler"** was overloaded between Lua scripts bound to URL patterns and `shttps::RequestHandler` C++ callbacks. Resolved: **Route handler** is the umbrella; **Lua route handler** and **C++ route handler** are the sub-types.
- **Knora** terms still ship in CLI flags, config, and the `knora.json` endpoint. Resolved: kept under the *Deprecated / legacy* section. Do not coin new uses.
