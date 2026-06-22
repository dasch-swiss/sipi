/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The narrow C FFI seam between the Rust HTTP shell and the C++ image engine
 * (strangler-fig Phase B; ADR-0013).
 *
 * This header is the **durable interface** of the rewrite: the Rust shell
 * (Phase C) drives the C++ engine through exactly these functions, and the
 * engine reaches the response only through the `SipiResponse` callbacks — never
 * a `shttps` type. It is plain C (no C++ in the surface) so it is consumable
 * both by the C++ implementation in `sipi_ffi.cpp` and, in Phase C, by Rust via
 * bindgen.
 *
 * **Streamed, not a result struct.** A real SIPI response carries a dynamic
 * status, a variable header set (Content-Range/206, Content-Disposition,
 * Retry-After, Cache-Control, Last-Modified, Link, repeated Set-Cookie) and a
 * streamed body. A fixed result struct cannot express that, so the response is
 * emitted through the `SipiResponse` callbacks: set status, add each header,
 * write each body chunk, poll for client cancellation.
 *
 * **No C++ exception crosses this boundary.** Every `sipi_*` entry wraps its
 * body in a catch-all and returns a status code; a throw unwinding through
 * `extern "C"` into Rust is UB (FFI contracts, Phase B).
 *
 * The full entry-point set is declared here as one locked contract; the
 * definitions in `sipi_ffi.cpp` grow as the engine is carved behind the seam.
 */
#ifndef SIPI_FFI_SIPI_FFI_H
#define SIPI_FFI_SIPI_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define SIPI_FFI_NODISCARD [[nodiscard]]
extern "C" {
#else
#define SIPI_FFI_NODISCARD
#endif

/* ── Response sink callbacks (Rust-owned in Phase C) ────────────────────────
 * The engine emits the whole response through these. `ctx` is the opaque
 * Rust-owned (or, in the Phase B parity path, C++-Connection-owned) cookie. */

/*! Body bytes, forward-only, for an **unknown-length** body (e.g. the image
 *  encoder, whose output size isn't known until encoding finishes — the
 *  transport frames it chunked over HTTP/1.1). Returns 0 on success, non-zero
 *  on a write failure (peer gone, socket error) so the engine aborts without
 *  throwing across C frames. Structurally identical to `Sipi::SipiWriteFn` in
 *  `formats/output_sink.h` — the two are kept in lock-step by design across the
 *  formats/ffi layer boundary; each header stays self-contained. */
typedef int (*SipiWriteFn)(void *ctx, const uint8_t *data, size_t len);

/*! Deliver a **known-length** file region `[offset, offset+length)` to the
 *  body. The size is known, so the transport frames it with Content-Length
 *  (and may use zero-copy `sendfile(2)`) — the right shape for raw file
 *  downloads (images via `/file`, and any media). Status + headers are set
 *  first via set_status/add_header. Returns 0 on success, non-zero on a write
 *  failure. Framing (Content-Length vs HTTP/2 DATA frames) is the transport's
 *  call, per the negotiated protocol version — never hard-coded here. */
typedef int (*SipiSendFileFn)(void *ctx, const char *path, uint64_t offset, uint64_t length);

/*! One call per response header line (Set-Cookie may repeat). */
typedef void (*SipiHeaderFn)(void *ctx, const char *name, const char *value);

/*! HTTP status code for the response. */
typedef void (*SipiStatusFn)(void *ctx, int status);

/*! Polled between pipeline stages; 1 = client gone / timed out → abort and
 *  unlink any partial cache file (the Rust equivalent of `peerConnected()`). */
typedef int (*SipiCancelledFn)(void *ctx);

/*! The response sink the engine drives. A body is delivered either as a
 *  known-length file region (`send_file`, Content-Length framing) or as an
 *  unknown-length byte stream (`write`, chunked framing) — never both. */
typedef struct
{
  void *ctx;
  SipiStatusFn set_status;
  SipiHeaderFn add_header;
  SipiWriteFn write;
  SipiSendFileFn send_file;
  SipiCancelledFn cancelled;
} SipiResponse;

/* ── IIIF serve request (consumed by sipi_serve_image) ──────────────────────
 * The IIIF URL components, typed as the `iiifparser` state machine produces
 * them. Region/size/rotation/quality/format are flattened so the seam carries
 * no `iiifparser` class layout (those types move to Rust in D+ without changing
 * this ABI). Enum values mirror the C++ `SipiRegion::CoordType`,
 * `SipiSize::SizeType`, `SipiQualityFormat::QualityType`/`FormatType`. */

typedef enum {
  SIPI_REGION_FULL = 0,
  SIPI_REGION_SQUARE = 1,
  SIPI_REGION_COORDS = 2,
  SIPI_REGION_PERCENTS = 3
} SipiRegionType;

typedef enum {
  SIPI_SIZE_UNDEFINED = 0,
  SIPI_SIZE_FULL = 1,
  SIPI_SIZE_PIXELS_XY = 2,
  SIPI_SIZE_PIXELS_X = 3,
  SIPI_SIZE_PIXELS_Y = 4,
  SIPI_SIZE_MAXDIM = 5,
  SIPI_SIZE_PERCENTS = 6,
  SIPI_SIZE_REDUCE = 7
} SipiSizeType;

typedef enum {
  SIPI_QUALITY_DEFAULT = 0,
  SIPI_QUALITY_COLOR = 1,
  SIPI_QUALITY_GRAY = 2,
  SIPI_QUALITY_BITONAL = 3
} SipiQualityType;

typedef enum {
  SIPI_FORMAT_UNSUPPORTED = 0,
  SIPI_FORMAT_JPG = 1,
  SIPI_FORMAT_TIF = 2,
  SIPI_FORMAT_PNG = 3,
  SIPI_FORMAT_GIF = 4,
  SIPI_FORMAT_JP2 = 5,
  SIPI_FORMAT_PDF = 6,
  SIPI_FORMAT_WEBP = 7
} SipiFormatType;

typedef struct
{
  SipiRegionType region_type;
  float region[4]; /* x,y,w,h (coords or percents); unused for FULL/SQUARE */

  SipiSizeType size_type;
  int size_upscaling; /* `^` prefix */
  float size_percent; /* PERCENTS */
  int size_reduce; /* REDUCE */
  size_t size_nx; /* requested width  (PIXELS or MAXDIM modes) */
  size_t size_ny; /* requested height (PIXELS or MAXDIM modes) */

  float rotation; /* degrees */
  int rotation_mirror;

  SipiQualityType quality_type;
  SipiFormatType format_type;
} SipiIiifParams;

typedef struct
{
  const char *resolved_path; /* image-root-validated absolute path (validation owned by the Rust edge) */
  const char *prefix; /* IIIF prefix — canonical-URL + cache key */
  const char *identifier; /* IIIF identifier — canonical-URL `id` + cache key */
  const char *client_ip; /* rate-limit key — XFF-resolved at the Rust edge */
  SipiIiifParams params; /* region/size/rotation/quality/format */
  const char *restricted_size; /* preflight `restrict` downscale, or NULL */
  const char *watermark_path; /* preflight `restrict` watermark, or NULL */
  const char *forwarded_proto; /* X-Forwarded-Proto → canonical-URL / redirect scheme */
  const char *forwarded_host; /* X-Forwarded-Host  → canonical-URL `id` (host for the canonical URL) */
  const char *request_uri; /* raw request URI — error/log context only (Sentry), or NULL */
  int is_head; /* 1 = HEAD: emit status + headers, no body, no cache write */
} SipiServeRequest;

/* ── Preflight (C++ LuaServer pre_flight / file_pre_flight) ──────────────────
 * A fixed permission TYPE plus an open key/value channel (infile, watermark,
 * size, cookie_url, token_url, logout_url, service pass-through) — the real
 * preflight returns an unordered_map, not a flat struct. Two hooks, same
 * shape: the IIIF image preflight (prefix + identifier) and the `/file`
 * media-serving preflight (a resolved filepath; audio / video / PDF / any
 * non-IIIF file). */
typedef enum {
  SIPI_ALLOW = 0,
  SIPI_LOGIN = 1,
  SIPI_CLICKTHROUGH = 2,
  SIPI_KIOSK = 3,
  SIPI_EXTERNAL = 4,
  SIPI_RESTRICT = 5,
  SIPI_DENY = 6
} SipiPermType;

typedef void (*SipiKVFn)(void *ctx, const char *key, const char *value);

/*! A name/value pair, for passing request headers + cookies to the request-context
 *  builder. Both pointers are caller-owned; the builder deep-copies them. */
typedef struct
{
  const char *name;
  const char *value;
} SipiStrPair;

/* ── Opaque handles ─────────────────────────────────────────────────────────
 * CLI/env overrides for `sipi_init` and the engine-counter snapshot for
 * `sipi_metrics_snapshot`. Incomplete here on purpose: the implementing
 * translation unit owns the layout, so the seam commits no field set it does
 * not need. */
typedef struct SipiServerConfig SipiServerConfig;
typedef struct SipiMetricsSnapshot SipiMetricsSnapshot;

/* The whole HTTP request the Lua subsystem reads (method/uri/host/secure +
 * headers, cookies, GET/POST params, uploads, body). Opaque because the Lua
 * runtime stays C++ behind the seam (it wraps the C++ `shttps::RequestContext`);
 * the Lua hooks can read ANY request field via `server.*`, so preflight and
 * configured routes carry the full request, not the narrow IIIF `SipiServeRequest`.
 * Built by the caller — the transport in the Phase B-L parity path, the Rust
 * shell (via a builder) in Phase C. */
typedef struct SipiRequestContext SipiRequestContext;

/* ── Edge-probe types (Rust-edge path validation + info.json/knora.json) ─────
 * Read-only helpers the Rust shell needs to build a request the way the C++
 * server did and to assemble the JSON responses the seam has no serve entry for. */

/*! Native image shape from a header read (NOT a full decode). `numpages` is 0
 *  for a single-page image; `tile_width`/`tile_height` are 0 when the image is
 *  untiled; `clevels` is the JP2/pyramidal resolution-level count (0 when none).
 *  Carries the tiling + level fields so the Rust shell assembles info.json's
 *  `sizes[]` / `tiles[]` from one probe rather than a second call. */
typedef struct
{
  uint32_t width;
  uint32_t height;
  uint32_t numpages;
  uint32_t tile_width;
  uint32_t tile_height;
  uint32_t clevels;
} SipiImageDims;

/*! Emits a single string value through a caller callback, so the seam returns no
 *  owned C string (no malloc/free contract across the boundary). */
typedef void (*SipiStrFn)(void *ctx, const char *value);

/* ── Entry points ───────────────────────────────────────────────────────────
 * All return 0 on success / an error code on failure; none let a C++ exception
 * cross the boundary. */

/*! IIIF decode→transform→encode→stream; honours the restrict size/watermark. */
SIPI_FFI_NODISCARD int sipi_serve_image(const SipiServeRequest *req, const SipiResponse *resp);

/*! Raw `/file` passthrough incl. HTTP Range / 206 — no decode. Owns the serve
 *  *policy* (stat, MIME → Content-Type, Range parse → status + Content-Range)
 *  and delegates the byte delivery to `resp->send_file`, so the transport
 *  streams the file with Content-Length framing (and zero-copy where it can).
 *  `resolved_path` is an already-validated absolute path; `range` is the raw
 *  `Range` header value or NULL. Returns 0 when the response was emitted, or an
 *  HTTP status code (e.g. 404, 400, 500) when it fails before any byte is sent,
 *  so the caller can render its own error response. */
SIPI_FFI_NODISCARD int sipi_serve_file(const char *resolved_path, const char *range, const SipiResponse *resp);

/*! C++ LuaServer `pre_flight()`: returns a permission type + key/value channel.
 *  The IIIF image preflight (serve_iiif / info.json). The hook reads the request
 *  through `ctx` (`server.header` / `server.cookies` / …) and gets prefix +
 *  identifier + the cookie header as its Lua arguments. Valid permission types:
 *  allow / login / clickthrough / kiosk / external / restrict / deny. */
SIPI_FFI_NODISCARD int sipi_preflight(const char *prefix,
  const char *identifier,
  SipiRequestContext *ctx,
  SipiPermType *type,
  SipiKVFn emit_kv,
  void *kv_ctx);

/*! C++ LuaServer `file_pre_flight()`: the `/file` media-serving path (audio /
 *  video / PDF / any non-IIIF file). Same shape as sipi_preflight but takes a
 *  resolved filepath; narrower valid permission set: allow / login / restrict /
 *  deny. */
SIPI_FFI_NODISCARD int sipi_file_preflight(const char *filepath,
  SipiRequestContext *ctx,
  SipiPermType *type,
  SipiKVFn emit_kv,
  void *kv_ctx);

/*! Build the opaque request context the preflight hooks read (`server.*`) from
 *  primitive request fields — the Phase C replacement for the transport's
 *  `make_request_context(Connection&)`. Header names are lowercased to match the
 *  transport. The JWT secret + the response sink are NOT taken here: the secret
 *  is injected from the engine Lua config by `make_lua_server`, and preflight is
 *  read-only (no response sink). Deep-copies `headers`/`cookies`, so the caller's
 *  arrays need not outlive the call. Returns the context (caller frees it with
 *  `sipi_free_request_context`) or NULL on allocation failure. */
SIPI_FFI_NODISCARD SipiRequestContext *sipi_make_request_context(const char *method,
  const char *client_ip,
  int client_port,
  int secure,
  const char *host,
  const char *uri,
  const SipiStrPair *headers,
  size_t n_headers,
  const SipiStrPair *cookies,
  size_t n_cookies);

/*! Free a context returned by `sipi_make_request_context`. NULL is a no-op. */
void sipi_free_request_context(SipiRequestContext *ctx);

/*! Whether the engine Lua config defines a `pre_flight` / `file_pre_flight` hook
 *  (`luaFunctionExists`). The Rust shell reads these once at startup to mirror the
 *  C++ `luaFunctionExists` gate: with no hook it falls back to a default path +
 *  allow. Builds a VM, so call once and cache. Returns 0, or 500 if uninitialised. */
SIPI_FFI_NODISCARD int sipi_has_preflight(int *out);
SIPI_FFI_NODISCARD int sipi_has_file_preflight(int *out);

/*! Run a configured Lua route (the C++ `script_handler` analogue): execute the
 *  route's script in the engine-config VM and emit its response (`server.print` /
 *  `sendStatus` / `sendHeader` / `sendCookie`) through `resp`. Takes the FULL
 *  request as the opaque `SipiRequestContext` — a route reads arbitrary request
 *  data via `server.*`, so it carries the whole request, not the narrow IIIF
 *  `SipiServeRequest`. Defined at the cutover, when the Rust shell owns route
 *  dispatch and the transport's `script_handler` is deleted; the upload routes
 *  additionally depend on multipart `uploads` reaching the context. */
SIPI_FFI_NODISCARD int sipi_run_lua_route(const char *script, SipiRequestContext *ctx, const SipiResponse *resp);

/*! Engine counters → Rust OTel meter (NOT Prometheus). */
SIPI_FFI_NODISCARD int sipi_metrics_snapshot(SipiMetricsSnapshot *out);

/*! C++ parses the Lua config (full `config.*` surface incl. routes + preflight
 *  hook); `overrides` carries CLI/env overrides only. */
SIPI_FFI_NODISCARD int sipi_init(const char *lua_config_path, const SipiServerConfig *overrides);

/* ── Edge probes ─────────────────────────────────────────────────────────────
 * Read-only helpers the Rust shell calls at the request edge. Like
 * sipi_metrics_snapshot they drive no response sink, so they are sipi_guard-only
 * (no build/apply split). All require `sipi_init` to have installed the engine. */

/*! The configured image root, for the Rust edge to build + containment-check a
 *  `resolved_path`. `resolved` = 0 → the raw config value (path build, parity
 *  with the C++ `imgroot()`); `resolved` = 1 → the realpath()-resolved root (the
 *  R2 containment check). `*out` points at process-static memory owned by the
 *  installed engine context — valid for the process lifetime after `sipi_init`,
 *  never freed by the caller. Returns 0, or 500 if `sipi_init` has not run. */
SIPI_FFI_NODISCARD int sipi_imgroot(int resolved, const char **out);

/*! The `prefix_as_path` config knob: `*out` = 1 → the IIIF prefix is a path
 *  component under imgroot (`imgroot/prefix/identifier`); 0 → `imgroot/identifier`.
 *  Returns 0, or 500 if `sipi_init` has not run. */
SIPI_FFI_NODISCARD int sipi_prefix_as_path(int *out);

/*! The configured worker-thread count (the Lua config `nthreads`). `*out` = 0
 *  means the operator left it auto — the Rust shell then sizes its blocking pool
 *  from the host parallelism. Returns 0, or 500 if `sipi_init` has not run. */
SIPI_FFI_NODISCARD int sipi_nthreads(int *out);

/*! Header-only image-shape probe (`SipiImage::read_shape` — no full decode).
 *  `resolved_path` is an already-validated absolute path (the Rust edge owns
 *  existence + containment). Fills `*out` on success. Returns 0, or 500 if the
 *  shape cannot be read (the edge has already confirmed the file exists, so an
 *  unreadable image here is an engine-level failure). */
SIPI_FFI_NODISCARD int sipi_image_dims(const char *resolved_path, SipiImageDims *out);

/*! The engine's libmagic MIME type for a file (the same `getBestFileMimetype`
 *  the `/file` and info.json paths use — one source of truth for MIME mapping),
 *  emitted once via `emit`. `resolved_path` is an already-validated absolute
 *  path. Returns 0 (and calls `emit` once) on success, or 500 on error. */
SIPI_FFI_NODISCARD int sipi_mimetype(const char *resolved_path, SipiStrFn emit, void *ctx);

/*! Hands argv verbatim to the existing C++ CLI11 parser; returns the process
 *  exit code (no `exit()`/`abort()` from inside the FFI). */
SIPI_FFI_NODISCARD int sipi_cli_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* SIPI_FFI_SIPI_FFI_H */
