//! The IIIF request router (strangler-fig rewrite).
//!
//! Replaces the C++ `iiif_handler` dispatch (`SipiHttpServer.cpp:1326`): a
//! catch-all axum handler classifies the path with [`crate::iiif`], validates it
//! at the edge with [`crate::path`], runs the Lua preflight hook (auth + path
//! resolution) through the seam, and dispatches to the engine
//! (`sipi_serve_image` / `sipi_serve_file`) or assembles JSON with
//! [`crate::info`]. The blocking FFI calls run on a `spawn_blocking` pool bounded
//! by a semaphore sized to the configured worker count, so the async runtime
//! never stalls on the C++ engine.

use std::ffi::CString;
use std::io::Write;
use std::sync::Arc;

use axum::body::{to_bytes, Body};
use axum::extract::{DefaultBodyLimit, FromRequest, Multipart, OriginalUri, Request, State};
use axum::http::{header, HeaderMap, HeaderValue, Method, StatusCode, Uri};
use axum::response::{IntoResponse, Response};
use axum::routing::{on, MethodFilter, MethodRouter};
use tempfile::NamedTempFile;
use tokio::sync::{mpsc, oneshot};

use crate::ffi::{self, PreflightOutcome, SipiPermType, SipiResponse, SipiServeRequest};
use crate::iiif::{self, ParsedRequest, RequestKind};
use crate::info::{self, Sidecar};
use crate::path::{self, Resolved};
use crate::sink::{self, Outcome};

/// Image MIME types that take the IIIF / image-dimension code path (the same set
/// the C++ server branches on, `SipiHttpServer.cpp:568-570,913-914`).
const IMAGE_MIMES: [&str; 5] = [
    "image/tiff",
    "image/jpeg",
    "image/png",
    "image/jpx",
    "image/jp2",
];

/// Cached engine config (read once at startup, after `sipi_init`) plus the shared
/// backpressure pool that bounds concurrent engine work.
#[derive(Clone)]
pub struct AppState {
    ready: bool,
    imgroot: String,
    resolved_imgroot: String,
    prefix_as_path: bool,
    has_preflight: bool,
    has_file_preflight: bool,
    /// Permits = the configured worker count: a permit is held for the duration
    /// of each blocking engine dispatch, reconstructing the shttps
    /// thread-per-connection bound. Shared (`Arc`) across all requests.
    pool: Arc<tokio::sync::Semaphore>,
    /// Configured Lua routes (method/route/script), registered as axum routes by
    /// [`crate::app`]. Empty when the engine is uninstalled.
    pub routes: Vec<ffi::RouteEntry>,
    /// Max POST body size in bytes; 0 = unlimited. The Lua-route handler rejects
    /// oversized bodies (413), reconstructing the transport's `Connection` cap.
    max_post_size: usize,
}

impl AppState {
    /// Read the cached config from the engine. Returns a not-ready state when
    /// `sipi_init` has not run (no `--config`); the serve routes then 503.
    ///
    /// `configured_routes` is `Some` for a TOML config (the shell parsed the
    /// `[[routes]]` table Rust-side and composed each script path); `None` for a
    /// Lua config, where the routes are read back from the engine via
    /// [`ffi::routes`] after `sipi_init` installed them.
    #[must_use]
    pub fn load(configured_routes: Option<Vec<ffi::RouteEntry>>) -> Self {
        // Bound concurrent engine work to the configured worker count (the shttps
        // thread-per-connection bound, reconstructed). 0/uninitialised → size from
        // the host parallelism.
        let permits = ffi::nthreads()
            .ok()
            .filter(|n| *n > 0)
            .map_or_else(default_pool_size, |n| n as usize);
        let pool = Arc::new(tokio::sync::Semaphore::new(permits));
        match (
            ffi::imgroot(false),
            ffi::imgroot(true),
            ffi::prefix_as_path(),
        ) {
            (Ok(imgroot), Ok(resolved_imgroot), Ok(prefix_as_path)) => Self {
                ready: true,
                imgroot,
                resolved_imgroot,
                prefix_as_path,
                // A failed hook probe (engine error) disables preflight rather
                // than failing startup — the same effect as "no hook defined".
                has_preflight: ffi::has_preflight().unwrap_or(false),
                has_file_preflight: ffi::has_file_preflight().unwrap_or(false),
                pool,
                // TOML config supplies routes directly; a Lua config has them
                // read back from the engine via the seam.
                routes: configured_routes.unwrap_or_else(|| ffi::routes().unwrap_or_default()),
                max_post_size: ffi::max_post_size().unwrap_or(0),
            },
            _ => Self {
                ready: false,
                imgroot: String::new(),
                resolved_imgroot: String::new(),
                prefix_as_path: true,
                has_preflight: false,
                has_file_preflight: false,
                pool,
                routes: Vec::new(),
                max_post_size: 0,
            },
        }
    }
}

/// Pool size when the configured `nthreads` is 0 (auto) or unreadable: the host
/// parallelism, falling back to 4 when even that is unavailable.
fn default_pool_size() -> usize {
    std::thread::available_parallelism().map_or(4, |n| n.get())
}

/// The resolved access decision: the on-disk file the hook chose, the permission,
/// and the open kv channel (restrict `size`/`watermark`, auth-service urls).
struct Access {
    infile: String,
    permission: SipiPermType,
    kv: Vec<(String, String)>,
}

/// The catch-all IIIF handler (`GET|HEAD /…`). Classifies, validates, runs
/// preflight, and dispatches. Never leaks an internal path in an error body:
/// every failure renders a bare status via [`sink::error_response`].
pub async fn iiif(
    State(state): State<Arc<AppState>>,
    method: Method,
    OriginalUri(uri): OriginalUri,
    headers: HeaderMap,
) -> Response {
    if !state.ready {
        return sink::error_response(StatusCode::SERVICE_UNAVAILABLE);
    }

    let parsed = match iiif::parse_request(uri.path()) {
        Ok(p) => p,
        Err(_) => return sink::error_response(StatusCode::BAD_REQUEST),
    };

    // R1: string-level traversal check on the decoded identifier (and prefix when
    // it is a path component), before any path construction or preflight.
    if path::contains_traversal(&parsed.identifier)
        || (state.prefix_as_path && path::contains_traversal(&parsed.prefix))
    {
        tracing::warn!(identifier = %parsed.identifier, prefix = %parsed.prefix, "rejected path traversal");
        return sink::error_response(StatusCode::BAD_REQUEST);
    }

    // Redirect is cheap and engine-free — answer it on the async path.
    if parsed.kind == RequestKind::Redirect {
        return redirect(&headers, &parsed);
    }

    // Everything else drives the blocking C++ engine (the per-call preflight VM,
    // realpath, decode/encode). Bound concurrency on the pool and run the work on
    // a blocking thread so the async runtime stays responsive; a full pool sheds
    // load with 503 + Retry-After. /health, /favicon, and OPTIONS are
    // separate routes that never reach here.
    let permit = match Arc::clone(&state.pool).try_acquire_owned() {
        Ok(permit) => permit,
        Err(_) => return busy_response(),
    };
    // Shell-set headers on the streamed (success) response, captured before the
    // request is moved onto the blocking thread: the CORS Origin echo (image +
    // /file; never with credentials) and, for a /file Range request,
    // the identifier-derived Content-Disposition (SipiHttpServer.cpp:1120-1129).
    let cors_origin = header_str(&headers, "origin").and_then(|o| HeaderValue::from_str(&o).ok());
    let content_disp = (parsed.kind == RequestKind::FileDownload
        && headers.contains_key(header::RANGE))
    .then(|| content_disposition(&parsed.identifier))
    .flatten();

    // The engine commits the head (status + headers) on the oneshot, then streams
    // body chunks on the bounded mpsc as it produces them. A slow client
    // back-pressures the engine thread; a disconnect drops the receiver, which
    // the engine's cancelled() poll and a failed body send both observe, aborting
    // the work. The pool permit is held for the whole dispatch — released when the
    // blocking task ends (after the last chunk), reconstructing the shttps bound.
    let (outcome_tx, outcome_rx) = oneshot::channel::<Outcome>();
    let (body_tx, body_rx) = mpsc::channel::<axum::body::Bytes>(sink::BODY_CHANNEL_CAP);
    // Carry the request span (created by OtelAxumLayer) onto the blocking thread
    // so the engine's coarse span nests under it and its trace context is what
    // stamps the C++ engine logs.
    let request_span = tracing::Span::current();
    tokio::task::spawn_blocking(move || {
        let _permit = permit; // released when the engine work (decode/encode/stream) completes
        let _entered = request_span.enter();
        dispatch_engine(
            &state, &parsed, &method, &uri, &headers, outcome_tx, body_tx,
        );
    });
    match outcome_rx.await {
        // JSON / redirect / error / HEAD: a complete response, returned as built.
        Ok(Outcome::Complete(response)) => response,
        // Image / file: stream the body as the engine produces it, adding the
        // shell-set CORS + Content-Disposition headers to the committed head.
        Ok(Outcome::StreamHead {
            status,
            headers: head,
        }) => {
            let mut response = sink::stream_response(status, head, body_rx);
            if let Some(origin) = cors_origin {
                response
                    .headers_mut()
                    .insert(header::ACCESS_CONTROL_ALLOW_ORIGIN, origin);
            }
            if let Some(cd) = content_disp {
                if response.status().is_success() {
                    response
                        .headers_mut()
                        .insert(header::CONTENT_DISPOSITION, cd);
                }
            }
            response
        }
        // The blocking task dropped the sender without sending — a panic the
        // sink's catch_unwind somehow escaped — maps to a bare 500.
        Err(_) => sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
    }
}

/// The blocking engine dispatch, run on a `spawn_blocking` thread under a pool
/// permit: access resolution (the preflight VM), path validation (realpath), and
/// the serve call (decode/encode, or raw `/file`). Split out of [`iiif`] so the
/// async runtime never blocks on the C++ engine. Redirects are handled by the
/// caller before the pool; file downloads are handled here, ahead of the IIIF
/// JSON/image kinds.
fn dispatch_engine(
    state: &AppState,
    parsed: &ParsedRequest,
    method: &Method,
    uri: &Uri,
    headers: &HeaderMap,
    outcome_tx: oneshot::Sender<Outcome>,
    body_tx: mpsc::Sender<axum::body::Bytes>,
) {
    // A coarse engine span under the request span. Low-cardinality name; the
    // identifier rides as an attribute (never in the name). Its trace
    // context stamps the C++ engine's log lines (the child span_id, so engine
    // logs nest under this step), cleared when the scope ends so a reused
    // blocking thread can't leak a stale id to the next request.
    let span = tracing::info_span!(
        "sipi.serve",
        kind = ?parsed.kind,
        identifier = %parsed.identifier,
        otel.status_code = tracing::field::Empty,
        error.type = tracing::field::Empty,
    );
    let _enter = span.enter();
    let _trace =
        crate::telemetry::current_trace_context().map(|(t, s)| ffi::LogTraceScope::set(&t, &s));

    if parsed.kind == RequestKind::FileDownload {
        match file_access(state, parsed, method, uri, headers)
            .and_then(|access| resolve(&access.infile, &state.resolved_imgroot))
        {
            Ok(resolved) => serve_file(&resolved, headers, outcome_tx, body_tx),
            Err(resp) => complete(outcome_tx, resp),
        }
        return;
    }

    // IIIF image / info.json / knora.json: resolve infile + permission via the
    // preflight hook (or a default path when no hook is defined).
    let access = match iiif_access(state, parsed, method, uri, headers) {
        Ok(a) => a,
        Err(resp) => return complete(outcome_tx, resp),
    };

    // The IIIF image serve enforces auth itself (401 for any non-allow/restrict);
    // info.json renders the auth-service block at 401; knora.json does not gate
    // (it relies on the file being accessible) — matching the C++ handlers.
    if parsed.kind == RequestKind::Iiif
        && !matches!(
            access.permission,
            SipiPermType::Allow | SipiPermType::Restrict
        )
    {
        return complete(outcome_tx, sink::error_response(StatusCode::UNAUTHORIZED));
    }

    let resolved = match resolve(&access.infile, &state.resolved_imgroot) {
        Ok(r) => r,
        Err(resp) => return complete(outcome_tx, resp),
    };

    match parsed.kind {
        RequestKind::Iiif => {
            serve_image(
                &resolved,
                parsed,
                headers,
                *method == Method::HEAD,
                &access,
                outcome_tx,
                body_tx,
            );
        }
        RequestKind::InfoJson => complete(
            outcome_tx,
            serve_info_json(&resolved, parsed, headers, &access),
        ),
        RequestKind::KnoraJson => {
            complete(outcome_tx, serve_knora_json(&resolved, parsed, headers))
        }
        RequestKind::Redirect | RequestKind::FileDownload => {
            unreachable!("redirect handled by caller, file above")
        }
    }
}

/// Deliver a complete (non-streaming) response — JSON, redirect, or an error —
/// on the outcome channel, consuming the sender. On a 4xx/5xx, mark the engine
/// span errored so Grafana can correlate failures (the outer OtelAxumLayer span
/// carries the HTTP status separately; `error.type` carries the status code).
fn complete(outcome_tx: oneshot::Sender<Outcome>, response: Response) {
    let status = response.status();
    if status.is_client_error() || status.is_server_error() {
        let span = tracing::Span::current();
        span.record("otel.status_code", "ERROR");
        span.record("error.type", status.as_str());
    }
    let _ = outcome_tx.send(Outcome::Complete(response));
}

/// Pool-full backpressure: a bare 503 with `Retry-After: 1` — no body,
/// no internal detail. The client should retry shortly.
fn busy_response() -> Response {
    let mut response = sink::error_response(StatusCode::SERVICE_UNAVAILABLE);
    response
        .headers_mut()
        .insert(header::RETRY_AFTER, HeaderValue::from_static("1"));
    response
}

// ── Access resolution (preflight) ───────────────────────────────────────────

/// Resolve the infile + permission for an IIIF / info / knora request: run the
/// `pre_flight` hook when one is defined (it returns the infile), else build the
/// default `imgroot/prefix/identifier` path with `allow`
/// (`SipiHttpServer.cpp:465-478`).
fn iiif_access(
    state: &AppState,
    parsed: &ParsedRequest,
    method: &Method,
    uri: &Uri,
    headers: &HeaderMap,
) -> Result<Access, Response> {
    if state.has_preflight {
        let ctx = build_ctx(method, uri, headers)
            .ok_or_else(|| sink::error_response(StatusCode::BAD_REQUEST))?;
        let outcome = ffi::preflight(&parsed.prefix, &parsed.identifier, &ctx)
            .map_err(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR))?;
        Ok(access_from(outcome))
    } else {
        Ok(Access {
            infile: path::build_request_path(
                &state.imgroot,
                &parsed.prefix,
                &parsed.identifier,
                state.prefix_as_path,
            ),
            permission: SipiPermType::Allow,
            kv: Vec::new(),
        })
    }
}

/// Resolve the infile + permission for a `/file` download: build the path, then
/// run `file_pre_flight` on it when defined (`SipiHttpServer.cpp:1078-1100`).
/// A non-allow/restrict permission is rejected here (401).
fn file_access(
    state: &AppState,
    parsed: &ParsedRequest,
    method: &Method,
    uri: &Uri,
    headers: &HeaderMap,
) -> Result<Access, Response> {
    let built = path::build_request_path(
        &state.imgroot,
        &parsed.prefix,
        &parsed.identifier,
        state.prefix_as_path,
    );
    if !state.has_file_preflight {
        return Ok(Access {
            infile: built,
            permission: SipiPermType::Allow,
            kv: Vec::new(),
        });
    }
    let ctx = build_ctx(method, uri, headers)
        .ok_or_else(|| sink::error_response(StatusCode::BAD_REQUEST))?;
    let outcome = ffi::file_preflight(&built, &ctx)
        .map_err(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR))?;
    match outcome.permission {
        SipiPermType::Allow | SipiPermType::Restrict => Ok(access_from(outcome)),
        _ => Err(sink::error_response(StatusCode::UNAUTHORIZED)),
    }
}

/// Fold a [`PreflightOutcome`] into an [`Access`], taking the hook's `infile`
/// (empty for `deny`, which then fails R2 → 404, matching the C++ `access()`).
fn access_from(outcome: PreflightOutcome) -> Access {
    let infile = outcome.get("infile").unwrap_or_default().to_owned();
    Access {
        infile,
        permission: outcome.permission,
        kv: outcome.kv,
    }
}

/// R2: realpath + image-root containment. Maps to a bare error response.
fn resolve(infile: &str, resolved_root: &str) -> Result<String, Response> {
    match path::validate_resolved_path(infile, resolved_root) {
        Resolved::Ok(p) => Ok(p),
        Resolved::NotFound => Err(sink::error_response(StatusCode::NOT_FOUND)),
        Resolved::Traversal => Err(sink::error_response(StatusCode::BAD_REQUEST)),
    }
}

// ── Serve handlers ──────────────────────────────────────────────────────────

/// IIIF image via `sipi_serve_image`, honouring a `restrict` decision's
/// `size`/`watermark` from the preflight kv channel.
fn serve_image(
    resolved: &str,
    parsed: &ParsedRequest,
    headers: &HeaderMap,
    is_head: bool,
    access: &Access,
    outcome_tx: oneshot::Sender<Outcome>,
    body_tx: mpsc::Sender<axum::body::Bytes>,
) {
    let params = parsed
        .params
        .expect("an Iiif request always carries parsed params");
    let (scheme, host) = forwarded(headers);

    // Every C string must outlive the synchronous sipi_serve_image call.
    let (c_resolved, c_prefix, c_identifier) = match (
        CString::new(resolved),
        CString::new(parsed.prefix.as_str()),
        CString::new(parsed.identifier.as_str()),
    ) {
        (Ok(a), Ok(b), Ok(c)) => (a, b, c),
        _ => return complete(outcome_tx, sink::error_response(StatusCode::BAD_REQUEST)),
    };
    let c_client_ip = CString::new(client_ip(headers)).unwrap_or_default();
    let c_host = CString::new(host).unwrap_or_default();
    let c_scheme = CString::new(scheme).unwrap_or_default();
    let c_uri = CString::new(parsed_request_uri(parsed)).unwrap_or_default();
    // restrict size + watermark from the preflight kv (NULL when not restricted).
    let c_size = (access.permission == SipiPermType::Restrict)
        .then(|| access_kv_cstring(access, "size"))
        .flatten();
    let c_watermark = (access.permission == SipiPermType::Restrict)
        .then(|| access_kv_cstring(access, "watermark"))
        .flatten();

    let req = SipiServeRequest {
        resolved_path: c_resolved.as_ptr(),
        prefix: c_prefix.as_ptr(),
        identifier: c_identifier.as_ptr(),
        client_ip: c_client_ip.as_ptr(),
        params,
        restricted_size: c_size.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
        watermark_path: c_watermark
            .as_ref()
            .map_or(std::ptr::null(), |c| c.as_ptr()),
        // The engine honours X-Forwarded-Proto for the canonical Link header
        // (SIPI serves plain HTTP behind Traefik); the cache key stays scheme-free.
        forwarded_proto: c_scheme.as_ptr(),
        forwarded_host: c_host.as_ptr(),
        request_uri: c_uri.as_ptr(),
        is_head: i32::from(is_head),
    };

    // SAFETY: every pointer in `req` outlives this synchronous call; the seam
    // guards C++ exceptions (→ status code, never an unwind into Rust). The
    // streamed head's CORS header is added by the caller (`iiif`).
    sink::serve_streaming(outcome_tx, body_tx, |resp: &SipiResponse| unsafe {
        ffi::sipi_serve_image(&req, resp)
    });
}

/// Raw `/file` passthrough via `sipi_serve_file` (Range/206). The CORS echo and,
/// on a Range request, the identifier-derived Content-Disposition are added to
/// the streamed head by the caller (`iiif`, `SipiHttpServer.cpp:1120-1129`).
fn serve_file(
    resolved: &str,
    headers: &HeaderMap,
    outcome_tx: oneshot::Sender<Outcome>,
    body_tx: mpsc::Sender<axum::body::Bytes>,
) {
    let c_resolved = match CString::new(resolved) {
        Ok(c) => c,
        Err(_) => return complete(outcome_tx, sink::error_response(StatusCode::BAD_REQUEST)),
    };
    let range = header_str(headers, header::RANGE.as_str());
    let c_range = range.as_deref().and_then(|r| CString::new(r).ok());
    let range_ptr = c_range.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());

    // SAFETY: `c_resolved`/`c_range` outlive the synchronous call; the seam guards.
    sink::serve_streaming(outcome_tx, body_tx, |resp: &SipiResponse| unsafe {
        ffi::sipi_serve_file(c_resolved.as_ptr(), range_ptr, resp)
    });
}

/// CORS preflight (`OPTIONS`): echo the Origin (no credentials),
/// advertise the served methods, and echo the requested headers
/// (`SipiHttpServer`/`Connection.cpp:411-416`, minus the credential reflection).
/// Engine-independent, so it serves without the readiness gate.
pub async fn cors_preflight(headers: HeaderMap) -> Response {
    let mut builder = Response::builder()
        .status(StatusCode::NO_CONTENT)
        .header(header::ACCESS_CONTROL_ALLOW_METHODS, "GET, HEAD, OPTIONS");
    if let Some(origin) =
        header_str(&headers, "origin").and_then(|o| HeaderValue::from_str(&o).ok())
    {
        builder = builder.header(header::ACCESS_CONTROL_ALLOW_ORIGIN, origin);
    }
    if let Some(req_headers) = header_str(&headers, "access-control-request-headers")
        .and_then(|h| HeaderValue::from_str(&h).ok())
    {
        builder = builder.header(header::ACCESS_CONTROL_ALLOW_HEADERS, req_headers);
    }
    builder
        .body(Body::empty())
        .unwrap_or_else(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR))
}

// ── Configured Lua routes ────────────────────────────────────────────────────

/// One parsed multipart file upload, spooled to a temp file (`server.uploads`).
/// The `NamedTempFile` handle is held alongside until the route call returns, so
/// the on-disk file the engine opens by `tmpname` survives the call and is then
/// auto-deleted.
struct UploadPart {
    fieldname: String,
    origname: String,
    tmpname: String,
    mime: String,
    size: u64,
}

/// Map an HTTP method name to the axum `MethodFilter` (the config's verbs).
fn method_filter(method: &str) -> Option<MethodFilter> {
    match method {
        "GET" => Some(MethodFilter::GET),
        "HEAD" => Some(MethodFilter::HEAD),
        "POST" => Some(MethodFilter::POST),
        "PUT" => Some(MethodFilter::PUT),
        "DELETE" => Some(MethodFilter::DELETE),
        "OPTIONS" => Some(MethodFilter::OPTIONS),
        _ => None,
    }
}

/// Build the axum `MethodRouter` for one configured Lua route: the route's method
/// dispatches to [`serve_lua_route`] with the route's script captured, capped at
/// the configured POST size (the transport's `Connection` body limit). Returns
/// `None` for an unsupported method verb.
#[must_use]
pub fn lua_route_method_router(
    state: Arc<AppState>,
    entry: &ffi::RouteEntry,
) -> Option<MethodRouter<Arc<AppState>>> {
    let filter = method_filter(&entry.method)?;
    let script = entry.script.clone();
    let max_post = state.max_post_size;
    let handler = on(filter, move |req: Request| {
        let state = Arc::clone(&state);
        let script = script.clone();
        async move { serve_lua_route(state, script, req).await }
    });
    // Cap the request body at max_post_size (oversized → 413), or lift axum's
    // default 2 MiB cap when the config leaves it unlimited.
    Some(if max_post > 0 {
        handler.layer(DefaultBodyLimit::max(max_post))
    } else {
        handler.layer(DefaultBodyLimit::disable())
    })
}

/// Serve a configured Lua route: snapshot the request, spool any multipart
/// uploads to temp files, build the request context, and run the route's script
/// through `sipi_run_lua_route` on the blocking pool. The route's own Lua sets
/// the response status/headers/body (no shell-injected CORS — unlike the IIIF
/// paths, the script owns its headers).
async fn serve_lua_route(state: Arc<AppState>, script: String, req: Request) -> Response {
    let method = req.method().clone();
    let uri = req.uri().clone();
    let headers = req.headers().clone();
    let content_type = header_str(&headers, header::CONTENT_TYPE.as_str()).unwrap_or_default();

    // Request fields, owned so they can move onto the blocking thread.
    let (_scheme, host) = forwarded(&headers);
    let client_ip = client_ip(&headers);
    let header_vec: Vec<(String, String)> = headers
        .iter()
        .filter_map(|(n, v)| {
            v.to_str()
                .ok()
                .map(|v| (n.as_str().to_owned(), v.to_owned()))
        })
        .collect();
    let cookies = parse_cookies(&headers);
    let get_params = uri.query().map(parse_form_encoded).unwrap_or_default();

    // Body: multipart → spooled uploads; urlencoded → POST params; else raw bytes.
    // The DefaultBodyLimit layer caps multipart; to_bytes caps the raw path.
    let mut uploads: Vec<UploadPart> = Vec::new();
    let mut tempfiles: Vec<NamedTempFile> = Vec::new();
    let mut post_params: Vec<(String, String)> = Vec::new();
    let mut body: Vec<u8> = Vec::new();

    if content_type.starts_with("multipart/form-data") {
        let mut multipart = match Multipart::from_request(req, &()).await {
            Ok(m) => m,
            // 413 (over the body cap) or 400 (malformed) — the rejection's own status.
            Err(rej) => return rej.into_response(),
        };
        loop {
            match multipart.next_field().await {
                Ok(Some(mut field)) => {
                    let fieldname = field.name().unwrap_or_default().to_owned();
                    let origname = field.file_name().unwrap_or_default().to_owned();
                    let mime = field.content_type().unwrap_or_default().to_owned();
                    if origname.is_empty() {
                        // A non-file field is a POST form parameter (server.post) —
                        // small, so buffer it in memory.
                        let data = match field.bytes().await {
                            Ok(b) => b,
                            Err(rej) => return rej.into_response(),
                        };
                        post_params.push((fieldname, String::from_utf8_lossy(&data).into_owned()));
                    } else {
                        // A file part: stream chunks straight to the temp file so a
                        // single upload never buffers fully in memory. The whole-request
                        // size is still bounded by the DefaultBodyLimit layer when
                        // max_post_size > 0 (unlimited matches the C++ transport, which
                        // also spools uploads to disk rather than holding them in RAM).
                        let Ok(mut tf) = NamedTempFile::new() else {
                            return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR);
                        };
                        let mut size: u64 = 0;
                        loop {
                            match field.chunk().await {
                                Ok(Some(chunk)) => {
                                    if tf.write_all(&chunk).is_err() {
                                        return sink::error_response(
                                            StatusCode::INTERNAL_SERVER_ERROR,
                                        );
                                    }
                                    size += chunk.len() as u64;
                                }
                                Ok(None) => break,
                                Err(rej) => return rej.into_response(),
                            }
                        }
                        if tf.flush().is_err() {
                            return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR);
                        }
                        let tmpname = tf.path().to_string_lossy().into_owned();
                        uploads.push(UploadPart {
                            fieldname,
                            origname,
                            tmpname,
                            mime,
                            size,
                        });
                        tempfiles.push(tf);
                    }
                }
                Ok(None) => break,
                Err(rej) => return rej.into_response(),
            }
        }
    } else if has_request_body(&method) {
        let limit = if state.max_post_size == 0 {
            usize::MAX
        } else {
            state.max_post_size
        };
        let bytes = match to_bytes(req.into_body(), limit).await {
            Ok(b) => b,
            Err(_) => return sink::error_response(StatusCode::PAYLOAD_TOO_LARGE),
        };
        if content_type.starts_with("application/x-www-form-urlencoded") {
            post_params = parse_form_encoded(std::str::from_utf8(&bytes).unwrap_or_default());
        }
        body = bytes.to_vec();
    }

    // Bound concurrency on the engine pool, then run the (blocking) Lua VM off the
    // async runtime. A full pool sheds load with 503 + Retry-After.
    let permit = match Arc::clone(&state.pool).try_acquire_owned() {
        Ok(permit) => permit,
        Err(_) => return busy_response(),
    };
    let (outcome_tx, outcome_rx) = oneshot::channel::<Outcome>();
    let (body_tx, body_rx) = mpsc::channel::<axum::body::Bytes>(sink::BODY_CHANNEL_CAP);
    let request_span = tracing::Span::current();
    let method_str = method.as_str().to_owned();
    let uri_path = uri.path().to_owned();
    tokio::task::spawn_blocking(move || {
        let _permit = permit;
        let _entered = request_span.enter();
        // Temp files outlive the route call: the engine opens them by path during
        // executeChunk, and they are deleted when this Vec drops (after the call).
        let _tempfiles = tempfiles;
        run_lua_route_blocking(
            &script,
            LuaRequest {
                method: &method_str,
                client_ip: &client_ip,
                host: &host,
                uri_path: &uri_path,
                headers: &header_vec,
                cookies: &cookies,
                get_params: &get_params,
                post_params: &post_params,
                content_type: &content_type,
                body: &body,
                uploads: &uploads,
            },
            outcome_tx,
            body_tx,
        );
    });
    match outcome_rx.await {
        Ok(Outcome::Complete(response)) => response,
        Ok(Outcome::StreamHead {
            status,
            headers: head,
        }) => sink::stream_response(status, head, body_rx),
        Err(_) => sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
    }
}

/// The owned, borrow-only view of a request handed to the blocking Lua dispatch.
struct LuaRequest<'a> {
    method: &'a str,
    client_ip: &'a str,
    host: &'a str,
    uri_path: &'a str,
    headers: &'a [(String, String)],
    cookies: &'a [(String, String)],
    get_params: &'a [(String, String)],
    post_params: &'a [(String, String)],
    content_type: &'a str,
    body: &'a [u8],
    uploads: &'a [UploadPart],
}

/// Build the request context (full request: body, uploads, params) and run the
/// route's script through `sipi_run_lua_route` against the streaming sink. Runs
/// on a `spawn_blocking` thread (the Lua VM + any decode/encode is blocking).
fn run_lua_route_blocking(
    script: &str,
    req: LuaRequest,
    outcome_tx: oneshot::Sender<Outcome>,
    body_tx: mpsc::Sender<axum::body::Bytes>,
) {
    // Log only the basename — the full resolved path leaks the server's
    // filesystem layout to the OTel backend, and the basename identifies the route.
    let script_name = std::path::Path::new(script)
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or(script);
    let span = tracing::info_span!("sipi.lua_route", route_script = %script_name);
    let _enter = span.enter();
    let _trace =
        crate::telemetry::current_trace_context().map(|(t, s)| ffi::LogTraceScope::set(&t, &s));

    // secure = false: SIPI runs plain HTTP behind Traefik (matches conn.secure()).
    let Some(ctx) = ffi::build_request_context(
        req.method,
        req.client_ip,
        0,
        false,
        req.host,
        req.uri_path,
        req.headers,
        req.cookies,
    ) else {
        return complete(outcome_tx, sink::error_response(StatusCode::BAD_REQUEST));
    };
    if !req.body.is_empty() || !req.content_type.is_empty() {
        ctx.set_body(req.content_type, req.body);
    }
    for (k, v) in req.get_params {
        ctx.add_param(0, k, v);
    }
    for (k, v) in req.post_params {
        ctx.add_param(1, k, v);
    }
    for u in req.uploads {
        ctx.add_upload(&u.fieldname, &u.origname, &u.tmpname, &u.mime, u.size);
    }

    let Ok(c_script) = CString::new(script) else {
        return complete(
            outcome_tx,
            sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
        );
    };
    // SAFETY: `c_script` + `ctx` outlive the synchronous call; the seam guards C++
    // exceptions (→ status code, never an unwind into Rust).
    sink::serve_streaming(outcome_tx, body_tx, |resp: &SipiResponse| unsafe {
        ffi::sipi_run_lua_route(c_script.as_ptr(), ctx.as_ptr(), resp)
    });
}

/// Whether an HTTP method carries a request body the Lua route should read.
fn has_request_body(method: &Method) -> bool {
    matches!(
        *method,
        Method::POST | Method::PUT | Method::PATCH | Method::DELETE
    )
}

/// Parse `application/x-www-form-urlencoded` (or a query string) into name/value
/// pairs via the `form_urlencoded` crate (WHATWG semantics: `&`-split, `=`-split,
/// `+` → space, percent-decode; empty pairs skipped).
///
/// DIFFERENTIAL-VERIFY: this feeds the Lua `server.get`/`server.post` bindings,
/// so its output must match the C++ shttps query/POST parser. Confirm parity via
/// the differential harness once it lands (plan 02 §7, step 4).
fn parse_form_encoded(s: &str) -> Vec<(String, String)> {
    form_urlencoded::parse(s.as_bytes()).into_owned().collect()
}

fn serve_info_json(
    resolved: &str,
    parsed: &ParsedRequest,
    headers: &HeaderMap,
    access: &Access,
) -> Response {
    let (scheme, host) = forwarded(headers);
    let id = canonical_id(&scheme, &host, &parsed.prefix, &parsed.identifier);

    let mime = match ffi::mimetype(resolved) {
        Ok(m) => m,
        Err(_) => return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
    };

    let (mut value, link_context) = if IMAGE_MIMES.contains(&mime.as_str()) {
        let dims = match ffi::image_dims(resolved) {
            Ok(d) => d,
            Err(_) => return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
        };
        (info::image_info_json(&id, &dims), info::image_context())
    } else {
        let size = match std::fs::metadata(resolved) {
            Ok(m) => m.len(),
            Err(_) => return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
        };
        (info::file_info_json(&id, &mime, size), info::file_context())
    };

    // An auth-type permission adds the IIIF Auth service block at status 401.
    let status = if info::is_auth_type(access.permission) {
        match info::auth_service(access.permission, &access.kv) {
            Ok(service) => {
                if let Some(obj) = value.as_object_mut() {
                    obj.insert("service".into(), serde_json::json!([service]));
                }
                StatusCode::UNAUTHORIZED
            }
            // Missing cookieUrl/tokenUrl in the hook result → 500 (parity).
            Err(()) => return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
        }
    } else {
        StatusCode::OK
    };

    // info.json always sends ACAO: * (SipiHttpServer.cpp:768), even with an Origin.
    json_response(status, &value, headers, Some(link_context), "*")
}

fn serve_knora_json(resolved: &str, parsed: &ParsedRequest, headers: &HeaderMap) -> Response {
    let (scheme, host) = forwarded(headers);
    let id = canonical_id(&scheme, &host, &parsed.prefix, &parsed.identifier);

    let mime = match ffi::mimetype(resolved) {
        Ok(m) => m,
        Err(_) => return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
    };
    let sidecar = read_sidecar(resolved);

    let value = if IMAGE_MIMES.contains(&mime.as_str()) {
        match ffi::image_dims(resolved) {
            Ok(dims) => info::image_knora_json(&id, &mime, &dims, &sidecar),
            Err(_) => return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
        }
    } else {
        let size = match std::fs::metadata(resolved) {
            Ok(m) => m.len(),
            Err(_) => return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
        };
        if mime == "video/mp4" {
            info::video_knora_json(&id, &mime, size, &sidecar)
        } else {
            info::generic_knora_json(&id, &mime, size, &sidecar)
        }
    };

    // knora.json echoes the Origin when present, else * (SipiHttpServer.cpp:815-821).
    let acao = header_str(headers, "origin").unwrap_or_else(|| "*".to_owned());
    json_response(StatusCode::OK, &value, headers, None, &acao)
}

/// 303 redirect from a bare identifier to its canonical info.json
/// (`SipiHttpServer.cpp:506-531`).
fn redirect(headers: &HeaderMap, parsed: &ParsedRequest) -> Response {
    let (scheme, host) = forwarded(headers);
    let target = if parsed.prefix.is_empty() {
        format!("{scheme}://{host}/{}/info.json", parsed.identifier)
    } else {
        format!(
            "{scheme}://{host}/{}/{}/info.json",
            parsed.prefix, parsed.identifier
        )
    };
    match HeaderValue::from_str(&target) {
        Ok(location) => {
            let mut response = Response::builder()
                .status(StatusCode::SEE_OTHER)
                .body(Body::from(format!("Redirect to {target}")))
                .unwrap_or_else(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR));
            response.headers_mut().insert(header::LOCATION, location);
            response
                .headers_mut()
                .insert(header::CONTENT_TYPE, HeaderValue::from_static("text/plain"));
            response
        }
        // A Location with control chars (CRLF injection) is rejected, not emitted.
        Err(_) => sink::error_response(StatusCode::BAD_REQUEST),
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────

/// Build the preflight request context from the axum request: all headers
/// (lowercased C-side), the parsed cookies, host, and client IP. `secure` is
/// false (SIPI runs plain HTTP behind Traefik, matching the C++ `conn.secure()`).
fn build_ctx(method: &Method, uri: &Uri, headers: &HeaderMap) -> Option<ffi::RequestContext> {
    let header_vec: Vec<(String, String)> = headers
        .iter()
        .filter_map(|(n, v)| {
            v.to_str()
                .ok()
                .map(|v| (n.as_str().to_owned(), v.to_owned()))
        })
        .collect();
    let cookies = parse_cookies(headers);
    let (_scheme, host) = forwarded(headers);
    ffi::build_request_context(
        method.as_str(),
        &client_ip(headers),
        0,
        false,
        &host,
        uri.path(),
        &header_vec,
        &cookies,
    )
}

/// Parse the `Cookie` header into name/value pairs (the parsed map the Lua
/// `server.cookies` binding reads) via the `cookie` crate's RFC 6265 splitter.
///
/// DIFFERENTIAL-VERIFY: this feeds the Lua `server.cookies` binding, so its
/// output must match the C++ shttps cookie parser. Confirm parity via the
/// differential harness once it lands (plan 02 §7, step 4).
fn parse_cookies(headers: &HeaderMap) -> Vec<(String, String)> {
    let Some(raw) = header_str(headers, header::COOKIE.as_str()) else {
        return Vec::new();
    };
    cookie::Cookie::split_parse(raw.as_str())
        .filter_map(Result::ok)
        .map(|c| (c.name().to_owned(), c.value().to_owned()))
        .collect()
}

fn access_kv_cstring(access: &Access, key: &str) -> Option<CString> {
    access
        .kv
        .iter()
        .find(|(k, _)| k == key)
        .and_then(|(_, v)| CString::new(v.as_str()).ok())
}

/// Serialise a JSON value with the IIIF headers the C++ server sets: a CORS
/// `Access-Control-Allow-Origin` (`acao`: `*` for info.json, Origin-echo-else-`*`
/// for knora.json) and either `application/ld+json` (when the client `Accept`s
/// it) or `application/json` + a `Link` to the JSON-LD context.
fn json_response(
    status: StatusCode,
    value: &serde_json::Value,
    headers: &HeaderMap,
    link_context: Option<&str>,
    acao: &str,
) -> Response {
    let body = serde_json::to_vec(value).unwrap_or_default();
    let mut builder = Response::builder().status(status);
    if let Ok(acao) = HeaderValue::from_str(acao) {
        builder = builder.header(header::ACCESS_CONTROL_ALLOW_ORIGIN, acao);
    }

    let wants_ldjson =
        header_str(headers, header::ACCEPT.as_str()).as_deref() == Some("application/ld+json");
    match (link_context, wants_ldjson) {
        (Some(ctx), true) => {
            builder = builder.header(
                header::CONTENT_TYPE,
                format!("application/ld+json;profile=\"{ctx}\""),
            );
        }
        (Some(ctx), false) => {
            builder = builder.header(header::CONTENT_TYPE, "application/json").header(
                header::LINK,
                format!("<{ctx}>; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\""),
            );
        }
        (None, _) => {
            builder = builder.header(header::CONTENT_TYPE, "application/json");
        }
    }
    builder
        .body(Body::from(body))
        .unwrap_or_else(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR))
}

/// Synthesise the request scheme + host from the forwarded headers (SIPI runs
/// plain HTTP behind Traefik, so the real scheme is in `X-Forwarded-Proto`). Host
/// falls back from `X-Forwarded-Host` to `Host`.
fn forwarded(headers: &HeaderMap) -> (String, String) {
    let scheme = if header_str(headers, "x-forwarded-proto").as_deref() == Some("https") {
        "https"
    } else {
        "http"
    }
    .to_owned();
    let host = header_str(headers, "x-forwarded-host")
        .or_else(|| header_str(headers, header::HOST.as_str()))
        .unwrap_or_default();
    (scheme, host)
}

/// The canonical service id: `scheme://host/[prefix/]identifier`.
fn canonical_id(scheme: &str, host: &str, prefix: &str, identifier: &str) -> String {
    if prefix.is_empty() {
        format!("{scheme}://{host}/{identifier}")
    } else {
        format!("{scheme}://{host}/{prefix}/{identifier}")
    }
}

/// The rate-limit client IP: the rightmost `X-Forwarded-For` value, else empty.
///
/// SECURITY: this trusts `X-Forwarded-For`, which is sound only when SIPI sits
/// behind a reverse proxy (Traefik) that overwrites the header with the real
/// peer. The shell must never be directly reachable by clients — a directly
/// connected client could forge `X-Forwarded-For` on every request to mint
/// unlimited rate-limit buckets and defeat the per-client limiter.
fn client_ip(headers: &HeaderMap) -> String {
    header_str(headers, "x-forwarded-for")
        .and_then(|v| v.rsplit(',').next().map(|s| s.trim().to_owned()))
        .unwrap_or_default()
}

fn parsed_request_uri(parsed: &ParsedRequest) -> String {
    if parsed.prefix.is_empty() {
        format!("/{}", parsed.identifier)
    } else {
        format!("/{}/{}", parsed.prefix, parsed.identifier)
    }
}

/// `inline; filename="…"` from the identifier, control-chars + quotes stripped.
fn content_disposition(identifier: &str) -> Option<HeaderValue> {
    let safe: String = identifier
        .chars()
        .filter(|c| !c.is_control() && *c != '"')
        .collect();
    HeaderValue::from_str(&format!("inline; filename=\"{safe}\"")).ok()
}

/// Read + parse the `[path-sans-ext].info` sidecar; missing/invalid → empty.
fn read_sidecar(infile: &str) -> Sidecar {
    let sidecar_path = match infile.rfind('.') {
        Some(pos) => format!("{}.info", &infile[..pos]),
        None => format!("{infile}.info"),
    };
    std::fs::read_to_string(sidecar_path)
        .map(|t| Sidecar::parse(&t))
        .unwrap_or_default()
}

/// First value of a header as an owned `String`, if present and valid UTF-8.
fn header_str(headers: &HeaderMap, name: &str) -> Option<String> {
    headers
        .get(name)
        .and_then(|v| v.to_str().ok())
        .map(str::to_owned)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn headers(pairs: &[(&str, &str)]) -> HeaderMap {
        let mut h = HeaderMap::new();
        for (k, v) in pairs {
            h.insert(
                axum::http::HeaderName::from_bytes(k.as_bytes()).unwrap(),
                HeaderValue::from_str(v).unwrap(),
            );
        }
        h
    }

    #[test]
    fn forwarded_synthesises_https_scheme() {
        let h = headers(&[("x-forwarded-proto", "https"), ("host", "iiif.example.org")]);
        assert_eq!(forwarded(&h), ("https".into(), "iiif.example.org".into()));
    }

    #[test]
    fn forwarded_defaults_http_and_prefers_forwarded_host() {
        let h = headers(&[
            ("host", "internal:1024"),
            ("x-forwarded-host", "iiif.example.org"),
        ]);
        assert_eq!(forwarded(&h), ("http".into(), "iiif.example.org".into()));
    }

    #[test]
    fn canonical_id_with_and_without_prefix() {
        assert_eq!(
            canonical_id("https", "h", "iiif/2", "a.jp2"),
            "https://h/iiif/2/a.jp2"
        );
        assert_eq!(canonical_id("http", "h", "", "a.jp2"), "http://h/a.jp2");
    }

    #[test]
    fn client_ip_takes_rightmost_xff() {
        let h = headers(&[("x-forwarded-for", "1.1.1.1, 2.2.2.2, 3.3.3.3")]);
        assert_eq!(client_ip(&h), "3.3.3.3");
        assert_eq!(client_ip(&HeaderMap::new()), "");
    }

    #[test]
    fn parse_cookies_splits_pairs() {
        let h = headers(&[("cookie", "sid=abc; theme=dark; lone")]);
        let c = parse_cookies(&h);
        assert_eq!(
            c,
            vec![
                ("sid".to_owned(), "abc".to_owned()),
                ("theme".to_owned(), "dark".to_owned())
            ]
        );
    }

    #[test]
    fn content_disposition_strips_quotes_and_controls() {
        let v = content_disposition("a\"b\nc.tif").unwrap();
        assert_eq!(v.to_str().unwrap(), "inline; filename=\"abc.tif\"");
    }

    #[test]
    fn busy_response_is_503_with_retry_after() {
        // The pool-full backpressure contract: bare 503 + Retry-After: 1.
        let resp = busy_response();
        assert_eq!(resp.status(), StatusCode::SERVICE_UNAVAILABLE);
        assert_eq!(resp.headers().get(header::RETRY_AFTER).unwrap(), "1");
    }

    #[test]
    fn default_pool_size_is_positive() {
        assert!(default_pool_size() >= 1);
    }
}
