//! The Rust side of the `SipiResponse` sink (strangler-fig Phase C).
//!
//! The C++ engine emits the whole response through the [`crate::ffi::SipiResponse`]
//! callbacks **during** a synchronous `sipi_serve_*` call: `set_status` once
//! first, then each `add_header`, then exactly one body — either `send_file`
//! (a known-length file region) or repeated `write` (an unknown-length stream)
//! — and `cancelled` is polled between stages.
//!
//! T5 **buffers** the response: the callbacks accumulate status + headers + body
//! into a [`SinkState`], and the caller builds an axum [`Response`] after the
//! serve call returns. This holds the encoded image in memory; true streaming
//! (so large responses don't buffer) is a later refinement (T9). Decode memory
//! is already bounded by the engine's memory budget (installed by `sipi_init`).

use axum::body::Body;
use axum::http::{HeaderName, HeaderValue, StatusCode};
use axum::response::Response;
use std::ffi::CStr;
use std::io::{Read, Seek, SeekFrom};
use std::os::raw::{c_char, c_int, c_void};

use crate::ffi::SipiResponse;

/// Accumulates the response the engine emits through the sink callbacks.
#[derive(Default)]
pub struct SinkState {
    status: u16,
    headers: Vec<(String, String)>,
    body: Vec<u8>,
    /// A `send_file` read failed — the caller should treat the response as a 500
    /// (the engine already committed status/headers, but the body is incomplete).
    io_error: bool,
}

// Each callback wraps its body in `catch_unwind` — the Rust-side analog of the
// C++ `sipi_guard`. The engine calls these synchronously across the C ABI, so a
// Rust panic must not unwind into C++; on panic the void callbacks swallow and
// the `c_int` callbacks return a non-zero "failure" sentinel (the engine then
// aborts the write, and the caller renders a 500). `AssertUnwindSafe` is sound
// because a panic discards the (then-unused) partial response.

extern "C" fn cb_set_status(ctx: *mut c_void, status: c_int) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut SinkState` the sink was built with; the
        // engine calls this synchronously on the serving thread, no aliasing race.
        let state = unsafe { &mut *(ctx as *mut SinkState) };
        state.status = status as u16;
    }));
}

extern "C" fn cb_add_header(ctx: *mut c_void, name: *const c_char, value: *const c_char) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let state = unsafe { &mut *(ctx as *mut SinkState) };
        // SAFETY: the engine passes NUL-terminated C strings valid for the call.
        let name = unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned();
        let value = unsafe { CStr::from_ptr(value) }.to_string_lossy().into_owned();
        state.headers.push((name, value));
    }));
}

extern "C" fn cb_write(ctx: *mut c_void, data: *const u8, len: usize) -> c_int {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let state = unsafe { &mut *(ctx as *mut SinkState) };
        if !data.is_null() && len > 0 {
            // SAFETY: the engine guarantees `data` points at `len` valid bytes.
            let slice = unsafe { std::slice::from_raw_parts(data, len) };
            state.body.extend_from_slice(slice);
        }
        0
    }))
    .unwrap_or(1)
}

extern "C" fn cb_send_file(ctx: *mut c_void, path: *const c_char, offset: u64, length: u64) -> c_int {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let state = unsafe { &mut *(ctx as *mut SinkState) };
        let path = unsafe { CStr::from_ptr(path) }.to_string_lossy().into_owned();
        match read_file_region(&path, offset, length) {
            Ok(mut bytes) => {
                state.body.append(&mut bytes);
                0
            }
            Err(_) => {
                state.io_error = true;
                1
            }
        }
    }))
    .unwrap_or(1)
}

extern "C" fn cb_cancelled(_ctx: *mut c_void) -> c_int {
    // T9 wires client-disconnect / timeout detection here. Until then a serve
    // never reports cancellation.
    0
}

fn read_file_region(path: &str, offset: u64, length: u64) -> std::io::Result<Vec<u8>> {
    let mut f = std::fs::File::open(path)?;
    if offset > 0 {
        f.seek(SeekFrom::Start(offset))?;
    }
    let mut buf = vec![0u8; length as usize];
    f.read_exact(&mut buf)?;
    Ok(buf)
}

/// Run a `sipi_serve_*` call against a buffering sink and return the engine's
/// status code plus the accumulated [`SinkState`]. The closure receives the
/// `SipiResponse` to hand to the FFI entry. The call must be **synchronous** —
/// the sink's `ctx` aliases `state` only for the duration of `call`.
pub fn serve_buffered<F>(call: F) -> (i32, SinkState)
where
    F: FnOnce(&SipiResponse) -> i32,
{
    // Box keeps the SinkState address stable for the raw `ctx` pointer.
    let mut state = Box::new(SinkState::default());
    let resp = SipiResponse {
        ctx: &mut *state as *mut SinkState as *mut c_void,
        set_status: Some(cb_set_status),
        add_header: Some(cb_add_header),
        write: Some(cb_write),
        send_file: Some(cb_send_file),
        cancelled: Some(cb_cancelled),
    };
    let code = call(&resp);
    (code, *state)
}

/// Build an axum [`Response`] from a buffered [`SinkState`] (after a serve call
/// that returned 0). A `send_file` read error or an unset status maps to 500.
pub fn into_response(state: SinkState) -> Response {
    if state.io_error {
        return error_response(StatusCode::INTERNAL_SERVER_ERROR);
    }
    let status = StatusCode::from_u16(state.status).unwrap_or(StatusCode::INTERNAL_SERVER_ERROR);
    let mut builder = Response::builder().status(status);
    for (name, value) in &state.headers {
        match (HeaderName::from_bytes(name.as_bytes()), HeaderValue::from_str(value)) {
            (Ok(n), Ok(v)) => builder = builder.header(n, v),
            // A header the engine emitted that http rejects (control chars etc.)
            // is dropped rather than failing the whole response; the engine
            // already sanitises header values at the boundary.
            _ => tracing::warn!(header = %name, "dropping malformed response header"),
        }
    }
    builder
        .body(Body::from(state.body))
        .unwrap_or_else(|_| error_response(StatusCode::INTERNAL_SERVER_ERROR))
}

/// A bare status response with no body and no internal detail (DEV-6062).
pub fn error_response(status: StatusCode) -> Response {
    Response::builder()
        .status(status)
        .body(Body::empty())
        .expect("static empty-body response is always valid")
}
