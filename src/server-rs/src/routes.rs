//! The IIIF request router (strangler-fig Phase C).
//!
//! Replaces the C++ `iiif_handler` dispatch (`SipiHttpServer.cpp:1326`): a
//! catch-all axum handler classifies the path with [`crate::iiif`], validates it
//! at the edge with [`crate::path`], runs the Lua preflight hook (auth + path
//! resolution) through the seam, and dispatches to the engine
//! (`sipi_serve_image` / `sipi_serve_file`) or assembles JSON with
//! [`crate::info`]. The blocking FFI calls move onto a `spawn_blocking` pool in
//! Slice 3.

use std::ffi::CString;
use std::sync::Arc;

use axum::body::Body;
use axum::extract::{OriginalUri, State};
use axum::http::{header, HeaderMap, HeaderValue, Method, StatusCode, Uri};
use axum::response::Response;

use crate::ffi::{self, PreflightOutcome, SipiPermType, SipiResponse, SipiServeRequest};
use crate::iiif::{self, ParsedRequest, RequestKind};
use crate::info::{self, Sidecar};
use crate::path::{self, Resolved};
use crate::sink;

/// Image MIME types that take the IIIF / image-dimension code path (the same set
/// the C++ server branches on, `SipiHttpServer.cpp:568-570,913-914`).
const IMAGE_MIMES: [&str; 5] = ["image/tiff", "image/jpeg", "image/png", "image/jpx", "image/jp2"];

/// Cached, immutable engine config read once at startup (after `sipi_init`).
#[derive(Clone)]
pub struct AppState {
    ready: bool,
    imgroot: String,
    resolved_imgroot: String,
    prefix_as_path: bool,
    has_preflight: bool,
    has_file_preflight: bool,
}

impl AppState {
    /// Read the cached config from the engine. Returns a not-ready state when
    /// `sipi_init` has not run (no `--config`); the serve routes then 503.
    #[must_use]
    pub fn load() -> Self {
        match (ffi::imgroot(false), ffi::imgroot(true), ffi::prefix_as_path()) {
            (Ok(imgroot), Ok(resolved_imgroot), Ok(prefix_as_path)) => Self {
                ready: true,
                imgroot,
                resolved_imgroot,
                prefix_as_path,
                // A failed hook probe (engine error) disables preflight rather
                // than failing startup — the same effect as "no hook defined".
                has_preflight: ffi::has_preflight().unwrap_or(false),
                has_file_preflight: ffi::has_file_preflight().unwrap_or(false),
            },
            _ => Self {
                ready: false,
                imgroot: String::new(),
                resolved_imgroot: String::new(),
                prefix_as_path: true,
                has_preflight: false,
                has_file_preflight: false,
            },
        }
    }
}

/// The resolved access decision: the on-disk file the hook chose, the permission,
/// and the open kv channel (restrict `size`/`watermark`, auth-service urls).
struct Access {
    infile: String,
    permission: SipiPermType,
    kv: Vec<(String, String)>,
}

/// The catch-all IIIF handler (`GET|HEAD /…`). Classifies, validates, runs
/// preflight, and dispatches. Never leaks an internal path in an error body
/// (DEV-6062): every failure renders a bare status via [`sink::error_response`].
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

    if parsed.kind == RequestKind::Redirect {
        return redirect(&headers, &parsed);
    }

    if parsed.kind == RequestKind::FileDownload {
        let access = match file_access(&state, &parsed, &method, &uri, &headers) {
            Ok(a) => a,
            Err(resp) => return resp,
        };
        return match resolve(&access.infile, &state.resolved_imgroot) {
            Ok(resolved) => serve_file(&resolved, &parsed, &headers),
            Err(resp) => resp,
        };
    }

    // IIIF image / info.json / knora.json: resolve infile + permission via the
    // preflight hook (or a default path when no hook is defined).
    let access = match iiif_access(&state, &parsed, &method, &uri, &headers) {
        Ok(a) => a,
        Err(resp) => return resp,
    };

    // The IIIF image serve enforces auth itself (401 for any non-allow/restrict);
    // info.json renders the auth-service block at 401; knora.json does not gate
    // (it relies on the file being accessible) — matching the C++ handlers.
    if parsed.kind == RequestKind::Iiif
        && !matches!(access.permission, SipiPermType::Allow | SipiPermType::Restrict)
    {
        return sink::error_response(StatusCode::UNAUTHORIZED);
    }

    let resolved = match resolve(&access.infile, &state.resolved_imgroot) {
        Ok(r) => r,
        Err(resp) => return resp,
    };

    match parsed.kind {
        RequestKind::Iiif => serve_image(&resolved, &parsed, &headers, method == Method::HEAD, &access),
        RequestKind::InfoJson => serve_info_json(&resolved, &parsed, &headers, &access),
        RequestKind::KnoraJson => serve_knora_json(&resolved, &parsed, &headers),
        RequestKind::Redirect | RequestKind::FileDownload => unreachable!("handled above"),
    }
}

// ── Access resolution (preflight) ───────────────────────────────────────────

/// Resolve the infile + permission for an IIIF / info / knora request: run the
/// `pre_flight` hook when one is defined (it returns the infile), else build the
/// default `imgroot/prefix/identifier` path with `allow`
/// (`SipiHttpServer.cpp:465-478`).
fn iiif_access(state: &AppState, parsed: &ParsedRequest, method: &Method, uri: &Uri, headers: &HeaderMap) -> Result<Access, Response> {
    if state.has_preflight {
        let ctx = build_ctx(method, uri, headers).ok_or_else(|| sink::error_response(StatusCode::BAD_REQUEST))?;
        let outcome = ffi::preflight(&parsed.prefix, &parsed.identifier, &ctx)
            .map_err(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR))?;
        Ok(access_from(outcome))
    } else {
        Ok(Access {
            infile: path::build_request_path(&state.imgroot, &parsed.prefix, &parsed.identifier, state.prefix_as_path),
            permission: SipiPermType::Allow,
            kv: Vec::new(),
        })
    }
}

/// Resolve the infile + permission for a `/file` download: build the path, then
/// run `file_pre_flight` on it when defined (`SipiHttpServer.cpp:1078-1100`).
/// A non-allow/restrict permission is rejected here (401).
fn file_access(state: &AppState, parsed: &ParsedRequest, method: &Method, uri: &Uri, headers: &HeaderMap) -> Result<Access, Response> {
    let built = path::build_request_path(&state.imgroot, &parsed.prefix, &parsed.identifier, state.prefix_as_path);
    if !state.has_file_preflight {
        return Ok(Access { infile: built, permission: SipiPermType::Allow, kv: Vec::new() });
    }
    let ctx = build_ctx(method, uri, headers).ok_or_else(|| sink::error_response(StatusCode::BAD_REQUEST))?;
    let outcome =
        ffi::file_preflight(&built, &ctx).map_err(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR))?;
    match outcome.permission {
        SipiPermType::Allow | SipiPermType::Restrict => Ok(access_from(outcome)),
        _ => Err(sink::error_response(StatusCode::UNAUTHORIZED)),
    }
}

/// Fold a [`PreflightOutcome`] into an [`Access`], taking the hook's `infile`
/// (empty for `deny`, which then fails R2 → 404, matching the C++ `access()`).
fn access_from(outcome: PreflightOutcome) -> Access {
    let infile = outcome.get("infile").unwrap_or_default().to_owned();
    Access { infile, permission: outcome.permission, kv: outcome.kv }
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
fn serve_image(resolved: &str, parsed: &ParsedRequest, headers: &HeaderMap, is_head: bool, access: &Access) -> Response {
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
        watermark_path: c_watermark.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
        // The engine hardcodes http:// in its canonical Link header today and
        // ignores forwarded_proto; the Rust-built info/knora/redirect ids honour
        // X-Forwarded-Proto. Honouring it in the engine is a separate follow-up.
        forwarded_proto: std::ptr::null(),
        forwarded_host: c_host.as_ptr(),
        request_uri: c_uri.as_ptr(),
        is_head: i32::from(is_head),
    };

    // SAFETY: every pointer in `req` outlives this synchronous call; the seam
    // guards C++ exceptions (→ status code, never an unwind into Rust).
    let (code, sink_state) = sink::serve_buffered(|resp: &SipiResponse| unsafe { ffi::sipi_serve_image(&req, resp) });
    let mut response = finish_serve(code, sink_state);
    apply_origin_cors(&mut response, headers);
    response
}

/// Raw `/file` passthrough via `sipi_serve_file` (Range/206). Content-Disposition
/// is set caller-side, only on a Range request (`SipiHttpServer.cpp:1120-1129`).
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
    apply_origin_cors(&mut response, headers);
    response
}

/// CORS preflight (`OPTIONS`): echo the Origin (no credentials — DEV-6061),
/// advertise the served methods, and echo the requested headers
/// (`SipiHttpServer`/`Connection.cpp:411-416`, minus the credential reflection).
/// Engine-independent, so it serves without the readiness gate.
pub async fn cors_preflight(headers: HeaderMap) -> Response {
    let mut builder = Response::builder()
        .status(StatusCode::NO_CONTENT)
        .header(header::ACCESS_CONTROL_ALLOW_METHODS, "GET, HEAD, OPTIONS");
    if let Some(origin) = header_str(&headers, "origin").and_then(|o| HeaderValue::from_str(&o).ok()) {
        builder = builder.header(header::ACCESS_CONTROL_ALLOW_ORIGIN, origin);
    }
    if let Some(req_headers) =
        header_str(&headers, "access-control-request-headers").and_then(|h| HeaderValue::from_str(&h).ok())
    {
        builder = builder.header(header::ACCESS_CONTROL_ALLOW_HEADERS, req_headers);
    }
    builder.body(Body::empty()).unwrap_or_else(|_| sink::error_response(StatusCode::INTERNAL_SERVER_ERROR))
}

/// Echo the request `Origin` into `Access-Control-Allow-Origin` (when present)
/// for engine-served image / `/file` responses. DEV-6061: never paired with
/// `Access-Control-Allow-Credentials: true` (the C++ transport's reflection bug).
fn apply_origin_cors(response: &mut Response, headers: &HeaderMap) {
    if let Some(origin) = header_str(headers, "origin").and_then(|o| HeaderValue::from_str(&o).ok()) {
        response.headers_mut().insert(header::ACCESS_CONTROL_ALLOW_ORIGIN, origin);
    }
}

fn serve_info_json(resolved: &str, parsed: &ParsedRequest, headers: &HeaderMap, access: &Access) -> Response {
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

/// Build the preflight request context from the axum request: all headers
/// (lowercased C-side), the parsed cookies, host, and client IP. `secure` is
/// false (SIPI runs plain HTTP behind Traefik, matching the C++ `conn.secure()`).
fn build_ctx(method: &Method, uri: &Uri, headers: &HeaderMap) -> Option<ffi::RequestContext> {
    let header_vec: Vec<(String, String)> = headers
        .iter()
        .filter_map(|(n, v)| v.to_str().ok().map(|v| (n.as_str().to_owned(), v.to_owned())))
        .collect();
    let cookies = parse_cookies(headers);
    let (_scheme, host) = forwarded(headers);
    ffi::build_request_context(method.as_str(), &client_ip(headers), 0, false, &host, uri.path(), &header_vec, &cookies)
}

/// Parse the `Cookie` header into name/value pairs (the parsed map the Lua
/// `server.cookies` binding reads).
fn parse_cookies(headers: &HeaderMap) -> Vec<(String, String)> {
    header_str(headers, header::COOKIE.as_str())
        .map(|raw| {
            raw.split(';')
                .filter_map(|kv| {
                    let kv = kv.trim();
                    let eq = kv.find('=')?;
                    Some((kv[..eq].trim().to_owned(), kv[eq + 1..].trim().to_owned()))
                })
                .collect()
        })
        .unwrap_or_default()
}

fn access_kv_cstring(access: &Access, key: &str) -> Option<CString> {
    access.kv.iter().find(|(k, _)| k == key).and_then(|(_, v)| CString::new(v.as_str()).ok())
}

/// Render the engine's buffered response, mapping its status code.
fn finish_serve(code: i32, sink_state: sink::SinkState) -> Response {
    if code == 0 {
        sink::into_response(sink_state)
    } else {
        let status = StatusCode::from_u16(code as u16).unwrap_or(StatusCode::INTERNAL_SERVER_ERROR);
        sink::error_response(status)
    }
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

/// Synthesise the request scheme + host from the forwarded headers (SIPI runs
/// plain HTTP behind Traefik, so the real scheme is in `X-Forwarded-Proto`). Host
/// falls back from `X-Forwarded-Host` to `Host`.
fn forwarded(headers: &HeaderMap) -> (String, String) {
    let scheme =
        if header_str(headers, "x-forwarded-proto").as_deref() == Some("https") { "https" } else { "http" }.to_owned();
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
            h.insert(axum::http::HeaderName::from_bytes(k.as_bytes()).unwrap(), HeaderValue::from_str(v).unwrap());
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
    fn parse_cookies_splits_pairs() {
        let h = headers(&[("cookie", "sid=abc; theme=dark; lone")]);
        let c = parse_cookies(&h);
        assert_eq!(c, vec![("sid".to_owned(), "abc".to_owned()), ("theme".to_owned(), "dark".to_owned())]);
    }

    #[test]
    fn content_disposition_strips_quotes_and_controls() {
        let v = content_disposition("a\"b\nc.tif").unwrap();
        assert_eq!(v.to_str().unwrap(), "inline; filename=\"abc.tif\"");
    }
}
