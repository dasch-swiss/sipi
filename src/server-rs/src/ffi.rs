//! Rust bindings for the narrow C FFI seam (`src/ffi/sipi_ffi.h`).
//!
//! Hand-written rather than bindgen-generated: the seam is a small, locked
//! `extern "C"` contract (strangler-fig Phase B; ADR-0013), so mirroring it by
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

    /// Hands argv verbatim to the C++ CLI11 parser (`//src/cli:cli_app`) and
    /// returns the process exit code — no `exit()`/`abort()` from inside the
    /// FFI, so the Rust caller owns teardown. Drives the offline subcommands
    /// (`convert`/`verify`/`query`/`compare`/`health`) the Rust shell does not
    /// own, plus top-level `--version`/`--help`.
    pub fn sipi_cli_main(argc: c_int, argv: *mut *mut c_char) -> c_int;

    /// Parse the Lua config and install the engine + Lua config from scratch.
    /// Must run once before any serve call (`engine_context()` hard-fails
    /// otherwise). `overrides` is the opaque `SipiServerConfig*` (CLI/env
    /// tweaks); the shell passes null today — the Lua config file is
    /// authoritative. Returns 0 on success, non-zero on failure.
    pub fn sipi_init(lua_config_path: *const c_char, overrides: *const c_void) -> c_int;

    /// The configured image root (`resolved` = 0 → raw config value for the path
    /// build; 1 → realpath()-resolved root for the containment check). `*out` is
    /// set to process-static memory owned by the engine (never freed). Returns 0,
    /// or 500 if `sipi_init` has not run.
    pub fn sipi_imgroot(resolved: c_int, out: *mut *const c_char) -> c_int;

    /// The `prefix_as_path` config knob (`*out` = 1/0). Returns 0, or 500 if
    /// `sipi_init` has not run.
    pub fn sipi_prefix_as_path(out: *mut c_int) -> c_int;

    /// The configured worker-thread count (`*out`); 0 = auto. Returns 0, or 500
    /// if `sipi_init` has not run.
    pub fn sipi_nthreads(out: *mut c_int) -> c_int;

    /// Header-only image-shape probe (no full decode). `resolved_path` is an
    /// already-validated absolute path. Returns 0 (and fills `*out`), or 500.
    pub fn sipi_image_dims(resolved_path: *const c_char, out: *mut SipiImageDims) -> c_int;

    /// The engine's libmagic MIME type for a file, emitted once via `emit`.
    /// `resolved_path` is an already-validated absolute path. Returns 0, or 500.
    pub fn sipi_mimetype(resolved_path: *const c_char, emit: SipiStrFn, ctx: *mut c_void) -> c_int;

    /// Run the IIIF `pre_flight(prefix, identifier, cookie)` hook against `ctx`.
    /// Writes the permission to `*type` and emits each kv pair (incl. `infile`)
    /// via `emit_kv`. Returns 0, or 500 on a Lua/validation failure.
    pub fn sipi_preflight(
        prefix: *const c_char,
        identifier: *const c_char,
        ctx: *mut SipiRequestContext,
        ty: *mut SipiPermType,
        emit_kv: SipiKVFn,
        kv_ctx: *mut c_void,
    ) -> c_int;

    /// Run the `/file` `file_pre_flight(filepath, cookie)` hook (narrower
    /// permission set). Same out-channel contract as [`sipi_preflight`].
    pub fn sipi_file_preflight(
        filepath: *const c_char,
        ctx: *mut SipiRequestContext,
        ty: *mut SipiPermType,
        emit_kv: SipiKVFn,
        kv_ctx: *mut c_void,
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
/// run once before serving images. Passes null overrides — the Lua config file
/// is authoritative. Returns the FFI status code on failure (non-zero).
pub fn init(config_path: &str) -> Result<(), i32> {
    let c_path = match CString::new(config_path) {
        Ok(p) => p,
        Err(_) => return Err(-1), // interior NUL in the path
    };
    // SAFETY: `c_path` is a valid NUL-terminated string outliving the call;
    // `overrides` is null (no CLI/env overrides); the seam guards exceptions.
    let code = unsafe { sipi_init(c_path.as_ptr(), std::ptr::null()) };
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
    Ok(unsafe { CStr::from_ptr(ptr) }.to_string_lossy().into_owned())
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

/// Header-only image shape for a validated path. `Err` carries the FFI status
/// (500 if the shape cannot be read, or -1 on an interior NUL in the path).
pub fn image_dims(resolved_path: &str) -> Result<SipiImageDims, i32> {
    let c_path = CString::new(resolved_path).map_err(|_| -1)?;
    let mut dims = SipiImageDims::default();
    // SAFETY: `c_path` outlives the synchronous call; `out` is a valid pointer;
    // the seam guards exceptions.
    let code = unsafe { sipi_image_dims(c_path.as_ptr(), &mut dims) };
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
            *out = Some(unsafe { CStr::from_ptr(value) }.to_string_lossy().into_owned());
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
        sipi_mimetype(c_path.as_ptr(), collect_mime, &mut out as *mut Option<String> as *mut c_void)
    };
    if code != 0 {
        return Err(code);
    }
    out.ok_or(-1)
}

// ── Preflight wrappers ──────────────────────────────────────────────────────

/// An owned request context. Frees the underlying `shttps::RequestContext` on
/// drop, so a preflight call never leaks (the seam contract: Rust owns it).
pub struct RequestContext {
    ptr: *mut SipiRequestContext,
}

impl Drop for RequestContext {
    fn drop(&mut self) {
        // SAFETY: `ptr` came from sipi_make_request_context and is freed exactly
        // once (this Drop); null is a no-op on the C side.
        unsafe { sipi_free_request_context(self.ptr) }
    }
}

/// Build the opaque request context the preflight hooks read, from primitive
/// request fields. Returns `None` on an interior NUL or an allocation failure.
pub fn build_request_context(
    method: &str,
    client_ip: &str,
    client_port: i32,
    secure: bool,
    host: &str,
    uri: &str,
    headers: &[(String, String)],
    cookies: &[(String, String)],
) -> Option<RequestContext> {
    let c_method = CString::new(method).ok()?;
    let c_client_ip = CString::new(client_ip).ok()?;
    let c_host = CString::new(host).ok()?;
    let c_uri = CString::new(uri).ok()?;

    // Own every C string for the duration of the call; the builder deep-copies,
    // so they only need to outlive the synchronous call. CString heap buffers are
    // stable across the Vec growth, so the recorded pointers stay valid.
    let mut owned: Vec<CString> = Vec::with_capacity((headers.len() + cookies.len()) * 2);
    fn pair(owned: &mut Vec<CString>, k: &str, v: &str) -> Option<SipiStrPair> {
        let ck = CString::new(k).ok()?;
        let cv = CString::new(v).ok()?;
        let p = SipiStrPair { name: ck.as_ptr(), value: cv.as_ptr() };
        owned.push(ck);
        owned.push(cv);
        Some(p)
    }
    let mut header_pairs = Vec::with_capacity(headers.len());
    for (k, v) in headers {
        header_pairs.push(pair(&mut owned, k, v)?);
    }
    let mut cookie_pairs = Vec::with_capacity(cookies.len());
    for (k, v) in cookies {
        cookie_pairs.push(pair(&mut owned, k, v)?);
    }

    // SAFETY: all pointers outlive the synchronous call; the builder deep-copies
    // and returns an owned handle (or null). The seam guards C++ exceptions.
    let ptr = unsafe {
        sipi_make_request_context(
            c_method.as_ptr(),
            c_client_ip.as_ptr(),
            client_port,
            c_int::from(secure),
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
        self.kv.iter().find(|(k, _)| k == key).map(|(_, v)| v.as_str())
    }
}

/// Collects emitted kv pairs into the `Vec<(String, String)>` at `ctx`.
extern "C" fn collect_kv(ctx: *mut c_void, key: *const c_char, value: *const c_char) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut Vec<(String, String)>` passed to the call.
        let kv = unsafe { &mut *(ctx as *mut Vec<(String, String)>) };
        if !key.is_null() && !value.is_null() {
            // SAFETY: the engine passes NUL-terminated C strings valid for the call.
            let k = unsafe { CStr::from_ptr(key) }.to_string_lossy().into_owned();
            let v = unsafe { CStr::from_ptr(value) }.to_string_lossy().into_owned();
            kv.push((k, v));
        }
    }));
}

/// Run the IIIF `pre_flight` hook. `Err` carries the FFI status (500), or -1 on
/// an interior NUL.
pub fn preflight(prefix: &str, identifier: &str, ctx: &RequestContext) -> Result<PreflightOutcome, i32> {
    let c_prefix = CString::new(prefix).map_err(|_| -1)?;
    let c_identifier = CString::new(identifier).map_err(|_| -1)?;
    let mut permission = SipiPermType::Deny;
    let mut kv: Vec<(String, String)> = Vec::new();
    // SAFETY: the C strings + ctx outlive the synchronous call; collect_kv writes
    // into `kv` via the ctx pointer; the seam guards exceptions.
    let code = unsafe {
        sipi_preflight(
            c_prefix.as_ptr(),
            c_identifier.as_ptr(),
            ctx.ptr,
            &mut permission,
            collect_kv,
            &mut kv as *mut Vec<(String, String)> as *mut c_void,
        )
    };
    if code != 0 {
        return Err(code);
    }
    Ok(PreflightOutcome { permission, kv })
}

/// Run the `/file` `file_pre_flight` hook (narrower permission set).
pub fn file_preflight(filepath: &str, ctx: &RequestContext) -> Result<PreflightOutcome, i32> {
    let c_filepath = CString::new(filepath).map_err(|_| -1)?;
    let mut permission = SipiPermType::Deny;
    let mut kv: Vec<(String, String)> = Vec::new();
    // SAFETY: as for `preflight`.
    let code = unsafe {
        sipi_file_preflight(
            c_filepath.as_ptr(),
            ctx.ptr,
            &mut permission,
            collect_kv,
            &mut kv as *mut Vec<(String, String)> as *mut c_void,
        )
    };
    if code != 0 {
        return Err(code);
    }
    Ok(PreflightOutcome { permission, kv })
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
