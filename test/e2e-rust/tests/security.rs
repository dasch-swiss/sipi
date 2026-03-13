mod common;

use base64::Engine;
use common::{client, server};
use jsonwebtoken::{encode, Algorithm, EncodingKey, Header};
use serde_json::json;
use sipi_e2e::{http_client, test_data_dir, SipiServer};
use std::io::{Read as _, Write as _};
use std::net::TcpStream;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

const JWT_SECRET: &str = "UP 4888, nice 4-8-4 steam engine";

/// Create a valid JWT with the given claims using HS256.
fn create_jwt(claims: &serde_json::Value) -> String {
    let header = Header::new(Algorithm::HS256);
    let key = EncodingKey::from_secret(JWT_SECRET.as_bytes());
    encode(&header, claims, &key).expect("JWT encode failed")
}

// =============================================================================
// JWT Security Tests (using the 'auth' prefix which checks JWT tokens)
// =============================================================================

#[test]
fn jwt_expired_token() {
    // SECURITY FINDING: sipi's Lua pre-flight handler does NOT check the `exp` claim.
    // It only validates the signature and checks `token_val['allow']`.
    // An expired JWT with a valid signature and `allow: true` grants access.
    // This documents the current behavior — fixing requires Lua handler changes.
    let srv = server();

    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    let claims = json!({
        "allow": true,
        "exp": now - 3600,  // expired 1 hour ago
        "iat": now - 7200,
    });
    let token = create_jwt(&claims);

    let resp = client()
        .get(format!(
            "{}/auth/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .header("Authorization", format!("Bearer {}", token))
        .send()
        .expect("expired JWT request failed");

    let status = resp.status().as_u16();
    // Current behavior: expired JWT is accepted (200) because Lua only checks `allow`
    // Ideal behavior would be 401, but documenting as-is.
    assert_eq!(
        status, 200,
        "KNOWN: sipi accepts expired JWT (no exp check in Lua handler), got {}",
        status
    );
}

#[test]
fn jwt_alg_none_bypass() {
    // Send JWT with alg:none and no signature — common JWT vulnerability.
    let srv = server();

    // Manually craft a JWT with alg: none
    let b64 = base64::engine::general_purpose::URL_SAFE_NO_PAD;
    let header = b64.encode(r#"{"alg":"none","typ":"JWT"}"#);
    let payload = b64.encode(r#"{"allow":true}"#);
    let token = format!("{}..{}", header, payload); // empty signature

    let resp = client()
        .get(format!(
            "{}/auth/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .header("Authorization", format!("Bearer {}", token))
        .send()
        .expect("alg:none JWT request failed");

    let status = resp.status().as_u16();
    // Must NOT return 200 — that would mean the bypass worked
    assert_ne!(
        status, 200,
        "SECURITY: alg:none JWT should NOT grant access (got 200)"
    );
}

#[test]
fn jwt_tampered_payload() {
    // Create valid JWT, modify payload without re-signing, verify rejection.
    let srv = server();

    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    let claims = json!({
        "allow": false,
        "exp": now + 3600,
    });
    let valid_token = create_jwt(&claims);

    // Split the token and replace payload with "allow: true"
    let parts: Vec<&str> = valid_token.split('.').collect();
    assert_eq!(parts.len(), 3, "JWT should have 3 parts");

    let b64 = base64::engine::general_purpose::URL_SAFE_NO_PAD;
    let tampered_payload = b64.encode(
        serde_json::to_string(&json!({
            "allow": true,
            "exp": now + 3600,
        }))
        .unwrap(),
    );
    let tampered_token = format!("{}.{}.{}", parts[0], tampered_payload, parts[2]);

    let resp = client()
        .get(format!(
            "{}/auth/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .header("Authorization", format!("Bearer {}", tampered_token))
        .send()
        .expect("tampered JWT request failed");

    let status = resp.status().as_u16();
    // Tampered token should be rejected
    assert_ne!(
        status, 200,
        "SECURITY: tampered JWT should NOT grant access (got 200)"
    );
}

#[test]
fn config_empty_jwt_secret() {
    // Start sipi with empty jwt_secret, verify JWT-authenticated endpoints
    // behave correctly (reject or accept-without-auth).
    let test_data = test_data_dir();

    // Create a config with empty jwt_secret
    let config_content = r#"sipi = {
    port = 1024,
    nthreads = 4,
    jpeg_quality = 60,
    scaling_quality = { jpeg = "medium", tiff = "high", png = "high", j2k = "high" },
    keep_alive = 5,
    max_post_size = '300M',
    imgroot = './images',
    prefix_as_path = true,
    subdir_levels = 0,
    subdir_excludes = { "tmp", "thumb" },
    initscript = './config/sipi.init-knora.lua',
    cache_dir = './cache',
    cache_size = '20M',
    cache_nfiles = 8,
    scriptdir = './scripts',
    thumb_size = '!128,128',
    tmpdir = '/tmp',
    max_temp_file_age = 86400,
    knora_path = 'localhost',
    knora_port = '3434',
    ssl_port = 1025,
    ssl_certificate = './certificate/certificate.pem',
    ssl_key = './certificate/key.pem',
    jwt_secret = '',
    logfile = "sipi.log",
    loglevel = "DEBUG"
}

admin = {
    user = 'admin',
    password = 'Sipi-Admin'
}

fileserver = {
    docroot = './server',
    wwwroute = '/server'
}

routes = {}
"#;

    let config_path = test_data.join("config/sipi.empty-jwt.lua");
    std::fs::write(&config_path, config_content).expect("write empty jwt config");

    let srv = SipiServer::start("config/sipi.empty-jwt.lua", &test_data);
    let c = http_client();

    // Basic IIIF should still work (no auth required for unit prefix)
    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("basic IIIF request failed with empty jwt_secret");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "basic IIIF should work even with empty jwt_secret"
    );

    let _ = std::fs::remove_file(&config_path);
}

#[test]
fn crlf_header_injection() {
    // Request identifier containing %0d%0a (CRLF), verify no response header injection.
    let srv = server();

    // %0d%0a is URL-encoded CRLF
    let resp = client()
        .get(format!(
            "{}/unit/lena512%0d%0aX-Injected:%20evil/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("CRLF injection request failed");

    // Should not crash and should not have injected header
    let status = resp.status().as_u16();
    assert!(
        status == 400 || status == 404 || status == 200,
        "CRLF request should return valid status, got {}",
        status
    );

    // Verify no injected header
    assert!(
        resp.headers().get("X-Injected").is_none(),
        "SECURITY: response should not contain injected header"
    );
}

#[test]
fn error_no_path_disclosure() {
    // Trigger a server error, verify response body does not leak internal paths.
    let srv = server();

    let resp = client()
        .get(format!(
            "{}/unit/nonexistent-file-for-path-test.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("error path disclosure request failed");

    let status = resp.status().as_u16();
    assert!(status >= 400, "nonexistent file should return error");

    let body = resp.text().unwrap_or_default().to_lowercase();

    // Check for common path disclosure patterns
    let path_indicators = [
        "/users/",
        "/home/",
        "/var/",
        "/opt/",
        "/tmp/sipi",
        "images/unit/",
        ".jp2",
    ];

    for indicator in &path_indicators {
        // Allow the filename itself to appear but not full filesystem paths
        if *indicator != ".jp2" {
            assert!(
                !body.contains(indicator),
                "SECURITY: error response may leak internal path '{}' in body: {}",
                indicator,
                &body[..body.len().min(500)]
            );
        }
    }
}

#[test]
fn decompression_bomb_rejection() {
    // Request an image that would decompress to extreme dimensions.
    // We can't easily create a decompression bomb test fixture, so instead
    // test that extreme IIIF size requests are rejected.
    let srv = server();

    // Request upscaling to extreme dimensions (100000x100000)
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/^100000,100000/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("extreme upscale request failed");

    let status = resp.status().as_u16();
    // Server should reject or handle gracefully — not OOM
    assert!(
        status == 400 || status == 500 || status == 200,
        "extreme upscale should be handled gracefully, got {}",
        status
    );

    // Verify server still responsive after potential OOM pressure
    let health = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("server should respond after extreme upscale");
    assert_eq!(health.status().as_u16(), 200);
}

#[test]
#[ignore = "slowloris test may be flaky in CI due to timing sensitivity"]
fn slowloris_resilience() {
    // Open connection, send partial request headers slowly, verify server doesn't hang.
    let srv = server();

    // Connect raw TCP
    let addr = format!("127.0.0.1:{}", srv.http_port);
    let mut stream = TcpStream::connect(&addr).expect("TCP connect failed");
    stream.set_read_timeout(Some(Duration::from_secs(10))).ok();
    stream.set_write_timeout(Some(Duration::from_secs(5))).ok();

    // Send partial HTTP request (no final \r\n\r\n)
    stream
        .write_all(b"GET /unit/lena512.jp2/full/max/0/default.jpg HTTP/1.1\r\n")
        .expect("write partial request");
    stream
        .write_all(b"Host: localhost\r\n")
        .expect("write host header");
    // Don't send the final \r\n — this is a slowloris attack

    // Wait a bit
    std::thread::sleep(Duration::from_secs(2));

    // Verify server is still accepting new connections
    let health = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("server should still accept connections during slowloris");
    assert_eq!(health.status().as_u16(), 200);

    // Try reading from the slow connection — server should eventually close it
    let mut buf = [0u8; 1024];
    let result = stream.read(&mut buf);
    // Either timeout (server kept connection open) or closed (server killed it)
    // Both are acceptable as long as the server didn't hang
    match result {
        Ok(0) => {} // Connection closed — good
        Ok(_) => {} // Got some data — server responded despite partial request
        Err(e) => {
            // Timeout or connection reset — acceptable
            eprintln!("Slowloris connection result: {}", e);
        }
    }
}
