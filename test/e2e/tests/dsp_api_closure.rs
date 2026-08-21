//! Runs the migrated Lua runtime against a verbatim copy of dsp-api's
//! production script closure (the 9 live files from
//! `dsp-api/modules/sipi/scripts/`, staged under `test/_test_data/dsp-api/`)
//! — the audit's breaks-prod findings as executable regressions: the `os.date`
//! call inside `log()` on every request (D1), lowercase `server.header`
//! lookups (D6), Lua 5.3 integer division in `send_response.lua` (D8), the
//! bare-globals inter-module contract (D9), `os.getenv` for the API host
//! (D11), and the routed-but-missing `delete_temp_file.lua` staying a
//! request-time 404 (D12).
//!
//! DSP-API itself is a canned mock: a TCP thread answering
//! `/admin/files/{shortcode}/{file}` with the permission JSON the closure
//! expects, selected by filename.

use serde_json::json;
use sipi_e2e::jwt::create_jwt;
use sipi_e2e::{http_client, test_data_dir, SipiServer};
use std::io::{Read, Write};
use std::net::TcpListener;
use std::sync::OnceLock;

/// Matches `jwt_secret` in `dsp-api/sipi.dsp-api-closure-config.lua`.
const JWT_SECRET: &str = "UP 4888, nice 4-8-4 steam engine";
/// The `iss` the closure validates tokens against
/// (`KNORA_WEBAPI_KNORA_API_EXTERNAL_HOST:PORT`).
const ISSUER_HOST: &str = "0.0.0.0";
const ISSUER_PORT: &str = "3333";

/// A canned DSP-API `/admin/files` mock: permission JSON selected by the
/// requested filename. Runs for the whole test binary.
fn mock_dsp_api() -> u16 {
    static PORT: OnceLock<u16> = OnceLock::new();
    *PORT.get_or_init(|| {
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind mock DSP-API");
        let port = listener.local_addr().expect("mock addr").port();
        std::thread::spawn(move || {
            for stream in listener.incoming() {
                let Ok(mut stream) = stream else { continue };
                let mut buf = [0u8; 4096];
                let n = stream.read(&mut buf).unwrap_or(0);
                let request = String::from_utf8_lossy(&buf[..n]);
                let body = if request.contains("perm2file") {
                    r#"{"permissionCode":2}"#
                } else if request.contains("perm1file") {
                    r#"{"permissionCode":1,"restrictedViewSettings":{"size":"!128,128"}}"#
                } else {
                    r#"{"permissionCode":0}"#
                };
                let _ = write!(
                    stream,
                    "HTTP/1.1 200 OK\r\ncontent-type: application/json\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{}",
                    body.len(),
                    body
                );
            }
        });
        port
    })
}

/// The shared server running dsp-api's closure, pointed at the mock via the
/// same env vars production uses.
fn server() -> &'static SipiServer {
    static SERVER: OnceLock<SipiServer> = OnceLock::new();
    SERVER.get_or_init(|| {
        let api_port = mock_dsp_api().to_string();
        SipiServer::start_env(
            "dsp-api/sipi.dsp-api-closure-config.lua",
            &test_data_dir(),
            &[],
            &[
                ("SIPI_WEBAPI_HOSTNAME", "127.0.0.1"),
                ("SIPI_WEBAPI_PORT", api_port.as_str()),
                ("KNORA_WEBAPI_KNORA_API_EXTERNAL_HOST", ISSUER_HOST),
                ("KNORA_WEBAPI_KNORA_API_EXTERNAL_PORT", ISSUER_PORT),
            ],
        )
    })
}

fn iiif_url(file: &str) -> String {
    format!("{}/0801/{file}/full/max/0/default.jpg", server().base_url)
}

/// Full view permission (code 2) from the API: the image serves. This is the
/// closure's whole anonymous happy path — `require` chain, `log()`'s
/// `os.date`, `find_file` over `server.fs.exists`, and `server.http` to the
/// API — in one request.
#[test]
fn permission_code_2_serves_the_image() {
    let resp = http_client()
        .get(iiif_url("perm2file.jp2"))
        .send()
        .expect("GET perm2");
    assert_eq!(resp.status().as_u16(), 200);
    assert!(resp.bytes().expect("body").len() > 1000);
}

/// No view permission (code 0) is a deny — 401.
#[test]
fn permission_code_0_denies() {
    let resp = http_client()
        .get(iiif_url("perm0file.jp2"))
        .send()
        .expect("GET perm0");
    assert_eq!(resp.status().as_u16(), 401);
}

/// Restricted view (code 1 with a size): the closure returns the
/// `{type='restrict', size=…}` table and the served image is the reduced one
/// — strictly smaller than the full-size render of the same source.
#[test]
fn permission_code_1_restricts_the_size() {
    let restricted = http_client()
        .get(iiif_url("perm1file.jp2"))
        .send()
        .expect("GET perm1");
    assert_eq!(restricted.status().as_u16(), 200);
    let restricted_len = restricted.bytes().expect("body").len();

    let full = http_client()
        .get(iiif_url("perm2file.jp2"))
        .send()
        .expect("GET perm2");
    let full_len = full.bytes().expect("body").len();

    assert!(
        restricted_len < full_len,
        "restricted render ({restricted_len}B) must be smaller than full ({full_len}B)"
    );
}

/// The `tmp` prefix is always allowed without asking the API.
#[test]
fn tmp_prefix_serves_without_the_api() {
    let resp = http_client()
        .get(format!(
            "{}/tmp/tmpfile.jp2/full/max/0/default.jpg",
            server().base_url
        ))
        .send()
        .expect("GET tmp");
    assert_eq!(resp.status().as_u16(), 200);
}

/// A system-admin token (scope `admin`, valid `exp`/`aud`/`iss`) bypasses the
/// permission lookup — the closure's own JWT validation path over the
/// hardened `server.decode_jwt`.
#[test]
fn admin_token_bypasses_the_permission_lookup() {
    let exp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .expect("clock")
        .as_secs()
        + 3600;
    let token = create_jwt(
        &json!({
            "sub": "admin-user",
            "scope": "admin",
            "aud": ["Knora", "Sipi"],
            "iss": format!("{ISSUER_HOST}:{ISSUER_PORT}"),
            "exp": exp,
        }),
        JWT_SECRET,
    );
    // perm0file would be denied by the API — the admin scope must win.
    let resp = http_client()
        .get(iiif_url("perm0file.jp2"))
        .header("Authorization", format!("Bearer {token}"))
        .send()
        .expect("GET as admin");
    assert_eq!(resp.status().as_u16(), 200);
}

/// An expired token hits the closure's own 401 path (`send_error` inside
/// `authentication.lua`) — the hardened `decode_jwt` rejects it, and the
/// script's direct response wins.
#[test]
fn expired_token_is_a_401_from_the_closure() {
    let token = create_jwt(
        &json!({
            "sub": "expired-user",
            "aud": ["Sipi"],
            "iss": format!("{ISSUER_HOST}:{ISSUER_PORT}"),
            "exp": 1000000000u64,
        }),
        JWT_SECRET,
    );
    let resp = http_client()
        .get(iiif_url("perm2file.jp2"))
        .header("Authorization", format!("Bearer {token}"))
        .send()
        .expect("GET expired");
    assert_eq!(resp.status().as_u16(), 401);
}

/// An unknown file: `find_file` returns nil, the closure answers
/// `'allow', 'file_does_not_exist'`, and serving that path fails downstream —
/// an error status, never a 200.
#[test]
fn unknown_file_fails_after_the_allow() {
    let resp = http_client()
        .get(iiif_url("nosuchfile.jp2"))
        .send()
        .expect("GET unknown");
    let status = resp.status().as_u16();
    assert!(
        status >= 400,
        "an unknown file must not serve, got {status}"
    );
}

/// dsp-api's production configs route `delete_temp_file.lua`, which does not
/// exist on disk (audit D12): the route must stay a request-time 404, never a
/// boot failure — the shared server booting at all is half the assertion.
#[test]
fn routed_but_missing_script_is_a_404() {
    let resp = http_client()
        .delete(format!("{}/delete_temp_file", server().base_url))
        .send()
        .expect("DELETE missing route");
    assert_eq!(resp.status().as_u16(), 404);
}
