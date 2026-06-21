//! The IIIF request router (strangler-fig Phase C).
//!
//! Replaces the C++ `iiif_handler` dispatch (`SipiHttpServer.cpp:1326`): a
//! catch-all axum handler classifies the path with [`crate::iiif`], validates it
//! at the edge with [`crate::path`], and dispatches to the engine seam
//! (`sipi_serve_image` / `sipi_serve_file`) or assembles JSON with
//! [`crate::info`]. Slice 1 wires the serve path with no auth; preflight, the
//! info.json auth block, and CORS land in Slice 2, and the blocking FFI calls
//! move onto a `spawn_blocking` pool in Slice 3.

use std::ffi::CString;
use std::sync::Arc;

use axum::body::Body;
use axum::extract::{OriginalUri, State};
use axum::http::{header, HeaderMap, HeaderValue, Method, StatusCode};
use axum::response::Response;

use crate::ffi::{self, SipiResponse, SipiServeRequest};
use crate::iiif::{self, ParsedRequest, RequestKind};
use crate::info::{self, Sidecar};
use crate::path::{self, Resolved};
use crate::sink;

/// Image MIME types that take the IIIF / image-dimension code path (the same set
/// the C++ server branches on, `SipiHttpServer.cpp:568-570,913-914`).
const IMAGE_MIMES: [&str; 5] = ["image/tiff", "image/jpeg", "image/png", "image/jpx", "image/jp2"];

/// Cached, immutable engine config read once at startup (after `sipi_init`). The
/// image root + `prefix_as_path` knob never change post-init, so the per-request
/// path never re-crosses the FFI for them.
#[derive(Clone)]
pub struct AppState {
    ready: bool,
    imgroot: String,
    resolved_imgroot: String,
    prefix_as_path: bool,
}

impl AppState {
    /// Read the cached config from the engine. Returns a not-ready state when
    /// `sipi_init` has not run (no `--config`); the serve routes then 503.
    #[must_use]
    pub fn load() -> Self {
        match (ffi::imgroot(false), ffi::imgroot(true), ffi::prefix_as_path()) {
            (Ok(imgroot), Ok(resolved_imgroot), Ok(prefix_as_path)) => {
                Self { ready: true, imgroot, resolved_imgroot, prefix_as_path }
            }
            _ => Self { ready: false, imgroot: String::new(), resolved_imgroot: String::new(), prefix_as_path: true },
        }
    }
}

/// The catch-all IIIF handler (`GET|HEAD /…`). Classifies the request, validates
/// the path, and dispatches. Never leaks an internal path in an error body
/// (DEV-6062): every failure renders a bare status via [`sink::error_response`].
pub async fn iiif(
    State(state): State<Arc<AppState>>,
    method: Method,
    OriginalUri(uri): OriginalUri,
    headers: HeaderMap,
) -> Response {
    if !state.ready {
        // Engine not installed (no --config). Only /health + /favicon serve.
        return sink::error_response(StatusCode::SERVICE_UNAVAILABLE);
    }

    let parsed = match iiif::parse_request(uri.path()) {
        Ok(p) => p,
        Err(_) => return sink::error_response(StatusCode::BAD_REQUEST),
    };

    // R1: string-level traversal check on the decoded identifier (and prefix when
    // it is a path component), before any path construction.
    if path::contains_traversal(&parsed.identifier)
        || (state.prefix_as_path && path::contains_traversal(&parsed.prefix))
    {
        tracing::warn!(identifier = %parsed.identifier, prefix = %parsed.prefix, "rejected path traversal");
        return sink::error_response(StatusCode::BAD_REQUEST);
    }

    // The redirect needs no on-disk file.
    if parsed.kind == RequestKind::Redirect {
        return redirect(&headers, &parsed);
    }

    // Build + R2-validate the on-disk path (Slice 2 inserts preflight before this,
    // which may substitute the infile).
    let built = path::build_request_path(&state.imgroot, &parsed.prefix, &parsed.identifier, state.prefix_as_path);
    let resolved = match path::validate_resolved_path(&built, &state.resolved_imgroot) {
        Resolved::Ok(p) => p,
        Resolved::NotFound => return sink::error_response(StatusCode::NOT_FOUND),
        Resolved::Traversal => return sink::error_response(StatusCode::BAD_REQUEST),
    };

    match parsed.kind {
        RequestKind::Iiif => serve_image(&resolved, &parsed, &headers, method == Method::HEAD),
        RequestKind::FileDownload => serve_file(&resolved, &parsed, &headers),
        RequestKind::InfoJson => serve_info_json(&resolved, &parsed, &headers),
        RequestKind::KnoraJson => serve_knora_json(&resolved, &parsed, &headers),
        RequestKind::Redirect => unreachable!("redirect handled above"),
    }
}

// ── Serve handlers ──────────────────────────────────────────────────────────

/// IIIF image via `sipi_serve_image`. The blocking call runs inline in Slice 1;
/// Slice 3 moves it onto a bounded `spawn_blocking` pool.
fn serve_image(resolved: &str, parsed: &ParsedRequest, headers: &HeaderMap, is_head: bool) -> Response {
    let params = parsed.params.expect("an Iiif request always carries parsed params");
    let (_scheme, host) = forwarded(headers);

    // Every C string must outlive the synchronous sipi_serve_image call.
    let (c_resolved, c_prefix, c_identifier) =
        match (CString::new(resolved), CString::new(parsed.prefix.as_str()), CString::new(parsed.identifier.as_str())) {
            (Ok(a), Ok(b), Ok(c)) => (a, b, c),
            _ => return sink::error_response(StatusCode::BAD_REQUEST),
        };
    let c_client_ip = CString::new(client_ip(headers)).unwrap_or_default();
    let c_host = CString::new(host).unwrap_or_default();
    let c_uri = CString::new(parsed_request_uri(parsed)).unwrap_or_default();

    let req = SipiServeRequest {
        resolved_path: c_resolved.as_ptr(),
        prefix: c_prefix.as_ptr(),
        identifier: c_identifier.as_ptr(),
        client_ip: c_client_ip.as_ptr(),
        params,
        restricted_size: std::ptr::null(), // Slice 2 (preflight restrict)
        watermark_path: std::ptr::null(),  // Slice 2
        // The engine hardcodes http:// in its canonical Link header today and
        // ignores forwarded_proto; the Rust-built info.json/knora.json/redirect
        // ids honour X-Forwarded-Proto. Honouring it in the engine Link header is
        // a separate engine change (follow-up).
        forwarded_proto: std::ptr::null(),
        forwarded_host: c_host.as_ptr(),
        request_uri: c_uri.as_ptr(),
        is_head: i32::from(is_head),
    };

    // SAFETY: every pointer in `req` outlives this synchronous call; the seam
    // guards C++ exceptions (→ status code, never an unwind into Rust).
    let (code, sink_state) = sink::serve_buffered(|resp: &SipiResponse| unsafe { ffi::sipi_serve_image(&req, resp) });
    finish_serve(code, sink_state)
}

/// Raw `/file` passthrough via `sipi_serve_file` (Range/206). Content-Disposition
/// is set caller-side, only on a Range request (`SipiHttpServer.cpp:64-68`).
fn serve_file(resolved: &str, parsed: &ParsedRequest, headers: &HeaderMap) -> Response {
    let c_resolved = match CString::new(resolved) {
        Ok(c) => c,
        Err(_) => return sink::error_response(StatusCode::BAD_REQUEST),
    };
    let range = header_str(headers, header::RANGE.as_str());
    let c_range = range.as_deref().and_then(|r| CString::new(r).ok());
    let range_ptr = c_range.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());

    // SAFETY: `c_resolved`/`c_range` outlive the synchronous call; the seam guards.
    let (code, sink_state) =
        sink::serve_buffered(|resp: &SipiResponse| unsafe { ffi::sipi_serve_file(c_resolved.as_ptr(), range_ptr, resp) });
    let mut response = finish_serve(code, sink_state);

    if range.is_some() && response.status().is_success() {
        if let Some(value) = content_disposition(&parsed.identifier) {
            response.headers_mut().insert(header::CONTENT_DISPOSITION, value);
        }
    }
    response
}

fn serve_info_json(resolved: &str, parsed: &ParsedRequest, headers: &HeaderMap) -> Response {
    let (scheme, host) = forwarded(headers);
    let id = canonical_id(&scheme, &host, &parsed.prefix, &parsed.identifier);

    let mime = match ffi::mimetype(resolved) {
        Ok(m) => m,
        Err(_) => return sink::error_response(StatusCode::INTERNAL_SERVER_ERROR),
    };

    let (value, link_context) = if IMAGE_MIMES.contains(&mime.as_str()) {
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

    json_response(StatusCode::OK, &value, headers, Some(link_context))
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

    json_response(StatusCode::OK, &value, headers, None)
}

/// 303 redirect from a bare identifier to its canonical info.json
/// (`SipiHttpServer.cpp:506-531`).
fn redirect(headers: &HeaderMap, parsed: &ParsedRequest) -> Response {
    let (scheme, host) = forwarded(headers);
    let target = if parsed.prefix.is_empty() {
        format!("{scheme}://{host}/{}/info.json", parsed.identifier)
    } else {
        format!("{scheme}://{host}/{}/{}/info.json", parsed.prefix, parsed.identifier)
    };
    match HeaderValue::from_str(&target) {
        Ok(location) => {
            let mut response = Response::builder()
                .status(StatusCode::SEE_OTHER)
                .body(Body::from(format!("Redirect to {target}")))
                .unwrap_or_else(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR));
            response.headers_mut().insert(header::LOCATION, location);
            response.headers_mut().insert(header::CONTENT_TYPE, HeaderValue::from_static("text/plain"));
            response
        }
        // A Location with control chars (CRLF injection) is rejected, not emitted.
        Err(_) => sink::error_response(StatusCode::BAD_REQUEST),
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────

/// Render the engine's buffered response, mapping its status code. 499
/// (client gone) means nothing was committed — render an empty 499.
fn finish_serve(code: i32, sink_state: sink::SinkState) -> Response {
    if code == 0 {
        sink::into_response(sink_state)
    } else {
        let status = StatusCode::from_u16(code as u16).unwrap_or(StatusCode::INTERNAL_SERVER_ERROR);
        sink::error_response(status)
    }
}

/// Serialise a JSON value into a response with the IIIF headers the C++ server
/// sets: `Access-Control-Allow-Origin: *` and either an `application/ld+json`
/// content type (when the client `Accept`s it) or `application/json` + a `Link`
/// to the JSON-LD context (`SipiHttpServer.cpp:768-788`).
fn json_response(status: StatusCode, value: &serde_json::Value, headers: &HeaderMap, link_context: Option<&str>) -> Response {
    let body = serde_json::to_vec(value).unwrap_or_default();
    let mut builder = Response::builder().status(status).header(header::ACCESS_CONTROL_ALLOW_ORIGIN, "*");

    let wants_ldjson = header_str(headers, header::ACCEPT.as_str()).as_deref() == Some("application/ld+json");
    match (link_context, wants_ldjson) {
        (Some(ctx), true) => {
            builder = builder.header(header::CONTENT_TYPE, format!("application/ld+json;profile=\"{ctx}\""));
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
    builder.body(Body::from(body)).unwrap_or_else(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR))
}

/// Synthesise the request scheme + host from the forwarded headers (decision:
/// SIPI runs plain HTTP behind Traefik, so the real scheme is in
/// `X-Forwarded-Proto`). Host falls back from `X-Forwarded-Host` to `Host`.
fn forwarded(headers: &HeaderMap) -> (String, String) {
    let scheme =
        if header_str(headers, "x-forwarded-proto").as_deref() == Some("https") { "https" } else { "http" }.to_owned();
    let host = header_str(headers, "x-forwarded-host")
        .or_else(|| header_str(headers, header::HOST.as_str()))
        .unwrap_or_default();
    (scheme, host)
}

/// The canonical service id: `scheme://host/[prefix/]identifier`
/// (`SipiHttpServer.cpp:585-589`).
fn canonical_id(scheme: &str, host: &str, prefix: &str, identifier: &str) -> String {
    if prefix.is_empty() {
        format!("{scheme}://{host}/{identifier}")
    } else {
        format!("{scheme}://{host}/{prefix}/{identifier}")
    }
}

/// The rate-limit client IP: the rightmost `X-Forwarded-For` value (the hop
/// Traefik appends), else empty (the engine treats empty as "no XFF").
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

/// `inline; filename="…"` from the identifier, control-chars stripped (R8/R9
/// header sanitisation). `None` when the value is unrepresentable.
fn content_disposition(identifier: &str) -> Option<HeaderValue> {
    let safe: String = identifier.chars().filter(|c| !c.is_control() && *c != '"').collect();
    HeaderValue::from_str(&format!("inline; filename=\"{safe}\"")).ok()
}

/// Read + parse the `[path-sans-ext].info` sidecar; missing/invalid → empty.
fn read_sidecar(infile: &str) -> Sidecar {
    let sidecar_path = match infile.rfind('.') {
        Some(pos) => format!("{}.info", &infile[..pos]),
        None => format!("{infile}.info"),
    };
    std::fs::read_to_string(sidecar_path).map(|t| Sidecar::parse(&t)).unwrap_or_default()
}

/// First value of a header as an owned `String`, if present and valid UTF-8.
fn header_str(headers: &HeaderMap, name: &str) -> Option<String> {
    headers.get(name).and_then(|v| v.to_str().ok()).map(str::to_owned)
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
        let h = headers(&[("host", "internal:1024"), ("x-forwarded-host", "iiif.example.org")]);
        assert_eq!(forwarded(&h), ("http".into(), "iiif.example.org".into()));
    }

    #[test]
    fn canonical_id_with_and_without_prefix() {
        assert_eq!(canonical_id("https", "h", "iiif/2", "a.jp2"), "https://h/iiif/2/a.jp2");
        assert_eq!(canonical_id("http", "h", "", "a.jp2"), "http://h/a.jp2");
    }

    #[test]
    fn client_ip_takes_rightmost_xff() {
        let h = headers(&[("x-forwarded-for", "1.1.1.1, 2.2.2.2, 3.3.3.3")]);
        assert_eq!(client_ip(&h), "3.3.3.3");
        assert_eq!(client_ip(&HeaderMap::new()), "");
    }

    #[test]
    fn content_disposition_strips_quotes_and_controls() {
        let v = content_disposition("a\"b\nc.tif").unwrap();
        assert_eq!(v.to_str().unwrap(), "inline; filename=\"abc.tif\"");
    }
}
