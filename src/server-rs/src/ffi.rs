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

    /// Header-only image-shape probe (no full decode). `resolved_path` is an
    /// already-validated absolute path. Returns 0 (and fills `*out`), or 500.
    pub fn sipi_image_dims(resolved_path: *const c_char, out: *mut SipiImageDims) -> c_int;

    /// The engine's libmagic MIME type for a file, emitted once via `emit`.
    /// `resolved_path` is an already-validated absolute path. Returns 0, or 500.
    pub fn sipi_mimetype(resolved_path: *const c_char, emit: SipiStrFn, ctx: *mut c_void) -> c_int;
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
