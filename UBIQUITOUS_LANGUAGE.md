# Ubiquitous Language

Canonical terminology for the SIPI repository. SIPI is a IIIF Image API 3.0 server first; the rest of the surface (file streaming, embedded webserver, Lua extensibility) is layered on top.

## Resources and identification

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Image** | A pixel-bearing artefact processed through the IIIF pipeline (region, size, rotation, quality, format). | resource, media, asset |
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

## Format handling

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Format handler** | A SipiIO subclass that adapts a codec to SIPI's read/write contract (SipiIOJ2k, SipiIOTiff, SipiIOPng, SipiIOJpeg). | IO backend, format driver |
| **Codec** | A third-party library that performs the actual encode/decode (kakadu, libtiff, libpng, libjpeg, libwebp). A format handler *uses* a codec. | library, backend |
| **ICC normalization** | The byte-level rewrite of bytes 24-35 (creation date) and 84-99 (Profile ID) inside `SipiIcc::iccBytes()`, gated by the *Reproducibility flag*. Test-only — production iccBytes() is the identity. | ICC scrubbing, ICC stripping (those imply removing profiles, not normalizing them) |
| **Reproducibility flag** | The `SOURCE_DATE_EPOCH` environment variable. When set, every ICC profile emitted by SipiIcc::iccBytes() has its creation date overwritten with the supplied epoch and its Profile ID zeroed; codec-bound emissions become byte-deterministic. CMake injects it for `sipi.approvaltests` only. | deterministic mode, test-only mode (the env var is the contract; "modes" obscure that) |

## Metadata and preservation

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Preservation metadata** | Umbrella term for all metadata SIPI manages across format conversions for long-term preservation. Comprises *embedded metadata* and the *Essentials packet*. | sidecar metadata, image metadata |
| **Embedded metadata** | Third-party metadata standards SIPI carries through unchanged where possible: EXIF, IPTC, XMP, and ICC color profiles. | header metadata |
| **Essentials packet** | The SIPI-specific record embedded in image headers: original filename, original mimetype, hash type, pixel checksum, and (optionally) an ICC profile when the destination format cannot embed one natively. C++ class: `SipiEssentials`. | preservation packet, sipi metadata |
| **Pixel checksum** | A checksum (e.g. SHA-256) over the *uncompressed* pixel values, stored in the Essentials packet to verify that a format conversion did not alter image content. | data checksum, content hash |

## Endpoints and documents

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Image Information document** | The IIIF-spec JSON returned at `{prefix}/{identifier}/info.json` for an Image. Advertises supported region/size/rotation/quality/format forms via `extraFeatures` and `extraFormats`. | info.json (the file name), info doc |
| **Bitstream Information document** | The SIPI-specific JSON returned at `{prefix}/{identifier}/info.json` for a Bitstream: `@context`, `id`, `internalMimeType`, `fileSize`. Same URL shape as the Image Information document, distinct schema (`http://sipi.io/api/file/3/context.json`). | file info, bitstream info |
| **`/file` endpoint** | The URL form `{prefix}/{identifier}/file` that streams the underlying Bitstream as-is, bypassing IIIF processing. | file pass-through, raw endpoint |

## Lua extensibility

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Init script** | A Lua script (`sipi.init.lua`) executed once at server startup. Sets up global state shared across requests. | startup script, bootstrap |
| **Preflight script** | A Lua script invoked per request before serving. Returns a Permission and resolves the on-disk path of the resource. Implemented by the Lua function `pre_flight` (Image / IIIF) or `file_pre_flight` (Bitstream). | pre-flight script, authorization hook |
| **Route handler** | A Lua script bound to a URL pattern, used to implement custom RESTful endpoints (e.g. upload, admin) on top of the embedded webserver. | route, custom endpoint |
| **Permission** | The verdict and shaping output returned by a preflight script. Carries a *permission type* and optional sub-fields: `infile` (resolved on-disk path), `watermark` (overlay PNG path), and access-restricting size caps. | access policy, ACL result |

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

## Operational concerns

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Backpressure** | Umbrella term for SIPI's admission-control mechanisms that protect the server under load. Comprises the *decode memory budget* and the *rate limiter*. | flow control, throttling |
| **Decode memory budget** | A process-wide, lock-free accounting of memory currently committed to in-flight image decodes, with an RAII guard. Rejects requests that would push concurrent decode memory over a configured ceiling. | memory budget, decode budget |
| **Rate limiter** | The per-client request-rate ceiling enforced before decode admission. | throttle |
| **Cache** | A file-based LRU of generated representations, keyed by *cache key*, with dual-limit eviction (total size **and** file count) and crash recovery. | response cache, output cache |
| **Client abort** | An HTTP response write that fails because the peer is gone (FIN, RST, or write timeout). Surfaces in code as `Sipi::SipiImageClientAbortError`, raised when `shttps::OUTPUT_WRITE_FAIL` is thrown from a socket write. Logged at info, **not** captured to Sentry — these are peer-side events, not server faults. | broken pipe error, peer disconnect error |

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
| **knora.json document** | Deprecated | Legacy DSP-specific information document at `{prefix}/{identifier}/knora.json`. New consumers should use the Image Information document. |
| **`--knorapath` / `--knoraport`** | Deprecated | CLI flags / config keys (`knora_path`, `knora_port`) retained for compatibility. |

## Relationships

- An **Image** is served through the IIIF pipeline; a **Bitstream** is served via the `/file` endpoint.
- An **Identifier** plus a **Prefix** locates exactly one Image *or* Bitstream under the **Image root**.
- A request resolves to a **Permission** via the **Preflight script** before any decode happens.
- A **Format handler** uses exactly one **Codec** to read or write its format.
- **Preservation metadata** = **Embedded metadata** ∪ **Essentials packet**; both travel with an Image across format conversions.
- The **Cache key** extends the **Canonical URL** with a watermark bit; the **Cache** is keyed by it.
- **Backpressure** = **Decode memory budget** + **Rate limiter**; both gate admission to decode.

## Example dialogue

> **Dev:** A request comes in for `/iiif/abc123/full/!500,500/0/default.jpg` — what's the first thing that runs?

> **Maintainer:** The **preflight script**. It receives the **prefix** (`iiif`), the **identifier** (`abc123`), and the request headers, and returns a **permission**. If the type is `restrict`, the permission carries a size cap and possibly a **watermark** path, both of which shape what we eventually serve.

> **Dev:** And the resolved on-disk file?

> **Maintainer:** Same permission — the `infile` field. The preflight script may rewrite it; we then validate the resolved path stays inside the **image root** before any I/O.

> **Dev:** What about `/iiif/abc123/file`?

> **Maintainer:** That's the **bitstream** surface. We treat `abc123` as a **bitstream**, not an **image**: no IIIF pipeline, no **decode level**, no cache. The `file_pre_flight` script runs instead of `pre_flight`, and `info.json` for that resource returns a **bitstream information document**, not an **image information document**.

> **Dev:** When does the **decode memory budget** come in?

> **Maintainer:** Just before we instantiate the **format handler** to decode. If admitting the decode would push us over the budget, we reject with backpressure — the request never touches the **codec**.

> **Dev:** And the **essentials packet** — that's only on write?

> **Maintainer:** Mostly, yes. We embed it when SIPI produces a converted file, so the **pixel checksum** and original filename travel with the new artefact. On read, we extract any existing essentials packet — that's how we recover an ICC profile when a JPEG2000 source couldn't natively embed one.

## Flagged ambiguities

- **"canonical URL"** was used in code for two different things: the IIIF-spec form and the SIPI cache-keying string with a watermark suffix. Resolved: **Canonical URL** = IIIF spec form only; **Cache key** = the SIPI extension. Code variables like `cannonical_watermark` (sic) are implementation; not part of the language.
- **"info.json"** is a single URL shape with two distinct response schemas depending on whether the resource is an Image or a Bitstream. Resolved by introducing two domain terms: **Image Information document** vs **Bitstream Information document**. Refer to them by their domain term, not the file name.
- **"preservation metadata"** was sometimes used narrowly (the Essentials packet) and sometimes broadly (everything embedded). Resolved: **Preservation metadata** is the umbrella; **Essentials packet** is the SIPI-specific subset; **Embedded metadata** is the third-party-standards subset.
- **"file"** is overloaded between the URL endpoint (`/file`), the on-disk artefact (`infile`, `imgroot`), and the served byte stream. Resolved: as a domain noun, **Bitstream** names the served byte stream; *file* survives only in URL paths and filesystem-level discussion.
- **"reduce"** appears as both a JPEG2000 codestream parameter and a TIFF resolution-level concept. Resolved: **Decode level** is the canonical domain term; *reduce* survives only as a codec-API parameter name.
- **Knora** terms still ship in CLI flags, config, and the `knora.json` endpoint. Resolved: kept under the *Deprecated / legacy* section. Do not coin new uses.
