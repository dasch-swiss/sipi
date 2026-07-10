//! The Rust side of the `SipiResponse` sink (strangler-fig rewrite).
//!
//! The C++ engine emits the whole response through the [`crate::ffi::SipiResponse`]
//! callbacks **during** a synchronous `sipi_serve_*` call: `set_status` once
//! first, then each `add_header`, then exactly one body — either `send_file`
//! (a known-length file region) or repeated `write` (an unknown-length stream)
//! — and `cancelled` is polled between stages.
//!
//! The sink **streams**. The engine runs on a `spawn_blocking` thread; the
//! response head (status + headers) is handed to the async handler through a
//! oneshot the moment the engine commits it, then body chunks flow over a
//! bounded mpsc channel as they are produced — neither the encoded image nor a
//! `/file` region is ever fully buffered. A slow client back-pressures the engine
//! thread (the bounded channel blocks the blocking [`cb_write`]); a disconnected
//! client drops the receiver, which [`cb_cancelled`] observes (and a body send
//! then fails), so the engine aborts instead of finishing work nobody reads.
//! Body-less responses (HEAD / errors) are delivered whole via [`Outcome::Complete`].

use axum::body::{Body, Bytes};
use axum::http::response::Builder;
use axum::http::{HeaderName, HeaderValue, StatusCode};
use axum::response::Response;
use std::ffi::CStr;
use std::io::{Read, Seek, SeekFrom};
use std::os::raw::{c_char, c_int, c_void};
use tokio::sync::{mpsc, oneshot};
use tokio_stream::wrappers::ReceiverStream;
use tokio_stream::StreamExt;

use crate::ffi::SipiResponse;

/// Body-chunk channel capacity: bounds in-flight memory to `CAP × chunk` and
/// back-pressures the engine thread when the client drains slowly.
pub const BODY_CHANNEL_CAP: usize = 16;

/// Read size for the `send_file` body mode (the `/file` region is streamed, not
/// read into memory).
const FILE_CHUNK: usize = 64 * 1024;

/// What the blocking engine dispatch hands back to the async handler.
pub enum Outcome {
    /// A fully-formed response (JSON, redirect, error, or an empty/HEAD body).
    Complete(Response),
    /// The committed head of a streaming body; chunks follow on the body channel.
    StreamHead {
        status: u16,
        headers: Vec<(String, String)>,
    },
}

/// Accumulates the head and bridges the engine's body callbacks to the async
/// handler's channels. Lives (boxed) on the blocking thread for the duration of
/// one `sipi_serve_*` call.
struct StreamSink {
    status: u16,
    headers: Vec<(String, String)>,
    /// Sends the head exactly once — taken on the first body callback, or in
    /// [`serve_streaming`]'s tail for a body-less response.
    outcome_tx: Option<oneshot::Sender<Outcome>>,
    body_tx: mpsc::Sender<Bytes>,
    head_sent: bool,
}

impl StreamSink {
    /// Hand the committed head to the async handler so it can start the body
    /// stream. Idempotent — only the first body callback delivers it.
    fn send_head(&mut self) {
        if self.head_sent {
            return;
        }
        self.head_sent = true;
        if let Some(tx) = self.outcome_tx.take() {
            let _ = tx.send(Outcome::StreamHead {
                status: self.status,
                headers: std::mem::take(&mut self.headers),
            });
        }
    }
}

// Each callback wraps its body in `catch_unwind` — the Rust-side analog of the
// C++ `sipi_guard`. The engine calls these synchronously on the blocking thread,
// so a Rust panic must not unwind into C++; on panic the void callbacks swallow
// and the `c_int` callbacks return a non-zero "failure" sentinel (the engine then
// aborts the write). `AssertUnwindSafe` is sound: a panic abandons the response.

extern "C" fn cb_set_status(ctx: *mut c_void, status: c_int) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut StreamSink` the sink was built with; the
        // engine calls this synchronously on the serving thread, no aliasing race.
        let state = unsafe { &mut *(ctx as *mut StreamSink) };
        state.status = status as u16;
    }));
}

extern "C" fn cb_add_header(ctx: *mut c_void, name: *const c_char, value: *const c_char) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut StreamSink` the sink was built with; the
        // engine calls this synchronously on the serving thread, no aliasing race.
        let state = unsafe { &mut *(ctx as *mut StreamSink) };
        // SAFETY: the engine passes NUL-terminated C strings valid for the call.
        let (name, value) = unsafe {
            (
                CStr::from_ptr(name).to_string_lossy().into_owned(),
                CStr::from_ptr(value).to_string_lossy().into_owned(),
            )
        };
        state.headers.push((name, value));
    }));
}

extern "C" fn cb_write(ctx: *mut c_void, data: *const u8, len: usize) -> c_int {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut StreamSink` the sink was built with; the
        // engine calls this synchronously on the serving thread, no aliasing race.
        let state = unsafe { &mut *(ctx as *mut StreamSink) };
        state.send_head();
        if data.is_null() || len == 0 {
            return 0;
        }
        // SAFETY: the engine guarantees `data` points at `len` valid bytes.
        let chunk = Bytes::copy_from_slice(unsafe { std::slice::from_raw_parts(data, len) });
        // blocking_send back-pressures a slow client; Err = the receiver is gone
        // (client disconnected) → tell the engine to abort + unlink partial cache.
        match state.body_tx.blocking_send(chunk) {
            Ok(()) => 0,
            Err(_) => 1,
        }
    }))
    .unwrap_or(1)
}

extern "C" fn cb_send_file(
    ctx: *mut c_void,
    path: *const c_char,
    offset: u64,
    length: u64,
) -> c_int {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `ctx` is the `&mut StreamSink` the sink was built with; the
        // engine calls this synchronously on the serving thread, no aliasing race.
        let state = unsafe { &mut *(ctx as *mut StreamSink) };
        state.send_head();
        // SAFETY: the engine passes a NUL-terminated C string valid for the call.
        let path = unsafe { CStr::from_ptr(path) }
            .to_string_lossy()
            .into_owned();
        stream_file_region(&state.body_tx, &path, offset, length)
    }))
    .unwrap_or(1)
}

extern "C" fn cb_cancelled(ctx: *mut c_void) -> c_int {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: as above. The client is "gone" once the body receiver is
        // dropped — the handler future was cancelled (disconnect during decode,
        // before the head) or the streamed response body was dropped (disconnect
        // mid-encode). A best-effort signal the engine polls between stages.
        let state = unsafe { &mut *(ctx as *mut StreamSink) };
        c_int::from(state.body_tx.is_closed())
    }))
    .unwrap_or(0)
}

/// Stream a `[offset, offset+length)` file region to the body channel in chunks,
/// without buffering the whole region. Returns 1 on a read error or a gone client
/// (the committed head can't be unsaid, so the body simply truncates — matching
/// the C++ transport, which drops the connection on a mid-send failure).
///
/// `pub(crate)` so the `/server` docroot fileserver streams static files through
/// the same bounded-memory path the engine `send_file` callback uses (run it on a
/// `spawn_blocking` thread — the reads are blocking `std::fs`).
pub(crate) fn stream_file_region(
    body_tx: &mpsc::Sender<Bytes>,
    path: &str,
    offset: u64,
    length: u64,
) -> c_int {
    let Ok(mut f) = std::fs::File::open(path) else {
        return 1;
    };
    if offset > 0 && f.seek(SeekFrom::Start(offset)).is_err() {
        return 1;
    }
    let mut remaining = length;
    while remaining > 0 {
        let want = remaining.min(FILE_CHUNK as u64) as usize;
        // A fresh buffer per chunk so `Bytes::from` takes ownership without a
        // second copy. (A reused scratch buffer would force `copy_from_slice`,
        // copying every byte twice on this bandwidth-bound path.)
        let mut chunk = vec![0u8; want];
        match f.read(&mut chunk) {
            Ok(0) => return 1, // file shorter than the declared length
            Ok(n) => {
                chunk.truncate(n);
                if body_tx.blocking_send(Bytes::from(chunk)).is_err() {
                    return 1; // client gone
                }
                remaining -= n as u64;
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::Interrupted => {}
            Err(_) => return 1,
        }
    }
    0
}

/// Drive a `sipi_serve_*` call against a streaming sink, **synchronously** on the
/// caller's (blocking) thread. The head is delivered on `outcome_tx` — as a
/// [`Outcome::StreamHead`] the instant the engine commits it (body chunks then
/// flow on `body_tx`), or, for a body-less response (HEAD / pre-commit failure),
/// as a [`Outcome::Complete`] in the tail. `body_tx` is dropped on return,
/// closing the stream.
pub fn serve_streaming<F>(
    outcome_tx: oneshot::Sender<Outcome>,
    body_tx: mpsc::Sender<Bytes>,
    call: F,
) where
    F: FnOnce(&SipiResponse) -> i32,
{
    // Box keeps the StreamSink address stable for the raw `ctx` pointer.
    let mut sink = Box::new(StreamSink {
        status: 0,
        headers: Vec::new(),
        outcome_tx: Some(outcome_tx),
        body_tx,
        head_sent: false,
    });
    let resp = SipiResponse {
        ctx: &mut *sink as *mut StreamSink as *mut c_void,
        set_status: Some(cb_set_status),
        add_header: Some(cb_add_header),
        write: Some(cb_write),
        send_file: Some(cb_send_file),
        cancelled: Some(cb_cancelled),
    };
    let code = call(&resp);
    // No body callback fired (HEAD / EmptyBody, or a pre-commit failure that
    // returned a status before `apply`) → the head was never sent; deliver a
    // complete response now.
    if !sink.head_sent {
        if let Some(tx) = sink.outcome_tx.take() {
            let outcome = if code == 0 {
                Outcome::Complete(head_response(sink.status, &sink.headers))
            } else {
                Outcome::Complete(error_response(map_status(code)))
            };
            let _ = tx.send(outcome);
        }
    }
}

/// Build the streaming axum [`Response`] from an [`Outcome::StreamHead`] and the
/// body channel: chunks stream to the client as the engine produces them.
pub fn stream_response(
    status: u16,
    headers: Vec<(String, String)>,
    body_rx: mpsc::Receiver<Bytes>,
) -> Response {
    let body = Body::from_stream(ReceiverStream::new(body_rx).map(Ok::<Bytes, std::io::Error>));
    apply_headers(Response::builder().status(map_status_u16(status)), &headers)
        .body(body)
        .unwrap_or_else(|_| error_response(StatusCode::INTERNAL_SERVER_ERROR))
}

/// A body-less response (HEAD / 0-length) carrying the engine's status + headers.
fn head_response(status: u16, headers: &[(String, String)]) -> Response {
    apply_headers(Response::builder().status(map_status_u16(status)), headers)
        .body(Body::empty())
        .unwrap_or_else(|_| error_response(StatusCode::INTERNAL_SERVER_ERROR))
}

fn apply_headers(mut builder: Builder, headers: &[(String, String)]) -> Builder {
    // Last-write-wins per header name, mirroring the C++ transport's `header_out`
    // map (Connection.cpp:1194 — even Set-Cookie is keyed there): a route that
    // sets the same header twice — e.g. a Lua script that sends `Content-Type:
    // text/html` and then `application/json` via `send_response.lua` — must emit a
    // single value, not both. `HeaderMap::insert` replaces any prior value under
    // that name, giving the map semantics natively (unlike `Builder::header`,
    // which appends).
    if let Some(map) = builder.headers_mut() {
        for (name, value) in headers {
            match (
                HeaderName::from_bytes(name.as_bytes()),
                HeaderValue::from_str(value),
            ) {
                (Ok(n), Ok(v)) => {
                    map.insert(n, v);
                }
                // A header http rejects (control chars etc.) is dropped rather
                // than failing the whole response; the engine sanitises at the
                // boundary.
                _ => tracing::warn!(header = %name, "dropping malformed response header"),
            }
        }
    }
    builder
}

fn map_status(code: i32) -> StatusCode {
    StatusCode::from_u16(u16::try_from(code).unwrap_or(500))
        .unwrap_or(StatusCode::INTERNAL_SERVER_ERROR)
}

fn map_status_u16(status: u16) -> StatusCode {
    StatusCode::from_u16(status).unwrap_or(StatusCode::INTERNAL_SERVER_ERROR)
}

/// A bare status response with no body and no internal detail.
pub fn error_response(status: StatusCode) -> Response {
    Response::builder()
        .status(status)
        .body(Body::empty())
        .expect("static empty-body response is always valid")
}
