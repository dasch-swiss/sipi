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

extern "C" {
    /// Raw `/file` passthrough incl. HTTP Range / 206 — no decode.
    /// `resolved_path` is an already-validated absolute path; `range` is the raw
    /// `Range` header value or null. Returns 0 when the response was emitted, or
    /// an HTTP status code (404/400/500) on a pre-commit failure.
    pub fn sipi_serve_file(
        resolved_path: *const c_char,
        range: *const c_char,
        resp: *const SipiResponse,
    ) -> c_int;
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
