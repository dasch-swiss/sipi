//! Rust bindings for the narrow C FFI seam (`src/ffi/sipi_ffi.h`).
//!
//! Hand-written rather than bindgen-generated: the seam is a small, locked
//! `extern "C"` contract (strangler-fig rewrite; ADR-0013), so mirroring it by
//! hand keeps the Bazel graph free of a bindgen build step and keeps the Rust
//! types readable. Each declaration here tracks `sipi_ffi.h` 1:1 — when the
//! header changes, this file changes in lock-step.
//!
//! Safety contract (from the header): no C++ exception crosses the boundary
//! (every entry is `sipi_guard`-wrapped → status code); all `*const c_char` /
//! struct inputs are caller-owned and must outlive the (blocking) call; the
//! response is emitted only through the [`SipiResponse`] callbacks.

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};

use crate::config::{OverridesHolder, ServerOverrides, SipiServerConfig};

// ── Response sink callbacks (Rust-owned) ────────────────────────────────────
// `Option<extern "C" fn ...>` is the FFI-safe nullable function pointer: `None`
// is a null pointer on the C side, matching a sink that does not implement a
// given delivery mode.

/// Body bytes, forward-only, unknown length (image encoder → chunked framing).
/// Returns 0 on success, non-zero on a write failure.
pub type SipiWriteFn = extern "C" fn(ctx: *mut c_void, data: *const u8, len: usize) -> c_int;

/// Known-length file region `[offset, offset+length)` (→ Content-Length framing,
/// zero-copy where possible). Returns 0 on success, non-zero on a write failure.
pub type SipiSendFileFn =
    extern "C" fn(ctx: *mut c_void, path: *const c_char, offset: u64, length: u64) -> c_int;

/// One call per response header line (`Set-Cookie` may repeat).
pub type SipiHeaderFn = extern "C" fn(ctx: *mut c_void, name: *const c_char, value: *const c_char);

/// HTTP status code for the response.
pub type SipiStatusFn = extern "C" fn(ctx: *mut c_void, status: c_int);

/// Polled between pipeline stages; 1 = client gone / timed out → abort.
pub type SipiCancelledFn = extern "C" fn(ctx: *mut c_void) -> c_int;

/// The response sink the engine drives. A body is delivered either as a
/// known-length file region (`send_file`) or an unknown-length byte stream
/// (`write`) — never both.
#[repr(C)]
pub struct SipiResponse {
    pub ctx: *mut c_void,
    pub set_status: Option<SipiStatusFn>,
    pub add_header: Option<SipiHeaderFn>,
    pub write: Option<SipiWriteFn>,
    pub send_file: Option<SipiSendFileFn>,
    pub cancelled: Option<SipiCancelledFn>,
}

// ── IIIF serve request (consumed by sipi_serve_image) ───────────────────────
// repr(C) mirrors of the seam structs/enums in `sipi_ffi.h`. The integer enum
// values match the C++ `iiifparser` enums 1:1 (verified against the header), so
// the Rust IIIF parser produces them directly.

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SipiRegionType {
    Full = 0,
    Square = 1,
    Coords = 2,
    Percents = 3,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SipiSizeType {
    Undefined = 0,
    Full = 1,
    PixelsXy = 2,
    PixelsX = 3,
    PixelsY = 4,
    Maxdim = 5,
    Percents = 6,
    Reduce = 7,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SipiQualityType {
    Default = 0,
    Color = 1,
    Gray = 2,
    Bitonal = 3,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SipiFormatType {
    Unsupported = 0,
    Jpg = 1,
    Tif = 2,
    Png = 3,
    Gif = 4,
    Jp2 = 5,
    Pdf = 6,
    Webp = 7,
}

/// Flattened IIIF region/size/rotation/quality.format — mirrors `SipiIiifParams`
/// in `sipi_ffi.h`. `c_int` ⇔ C `int`, `usize` ⇔ C `size_t`, `f32` ⇔ C `float`.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SipiIiifParams {
    pub region_type: SipiRegionType,
    pub region: [f32; 4],
    pub size_type: SipiSizeType,
    pub size_upscaling: c_int,
    pub size_percent: f32,
    pub size_reduce: c_int,
    pub size_nx: usize,
    pub size_ny: usize,
    pub rotation: f32,
    pub rotation_mirror: c_int,
    pub quality_type: SipiQualityType,
    pub format_type: SipiFormatType,
}

/// The IIIF serve request — mirrors `SipiServeRequest` in `sipi_ffi.h`. All
/// `*const c_char` fields are caller-owned and must outlive the (synchronous)
/// `sipi_serve_image` call; null is allowed where the header documents it.
#[repr(C)]
pub struct SipiServeRequest {
    pub resolved_path: *const c_char,
    pub prefix: *const c_char,
    pub identifier: *const c_char,
    pub client_ip: *const c_char,
    pub params: SipiIiifParams,
    pub restricted_size: *const c_char,
    pub watermark_path: *const c_char,
    pub forwarded_proto: *const c_char,
    pub forwarded_host: *const c_char,
    pub request_uri: *const c_char,
    pub is_head: c_int,
}

/// Native image shape from a header read — mirrors `SipiImageDims` in
/// `sipi_ffi.h`. `numpages` is 0 for a single-page image; `tile_width`/
/// `tile_height` are 0 when untiled; `clevels` is the JP2/pyramidal level count.
/// Enough to assemble info.json's `sizes[]` / `tiles[]` from one probe.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SipiImageDims {
    pub width: u32,
    pub height: u32,
    pub numpages: u32,
    pub tile_width: u32,
    pub tile_height: u32,
    pub clevels: u32,
}

/// Emits a single string value (mirrors `SipiStrFn`) — the seam returns no owned
/// C string, so `sipi_mimetype` hands its result back through this callback.
pub type SipiStrFn = extern "C" fn(ctx: *mut c_void, value: *const c_char);

/// Emits one configured Lua route — method/route/script — (mirrors `SipiRouteFn`).
pub type SipiRouteFn = extern "C" fn(
    ctx: *mut c_void,
    method: *const c_char,
    route: *const c_char,
    script: *const c_char,
);

/// Emits an image's Essentials-packet identity — original mimetype + original
/// filename — together (mirrors `SipiEssentialsFn`). Called at most once: both
/// strings are known together or not at all.
pub type SipiEssentialsFn =
    extern "C" fn(ctx: *mut c_void, orig_mimetype: *const c_char, orig_filename: *const c_char);

// ── Preflight (auth) ────────────────────────────────────────────────────────

/// Permission type a Lua preflight hook returns (mirrors `SipiPermType`). The
/// discriminants match the C enum 1:1.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SipiPermType {
    Allow = 0,
    Login = 1,
    Clickthrough = 2,
    Kiosk = 3,
    External = 4,
    Restrict = 5,
    Deny = 6,
}

/// A name/value pair passed to the request-context builder (mirrors `SipiStrPair`).
#[repr(C)]
pub struct SipiStrPair {
    pub name: *const c_char,
    pub value: *const c_char,
}

/// The preflight key/value emit callback (mirrors `SipiKVFn`).
pub type SipiKVFn = extern "C" fn(ctx: *mut c_void, key: *const c_char, value: *const c_char);

/// The opaque request context the preflight hooks read (`= shttps::RequestContext`).
/// Built by [`sipi_make_request_context`], freed by [`sipi_free_request_context`].
#[repr(C)]
pub struct SipiRequestContext {
    _private: [u8; 0],
}

extern "C" {
    /// IIIF decode→transform→encode→stream; honours the restrict size/watermark.
    /// Returns 0 when the response was emitted via the sink, or an HTTP status
    /// code (400/404/500/…) on a pre-commit failure (499 = client gone → no
    /// response). The engine reads `engine_context()`, so `sipi_init` must have
    /// run first.
    pub fn sipi_serve_image(req: *const SipiServeRequest, resp: *const SipiResponse) -> c_int;

    /// Raw `/file` passthrough incl. HTTP Range / 206 — no decode.
    /// `resolved_path` is an already-validated absolute path; `range` is the raw
    /// `Range` header value or null. Returns 0 when the response was emitted, or
    /// an HTTP status code (404/400/500) on a pre-commit failure.
    pub fn sipi_serve_file(
        resolved_path: *const c_char,
        range: *const c_char,
        resp: *const SipiResponse,
    ) -> c_int;

    /// Parse the Lua config and install the engine + Lua config from scratch.
    /// Must run once before any serve call (`engine_context()` hard-fails
    /// otherwise). `overrides` is the `SipiServerConfig*` CLI/env override
    /// channel the engine layers over the Lua config (null = no overrides).
    /// Returns 0 on success, non-zero on failure.
    ///
    /// `pub(crate)` (unlike the sibling bindings): it names the crate-private
    /// `SipiServerConfig`, so wider visibility would trip `private_interfaces`.
    pub(crate) fn sipi_init(
        lua_config_path: *const c_char,
        overrides: *const SipiServerConfig,
    ) -> c_int;

    /// The configured image root (`resolved` = 0 → raw config value for the path
    /// build; 1 → realpath()-resolved root for the containment check). `*out` is
    /// set to process-static memory owned by the engine (never freed). Returns 0,
    /// or 500 if `sipi_init` has not run.
    pub fn sipi_imgroot(resolved: c_int, out: *mut *const c_char) -> c_int;

    /// The `/server` fileserver docroot (raw config value; the edge canonicalises
    /// per request). `*out` is empty when no fileserver is configured. `*out` is
    /// process-static engine memory. Returns 0, or 500 if `sipi_init` has not run.
    pub fn sipi_docroot(out: *mut *const c_char) -> c_int;

    /// The URL prefix the docroot fileserver is mounted at (e.g. "/server").
    /// `*out` is empty when no fileserver is configured. Returns 0, or 500 if
    /// `sipi_init` has not run.
    pub fn sipi_wwwroute(out: *mut *const c_char) -> c_int;

    /// The `prefix_as_path` config knob (`*out` = 1/0). Returns 0, or 500 if
    /// `sipi_init` has not run.
    pub fn sipi_prefix_as_path(out: *mut c_int) -> c_int;

    /// The configured worker-thread count (`*out`); 0 = auto. Returns 0, or 500
    /// if `sipi_init` has not run.
    pub fn sipi_nthreads(out: *mut c_int) -> c_int;

    /// The configured max POST body size in bytes (`*out`); 0 = unlimited.
    /// Returns 0, or 500 if `sipi_init` has not run.
    pub fn sipi_max_post_size(out: *mut usize) -> c_int;

    /// The configured HTTP listen port (the Lua config `sipi.port`); a
    /// fallback below `--serverport`/`SIPI_SERVERPORT`/`SIPI_RS_PORT` (plan 02
    /// §6 R3). Returns 0, or 500 if `sipi_init` has not run.
    pub fn sipi_port(out: *mut c_int) -> c_int;

    /// Enumerate the configured Lua routes (method/route/script) installed by
    /// `sipi_init`, one `emit` call per route. Returns 0, or 500 if `sipi_init`
    /// has not run.
    pub fn sipi_routes(emit: SipiRouteFn, ctx: *mut c_void) -> c_int;

    /// Run a configured Lua route's script against `ctx`, emitting its response
    /// through `resp` (the streaming sink). Returns 0 once emitted, or an HTTP
    /// status (404/500) on a pre-body failure.
    pub fn sipi_run_lua_route(
        script: *const c_char,
        ctx: *mut SipiRequestContext,
        resp: *const SipiResponse,
    ) -> c_int;

    /// Attach the POST body + content type to a context (deep-copied). `data` may
    /// be null with `len` 0.
    pub fn sipi_request_context_set_body(
        ctx: *mut SipiRequestContext,
        content_type: *const c_char,
        data: *const u8,
        len: usize,
    );

    /// Append one parsed multipart upload (`server.uploads`). `tmpname` is the
    /// on-disk path of the spooled part; it must exist for the route call.
    pub fn sipi_request_context_add_upload(
        ctx: *mut SipiRequestContext,
        fieldname: *const c_char,
        origname: *const c_char,
        tmpname: *const c_char,
        mimetype: *const c_char,
        filesize: u64,
    );

    /// Append a GET (`kind` = 0) or POST (`kind` = 1) form parameter; both also
    /// feed `server.request`.
    pub fn sipi_request_context_add_param(
        ctx: *mut SipiRequestContext,
        kind: c_int,
        name: *const c_char,
        value: *const c_char,
    );

    /// Set `server.docroot` for a docroot `.lua`/`.elua` script (injected into the
    /// VM by `run_lua_route`). NULL/empty = not injected (configured routes leave
    /// it unset, matching the C++ `script_handler`).
    pub fn sipi_request_context_set_docroot(ctx: *mut SipiRequestContext, docroot: *const c_char);

    /// Header-only image-shape probe (no full decode) — also optionally emits
    /// the Essentials identity from the SAME read via `emit`/`ctx` (`None` =
    /// caller doesn't want it, e.g. info.json). When `emit` is present, it
    /// fires exactly once, with both strings, iff the file carries a
    /// parseable Essentials packet; zero times otherwise (not an error).
    /// `resolved_path` is an already-validated absolute path. Returns 0 (and
    /// fills `*out`), or 500.
    pub fn sipi_image_dims(
        resolved_path: *const c_char,
        out: *mut SipiImageDims,
        emit: Option<SipiEssentialsFn>,
        ctx: *mut c_void,
    ) -> c_int;

    /// The engine's libmagic MIME type for a file, emitted once via `emit`.
    /// `resolved_path` is an already-validated absolute path. Returns 0, or 500.
    pub fn sipi_mimetype(resolved_path: *const c_char, emit: SipiStrFn, ctx: *mut c_void) -> c_int;

    /// Run the IIIF `pre_flight(prefix, identifier, cookie)` hook against `ctx`.
    /// Writes the permission to `*type` and emits each kv pair (incl. `infile`)
    /// via `emit_kv`. Returns 0, or 500 on a Lua/validation failure. `resp`, if
    /// non-null, is wired as the hook's response sink for the call — some
    /// `pre_flight` scripts emit a response directly (`server.sendStatus`/
    /// `sendHeader`/`server.print`) instead of, or alongside, returning a
    /// permission; without a sink that write null-derefs.
    pub fn sipi_preflight(
        prefix: *const c_char,
        identifier: *const c_char,
        ctx: *mut SipiRequestContext,
        ty: *mut SipiPermType,
        emit_kv: SipiKVFn,
        kv_ctx: *mut c_void,
        resp: *const SipiResponse,
    ) -> c_int;

    /// Run the `/file` `file_pre_flight(filepath, cookie)` hook (narrower
    /// permission set). Same out-channel contract as [`sipi_preflight`],
    /// including the `resp` sink.
    pub fn sipi_file_preflight(
        filepath: *const c_char,
        ctx: *mut SipiRequestContext,
        ty: *mut SipiPermType,
        emit_kv: SipiKVFn,
        kv_ctx: *mut c_void,
        resp: *const SipiResponse,
    ) -> c_int;

    /// Build the opaque request context from primitive fields (header names are
    /// lowercased). Deep-copies the arrays. Returns the context or null.
    pub fn sipi_make_request_context(
        method: *const c_char,
        client_ip: *const c_char,
        client_port: c_int,
        secure: c_int,
        host: *const c_char,
        uri: *const c_char,
        headers: *const SipiStrPair,
        n_headers: usize,
        cookies: *const SipiStrPair,
        n_cookies: usize,
    ) -> *mut SipiRequestContext;

    /// Free a context from [`sipi_make_request_context`]. Null is a no-op.
    pub fn sipi_free_request_context(ctx: *mut SipiRequestContext);

    /// Whether the engine Lua config defines a `pre_flight` / `file_pre_flight`
    /// hook. Builds a VM, so call once at startup. Returns 0 (and sets `*out`).
    pub fn sipi_has_preflight(out: *mut c_int) -> c_int;
    pub fn sipi_has_file_preflight(out: *mut c_int) -> c_int;

    /// Stamp the C++ engine's server-mode JSON logs on the calling thread with
    /// the active trace context (lowercase-hex `trace_id`/`span_id`); both NULL
    /// clears it. See `sipi_ffi.h`.
    pub fn sipi_set_log_trace_context(trace_id: *const c_char, span_id: *const c_char);
}

/// Stamps the C++ engine's logs (on the current thread) with a trace context for
/// the guard's lifetime, clearing on drop. Held across a blocking FFI serve call
/// so the engine's log lines carry the active `trace_id`/`span_id` and correlate
/// with the Rust trace; cleared on drop so a reused `spawn_blocking` thread never
/// leaks a stale id onto the next request.
pub struct LogTraceScope {
    active: bool,
}

impl LogTraceScope {
    /// Set the current thread's engine-log trace context (hex ids from the active
    /// OTel span). Returns a guard that clears it on drop.
    #[must_use]
    pub fn set(trace_id: &str, span_id: &str) -> Self {
        match (CString::new(trace_id), CString::new(span_id)) {
            (Ok(t), Ok(s)) => {
                // SAFETY: both are valid NUL-terminated strings for the call; the
                // engine copies them into a thread-local; the seam guards exceptions.
                unsafe { sipi_set_log_trace_context(t.as_ptr(), s.as_ptr()) };
                Self { active: true }
            }
            _ => Self { active: false },
        }
    }
}

impl Drop for LogTraceScope {
    fn drop(&mut self) {
        if self.active {
            // SAFETY: NULL clears the thread-local; the seam guards exceptions.
            unsafe { sipi_set_log_trace_context(std::ptr::null(), std::ptr::null()) };
        }
    }
}

/// Startup link self-check: forces the C++ engine `cc_library` to link into this
/// `rust_binary` and proves a round-trip across the seam. Calls `sipi_serve_file`
/// on a path that cannot exist, so the engine returns 404 (`access(2)` fails
/// before any I/O or sink callback) without side effects. Returns the status
/// code (expected 404).
pub fn link_self_check() -> i32 {
    let resp = SipiResponse {
        ctx: std::ptr::null_mut(),
        set_status: None,
        add_header: None,
        write: None,
        send_file: None,
        cancelled: None,
    };
    let bogus = c"/sipi-rust-shell-link-self-check/does-not-exist";
    // SAFETY: `bogus` is a valid NUL-terminated C string that outlives the call;
    // `range` is null (no Range header); `resp` outlives the synchronous call.
    // The seam guarantees no exception unwinds across the boundary.
    unsafe { sipi_serve_file(bogus.as_ptr(), std::ptr::null(), &resp) as i32 }
}

/// Parse the Lua config and install the engine + Lua config (`sipi_init`). Must
/// run once before serving images. `overrides` carries the parsed CLI/env flags
/// the engine layers over the loaded config. Returns the FFI status code on
/// failure (non-zero).
pub fn init(config_path: &str, overrides: &ServerOverrides) -> Result<(), i32> {
    let c_path = match CString::new(config_path) {
        Ok(p) => p,
        Err(_) => return Err(-1), // interior NUL in the path
    };
    let holder = match OverridesHolder::new(overrides) {
        Ok(h) => h,
        Err(_) => return Err(-1), // interior NUL in a config string value
    };
    // SAFETY: `c_path` and `holder` both outlive this synchronous call; the
    // engine deep-copies every present override during sipi_init, so none of the
    // holder's pointers escape; the seam guards exceptions. `holder` is a local
    // consumed inline, so it is not moved between `as_ptr()` and the call.
    let code = unsafe { sipi_init(c_path.as_ptr(), holder.as_ptr()) };
    if code == 0 {
        Ok(())
    } else {
        Err(code)
    }
}

/// The configured image root. `resolved = false` → the raw config value (used to
/// build the request path, parity with the C++ `imgroot()`); `resolved = true`
/// → the realpath()-resolved root (used for the R2 containment check). Returns
/// an owned copy — the underlying C string is process-static, but the edge holds
/// these in its own config so a copy keeps lifetimes simple. `Err` carries the
/// FFI status (500 if `sipi_init` has not run).
pub fn imgroot(resolved: bool) -> Result<String, i32> {
    let mut ptr: *const c_char = std::ptr::null();
    // SAFETY: `out` is a valid pointer; on success the engine writes a
    // process-static, NUL-terminated pointer; the seam guards exceptions.
    let code = unsafe { sipi_imgroot(resolved as c_int, &mut ptr) };
    if code != 0 {
        return Err(code);
    }
    if ptr.is_null() {
        return Err(-1);
    }
    // SAFETY: `ptr` is a NUL-terminated C string owned by the engine, valid for
    // the process lifetime; we copy it before returning.
    Ok(unsafe { CStr::from_ptr(ptr) }
        .to_string_lossy()
        .into_owned())
}

/// The `/server` fileserver docroot (raw config value). Empty when no fileserver
/// is configured; the shell then registers no static route. The edge canonicalises
/// it per request for the containment check. `Err` carries the FFI status (500 if
/// `sipi_init` has not run, -1 on a null pointer).
pub fn docroot() -> Result<String, i32> {
    cstr_getter(|out| {
        // SAFETY: `out` is the local out-pointer `cstr_getter` supplies; the seam guards exceptions.
        unsafe { sipi_docroot(out) }
    })
}

/// The URL prefix the docroot fileserver is mounted at (e.g. "/server"). Empty
/// when no fileserver is configured. `Err` carries the FFI status (500 if
/// `sipi_init` has not run, -1 on a null pointer).
pub fn wwwroute() -> Result<String, i32> {
    cstr_getter(|out| {
        // SAFETY: `out` is the local out-pointer `cstr_getter` supplies; the seam guards exceptions.
        unsafe { sipi_wwwroute(out) }
    })
}

/// Shared body of the `*const c_char`-out getters (docroot/wwwroute): run the FFI
/// call into a local out-pointer, then copy the process-static C string to an
/// owned `String`. `Err` carries the FFI status, or -1 on a null pointer.
fn cstr_getter(call: impl FnOnce(*mut *const c_char) -> c_int) -> Result<String, i32> {
    let mut ptr: *const c_char = std::ptr::null();
    let code = call(&mut ptr);
    if code != 0 {
        return Err(code);
    }
    if ptr.is_null() {
        return Err(-1);
    }
    // SAFETY: `ptr` is a NUL-terminated C string owned by the engine, valid for
    // the process lifetime; we copy it before returning.
    Ok(unsafe { CStr::from_ptr(ptr) }
        .to_string_lossy()
        .into_owned())
}

/// The `prefix_as_path` config knob: `true` → the IIIF prefix is a path
/// component under imgroot. `Err` carries the FFI status (500 if uninitialised).
pub fn prefix_as_path() -> Result<bool, i32> {
    let mut v: c_int = 0;
    // SAFETY: `out` is a valid pointer; the seam guards exceptions.
    let code = unsafe { sipi_prefix_as_path(&mut v) };
    if code != 0 {
        return Err(code);
    }
    Ok(v != 0)
}

/// The configured worker-thread count (the Lua config `nthreads`). `0` means the
/// operator left it auto — the caller sizes its blocking pool from the host
/// parallelism. `Err` carries the FFI status (500 if `sipi_init` has not run).
pub fn nthreads() -> Result<u32, i32> {
    let mut v: c_int = 0;
    // SAFETY: `out` is a valid pointer; the seam guards exceptions.
    let code = unsafe { sipi_nthreads(&mut v) };
    if code != 0 {
        return Err(code);
    }
    Ok(v.max(0) as u32)
}

/// The configured max POST body size in bytes (`max_post_size`). `0` means
/// unlimited — the shell then imposes no Lua-route body cap. `Err` carries the
/// FFI status (500 if `sipi_init` has not run).
pub fn max_post_size() -> Result<usize, i32> {
    let mut v: usize = 0;
    // SAFETY: `out` is a valid pointer; the seam guards exceptions.
    let code = unsafe { sipi_max_post_size(&mut v) };
    if code != 0 {
        return Err(code);
    }
    Ok(v)
}

/// The configured HTTP listen port (the Lua config `sipi.port`). Used only as
/// a fallback below `--serverport`/`SIPI_SERVERPORT`/`SIPI_RS_PORT` (plan 02
/// §6 R3). `Err` carries the FFI status (500 if `sipi_init` has not run).
pub fn port() -> Result<u16, i32> {
    let mut v: c_int = 0;
    // SAFETY: `out` is a valid pointer; the seam guards exceptions.
    let code = unsafe { sipi_port(&mut v) };
    if code != 0 {
        return Err(code);
    }
    Ok(v.clamp(0, i32::from(u16::MAX)) as u16)
}

/// One configured Lua route: HTTP method, the route prefix, and the resolved
/// script path (already composed against scriptdir by the engine).
#[derive(Clone)]
pub struct RouteEntry {
    pub method: String,
    pub route: String,
    pub script: String,
}

/// Collects emitted routes into the `Vec<RouteEntry>` at `ctx`.
extern "C" fn collect_route(
    ctx: *mut c_void,
    method: *const c_char,
    route: *const c_char,
    script: *const c_char,
) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut Vec<RouteEntry>` passed to sipi_routes.
        let out = unsafe { &mut *(ctx as *mut Vec<RouteEntry>) };
        if method.is_null() || route.is_null() || script.is_null() {
            return;
        }
        // SAFETY: the engine passes NUL-terminated C strings valid for the call.
        let (method, route, script) = unsafe {
            (
                CStr::from_ptr(method).to_string_lossy().into_owned(),
                CStr::from_ptr(route).to_string_lossy().into_owned(),
                CStr::from_ptr(script).to_string_lossy().into_owned(),
            )
        };
        out.push(RouteEntry {
            method,
            route,
            script,
        });
    }));
}

/// The configured Lua routes installed by `sipi_init` (read once at startup; the
/// shell registers an axum route per entry). `Err` carries the FFI status (500 if
/// `sipi_init` has not run).
pub fn routes() -> Result<Vec<RouteEntry>, i32> {
    let mut out: Vec<RouteEntry> = Vec::new();
    // SAFETY: `collect_route` writes into `out` via the ctx pointer; the seam
    // guards exceptions.
    let code = unsafe {
        sipi_routes(
            collect_route,
            &mut out as *mut Vec<RouteEntry> as *mut c_void,
        )
    };
    if code != 0 {
        return Err(code);
    }
    Ok(out)
}

/// Header-only image shape for a validated path (no Essentials identity —
/// the info.json path doesn't need it, so this passes no callback and costs
/// exactly one `read_shape()`; see [`image_dims_and_essentials`] for the
/// knora.json path, which wants both from a single call). `Err` carries the
/// FFI status (500 if the shape cannot be read, or -1 on an interior NUL in
/// the path).
pub fn image_dims(resolved_path: &str) -> Result<SipiImageDims, i32> {
    let c_path = CString::new(resolved_path).map_err(|_| -1)?;
    let mut dims = SipiImageDims::default();
    // SAFETY: `c_path` outlives the synchronous call; `out` is a valid pointer;
    // the seam guards exceptions.
    let code = unsafe { sipi_image_dims(c_path.as_ptr(), &mut dims, None, std::ptr::null_mut()) };
    if code != 0 {
        return Err(code);
    }
    Ok(dims)
}

/// Collects the single emitted MIME string into the `Option<String>` at `ctx`.
extern "C" fn collect_mime(ctx: *mut c_void, value: *const c_char) {
    // Mirror the sink callbacks: a Rust panic must not unwind into C++.
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut Option<String>` passed to sipi_mimetype.
        let out = unsafe { &mut *(ctx as *mut Option<String>) };
        if !value.is_null() {
            // SAFETY: the engine passes a NUL-terminated C string valid for the call.
            *out = Some(
                unsafe { CStr::from_ptr(value) }
                    .to_string_lossy()
                    .into_owned(),
            );
        }
    }));
}

/// The engine's libmagic MIME type for a validated path (one source of truth
/// with the serve paths). `Err` carries the FFI status (500), or -1 on an
/// interior NUL or a missing emit.
pub fn mimetype(resolved_path: &str) -> Result<String, i32> {
    let c_path = CString::new(resolved_path).map_err(|_| -1)?;
    let mut out: Option<String> = None;
    // SAFETY: `c_path` outlives the synchronous call; `collect_mime` writes into
    // `out` via the ctx pointer; the seam guards exceptions.
    let code = unsafe {
        sipi_mimetype(
            c_path.as_ptr(),
            collect_mime,
            &mut out as *mut Option<String> as *mut c_void,
        )
    };
    if code != 0 {
        return Err(code);
    }
    out.ok_or(-1)
}

/// An image's original-file identity, read from its embedded Essentials
/// packet: the client-declared mimetype and filename at upload time. Both
/// fields are known together or not at all — see
/// [`image_dims_and_essentials`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ImageEssentials {
    pub original_mimetype: String,
    pub original_filename: String,
}

/// Collects the jointly-emitted essentials strings into the
/// `Option<ImageEssentials>` at `ctx`. Fires at most once; a null pointer on
/// either string (should not happen — the probe emits both or neither) skips
/// the write rather than panicking.
extern "C" fn collect_essentials(
    ctx: *mut c_void,
    orig_mimetype: *const c_char,
    orig_filename: *const c_char,
) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut Option<ImageEssentials>` passed to
        // sipi_image_dims.
        let out = unsafe { &mut *(ctx as *mut Option<ImageEssentials>) };
        if orig_mimetype.is_null() || orig_filename.is_null() {
            return;
        }
        // SAFETY: the engine passes NUL-terminated C strings valid for the call.
        let (original_mimetype, original_filename) = unsafe {
            (
                CStr::from_ptr(orig_mimetype).to_string_lossy().into_owned(),
                CStr::from_ptr(orig_filename).to_string_lossy().into_owned(),
            )
        };
        *out = Some(ImageEssentials {
            original_mimetype,
            original_filename,
        });
    }));
}

/// Header-only image shape AND original-file identity, for a validated path,
/// from a SINGLE `read_shape()` call (the knora.json path wants both; see
/// [`image_dims`] for the info.json path, which wants only the shape and so
/// pays for no essentials work). The identity is `None` when the file carries
/// no Essentials packet (a plain JPEG/PNG, or a packet-less TIFF/JP2 — not an
/// error). `Err` carries the FFI status (500 if the shape cannot be read), or
/// -1 on an interior NUL in the path.
pub fn image_dims_and_essentials(
    resolved_path: &str,
) -> Result<(SipiImageDims, Option<ImageEssentials>), i32> {
    let c_path = CString::new(resolved_path).map_err(|_| -1)?;
    let mut dims = SipiImageDims::default();
    let mut essentials: Option<ImageEssentials> = None;
    // SAFETY: `c_path` outlives the synchronous call; `out` is a valid
    // pointer; `collect_essentials` writes into `essentials` via the ctx
    // pointer; the seam guards exceptions.
    let code = unsafe {
        sipi_image_dims(
            c_path.as_ptr(),
            &mut dims,
            Some(collect_essentials),
            &mut essentials as *mut Option<ImageEssentials> as *mut c_void,
        )
    };
    if code != 0 {
        return Err(code);
    }
    Ok((dims, essentials))
}

// ── Preflight wrappers ──────────────────────────────────────────────────────

/// An owned request context. Frees the underlying `shttps::RequestContext` on
/// drop, so a preflight call never leaks (the seam contract: Rust owns it).
pub struct RequestContext {
    ptr: *mut SipiRequestContext,
}

impl RequestContext {
    /// The raw context pointer, for passing to [`sipi_run_lua_route`]. Valid for
    /// the lifetime of this `RequestContext`.
    #[must_use]
    pub fn as_ptr(&self) -> *mut SipiRequestContext {
        self.ptr
    }

    /// Attach the POST body + content type (`server.content` / `content_type`).
    /// Binary-safe (the body is passed as raw bytes, not a C string).
    pub fn set_body(&self, content_type: &str, data: &[u8]) {
        let ct = CString::new(content_type).unwrap_or_default();
        // SAFETY: `ct` and `data` outlive the synchronous call; the builder
        // deep-copies; the seam guards exceptions.
        unsafe { sipi_request_context_set_body(self.ptr, ct.as_ptr(), data.as_ptr(), data.len()) };
    }

    /// Append one parsed multipart upload (`server.uploads`). A field with an
    /// interior NUL in any string is skipped (such fields cannot reach Lua).
    pub fn add_upload(
        &self,
        fieldname: &str,
        origname: &str,
        tmpname: &str,
        mimetype: &str,
        filesize: u64,
    ) {
        let (Ok(f), Ok(o), Ok(t), Ok(m)) = (
            CString::new(fieldname),
            CString::new(origname),
            CString::new(tmpname),
            CString::new(mimetype),
        ) else {
            return;
        };
        // SAFETY: the C strings outlive the synchronous call; deep-copied; guarded.
        unsafe {
            sipi_request_context_add_upload(
                self.ptr,
                f.as_ptr(),
                o.as_ptr(),
                t.as_ptr(),
                m.as_ptr(),
                filesize,
            )
        };
    }

    /// Append a GET (`kind` = 0) or POST (`kind` = 1) form parameter (`server.get`
    /// / `server.post`; both visible through `server.request`).
    pub fn add_param(&self, kind: i32, name: &str, value: &str) {
        let (Ok(n), Ok(v)) = (CString::new(name), CString::new(value)) else {
            return;
        };
        // SAFETY: the C strings outlive the synchronous call; deep-copied; guarded.
        unsafe { sipi_request_context_add_param(self.ptr, kind, n.as_ptr(), v.as_ptr()) };
    }

    /// Set `server.docroot` for a docroot `.lua`/`.elua` script. An interior NUL
    /// is dropped (the field then stays unset, as for a configured route).
    pub fn set_docroot(&self, docroot: &str) {
        let Ok(d) = CString::new(docroot) else {
            return;
        };
        // SAFETY: `d` outlives the synchronous call; the engine deep-copies; guarded.
        unsafe { sipi_request_context_set_docroot(self.ptr, d.as_ptr()) };
    }
}

impl Drop for RequestContext {
    fn drop(&mut self) {
        // SAFETY: `ptr` came from sipi_make_request_context and is freed exactly
        // once (this Drop); null is a no-op on the C side.
        unsafe { sipi_free_request_context(self.ptr) }
    }
}

/// The primitive request fields the preflight hooks read, grouped for the
/// [`build_request_context`] FFI builder.
pub struct RequestFields<'a> {
    pub method: &'a str,
    pub client_ip: &'a str,
    pub client_port: i32,
    pub secure: bool,
    pub host: &'a str,
    pub uri: &'a str,
    pub headers: &'a [(String, String)],
    pub cookies: &'a [(String, String)],
}

/// Build the opaque request context the preflight hooks read, from primitive
/// request fields. Returns `None` on an interior NUL or an allocation failure.
pub fn build_request_context(fields: RequestFields) -> Option<RequestContext> {
    let c_method = CString::new(fields.method).ok()?;
    let c_client_ip = CString::new(fields.client_ip).ok()?;
    let c_host = CString::new(fields.host).ok()?;
    let c_uri = CString::new(fields.uri).ok()?;

    // Own every C string for the duration of the call; the builder deep-copies,
    // so they only need to outlive the synchronous call. CString heap buffers are
    // stable across the Vec growth, so the recorded pointers stay valid.
    let mut owned: Vec<CString> =
        Vec::with_capacity((fields.headers.len() + fields.cookies.len()) * 2);
    fn pair(owned: &mut Vec<CString>, k: &str, v: &str) -> Option<SipiStrPair> {
        let ck = CString::new(k).ok()?;
        let cv = CString::new(v).ok()?;
        let p = SipiStrPair {
            name: ck.as_ptr(),
            value: cv.as_ptr(),
        };
        owned.push(ck);
        owned.push(cv);
        Some(p)
    }
    let mut header_pairs = Vec::with_capacity(fields.headers.len());
    for (k, v) in fields.headers {
        header_pairs.push(pair(&mut owned, k, v)?);
    }
    let mut cookie_pairs = Vec::with_capacity(fields.cookies.len());
    for (k, v) in fields.cookies {
        cookie_pairs.push(pair(&mut owned, k, v)?);
    }

    // SAFETY: all pointers outlive the synchronous call; the builder deep-copies
    // and returns an owned handle (or null). The seam guards C++ exceptions.
    let ptr = unsafe {
        sipi_make_request_context(
            c_method.as_ptr(),
            c_client_ip.as_ptr(),
            fields.client_port,
            c_int::from(fields.secure),
            c_host.as_ptr(),
            c_uri.as_ptr(),
            header_pairs.as_ptr(),
            header_pairs.len(),
            cookie_pairs.as_ptr(),
            cookie_pairs.len(),
        )
    };
    if ptr.is_null() {
        None
    } else {
        Some(RequestContext { ptr })
    }
}

/// The parsed result of a preflight hook: the permission and the open kv channel
/// (`infile` plus `watermark` / `size` / auth-service keys).
pub struct PreflightOutcome {
    pub permission: SipiPermType,
    pub kv: Vec<(String, String)>,
}

impl PreflightOutcome {
    /// The value of a kv key, if the hook emitted it.
    #[must_use]
    pub fn get(&self, key: &str) -> Option<&str> {
        self.kv
            .iter()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v.as_str())
    }
}

/// Collects emitted kv pairs into the `Vec<(String, String)>` at `ctx`.
extern "C" fn collect_kv(ctx: *mut c_void, key: *const c_char, value: *const c_char) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut Vec<(String, String)>` passed to the call.
        let kv = unsafe { &mut *(ctx as *mut Vec<(String, String)>) };
        if !key.is_null() && !value.is_null() {
            // SAFETY: the engine passes NUL-terminated C strings valid for the call.
            let (k, v) = unsafe {
                (
                    CStr::from_ptr(key).to_string_lossy().into_owned(),
                    CStr::from_ptr(value).to_string_lossy().into_owned(),
                )
            };
            kv.push((k, v));
        }
    }));
}

/// A response a `pre_flight`/`file_pre_flight` hook wrote directly to its
/// response sink (`server.sendStatus`/`sendHeader`/`server.print`) instead of,
/// or alongside, returning a permission decision. `status == 0` (the initial
/// value) means the hook never touched the sink — the normal case, where the
/// caller dispatches on the returned permission as before. Captured
/// synchronously into plain fields, not the streaming machinery in `sink.rs`:
/// a preflight response is a script's own error/redirect page, never the
/// multi-megabyte body the image/file serve paths stream.
#[derive(Default)]
struct PreflightCapture {
    status: u16,
    headers: Vec<(String, String)>,
    body: Vec<u8>,
}

impl PreflightCapture {
    /// The `SipiResponse` wired into `sipi_preflight`/`sipi_file_preflight`'s
    /// `resp` parameter, writing into `self` via its `ctx` pointer.
    fn as_sipi_response(&mut self) -> SipiResponse {
        SipiResponse {
            ctx: self as *mut PreflightCapture as *mut c_void,
            set_status: Some(pf_set_status),
            add_header: Some(pf_add_header),
            write: Some(pf_write),
            send_file: None,
            cancelled: None,
        }
    }

    /// `Some` iff the hook actually wrote to the sink.
    fn into_direct_response(self) -> Option<DirectResponse> {
        (self.status != 0).then_some(DirectResponse {
            status: self.status,
            headers: self.headers,
            body: self.body,
        })
    }
}

extern "C" fn pf_set_status(ctx: *mut c_void, status: c_int) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut PreflightCapture` the sink was built with;
        // the engine calls this synchronously within the `sipi_preflight` call.
        let cap = unsafe { &mut *(ctx as *mut PreflightCapture) };
        cap.status = status as u16;
    }));
}

extern "C" fn pf_add_header(ctx: *mut c_void, name: *const c_char, value: *const c_char) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: as for `pf_set_status`.
        let cap = unsafe { &mut *(ctx as *mut PreflightCapture) };
        // SAFETY: the engine passes NUL-terminated C strings valid for the call.
        let (name, value) = unsafe {
            (
                CStr::from_ptr(name).to_string_lossy().into_owned(),
                CStr::from_ptr(value).to_string_lossy().into_owned(),
            )
        };
        cap.headers.push((name, value));
    }));
}

extern "C" fn pf_write(ctx: *mut c_void, data: *const u8, len: usize) -> c_int {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: as for `pf_set_status`.
        let cap = unsafe { &mut *(ctx as *mut PreflightCapture) };
        if !data.is_null() && len > 0 {
            // SAFETY: the engine guarantees `data` points at `len` valid bytes.
            cap.body
                .extend_from_slice(unsafe { std::slice::from_raw_parts(data, len) });
        }
        0
    }))
    .unwrap_or(1)
}

/// A response the hook emitted directly — see [`PreflightCapture`]. The
/// caller must render this as the final HTTP response instead of dispatching
/// on a permission: the hook chose to answer the request itself.
pub struct DirectResponse {
    pub status: u16,
    pub headers: Vec<(String, String)>,
    pub body: Vec<u8>,
}

/// Everything a preflight call can produce: a direct response (the hook
/// answered itself), a normal permission outcome, or both absent only when
/// the underlying call errored with nothing to relay (see [`preflight`]).
pub struct PreflightResult {
    pub direct_response: Option<DirectResponse>,
    pub outcome: Option<PreflightOutcome>,
}

/// Run the IIIF `pre_flight` hook. `Err` carries the FFI status, or -1 on an
/// interior NUL — only when the hook neither returned a valid permission nor
/// wrote a direct response (a genuine failure with nothing to relay). A hook
/// that fails validation but *did* write a response (e.g. an auth script that
/// sends its own 500 before `return false`) still resolves `Ok`, with
/// `outcome: None` and `direct_response: Some(..)`.
pub fn preflight(
    prefix: &str,
    identifier: &str,
    ctx: &RequestContext,
) -> Result<PreflightResult, i32> {
    let c_prefix = CString::new(prefix).map_err(|_| -1)?;
    let c_identifier = CString::new(identifier).map_err(|_| -1)?;
    let mut permission = SipiPermType::Deny;
    let mut kv: Vec<(String, String)> = Vec::new();
    let mut capture = PreflightCapture::default();
    let resp = capture.as_sipi_response();
    // SAFETY: the C strings + ctx + resp outlive the synchronous call;
    // collect_kv/pf_* write into `kv`/`capture` via their ctx pointers; the
    // seam guards exceptions.
    let code = unsafe {
        sipi_preflight(
            c_prefix.as_ptr(),
            c_identifier.as_ptr(),
            ctx.ptr,
            &mut permission,
            collect_kv,
            &mut kv as *mut Vec<(String, String)> as *mut c_void,
            &resp,
        )
    };
    let direct_response = capture.into_direct_response();
    if code != 0 {
        return match direct_response {
            Some(dr) => Ok(PreflightResult {
                direct_response: Some(dr),
                outcome: None,
            }),
            None => Err(code),
        };
    }
    Ok(PreflightResult {
        direct_response,
        outcome: Some(PreflightOutcome { permission, kv }),
    })
}

/// Run the `/file` `file_pre_flight` hook (narrower permission set). Same
/// direct-response contract as [`preflight`].
pub fn file_preflight(filepath: &str, ctx: &RequestContext) -> Result<PreflightResult, i32> {
    let c_filepath = CString::new(filepath).map_err(|_| -1)?;
    let mut permission = SipiPermType::Deny;
    let mut kv: Vec<(String, String)> = Vec::new();
    let mut capture = PreflightCapture::default();
    let resp = capture.as_sipi_response();
    // SAFETY: as for `preflight`.
    let code = unsafe {
        sipi_file_preflight(
            c_filepath.as_ptr(),
            ctx.ptr,
            &mut permission,
            collect_kv,
            &mut kv as *mut Vec<(String, String)> as *mut c_void,
            &resp,
        )
    };
    let direct_response = capture.into_direct_response();
    if code != 0 {
        return match direct_response {
            Some(dr) => Ok(PreflightResult {
                direct_response: Some(dr),
                outcome: None,
            }),
            None => Err(code),
        };
    }
    Ok(PreflightResult {
        direct_response,
        outcome: Some(PreflightOutcome { permission, kv }),
    })
}

/// Whether the engine Lua config defines a `pre_flight` hook (read once at
/// startup; falling back to a default path + allow when absent).
pub fn has_preflight() -> Result<bool, i32> {
    let mut v: c_int = 0;
    // SAFETY: `out` is a valid pointer; the seam guards exceptions.
    let code = unsafe { sipi_has_preflight(&mut v) };
    if code != 0 {
        return Err(code);
    }
    Ok(v != 0)
}

/// Whether the engine Lua config defines a `file_pre_flight` hook.
pub fn has_file_preflight() -> Result<bool, i32> {
    let mut v: c_int = 0;
    // SAFETY: `out` is a valid pointer; the seam guards exceptions.
    let code = unsafe { sipi_has_file_preflight(&mut v) };
    if code != 0 {
        return Err(code);
    }
    Ok(v != 0)
}
