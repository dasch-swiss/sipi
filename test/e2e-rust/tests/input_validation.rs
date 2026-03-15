mod common;

use common::{client, server};
use std::io::{Read as _, Write as _};
use std::net::TcpStream;
use std::time::Duration;

// =============================================================================
// Path Traversal Tests (R1-R4)
// =============================================================================

#[test]
fn path_traversal_encoded_dotdot_returns_400() {
    let srv = server();
    // Use raw TCP to send %2e%2e without client-side URL normalization
    let addr = format!("127.0.0.1:{}", srv.http_port);
    let mut stream = TcpStream::connect(&addr).expect("TCP connect failed");
    stream.set_read_timeout(Some(Duration::from_secs(5))).ok();

    let request = "GET /unit/%2e%2e/%2e%2e/etc/passwd/full/max/0/default.jpg HTTP/1.1\r\nHost: localhost\r\n\r\n";
    stream.write_all(request.as_bytes()).expect("write failed");

    let mut response = String::new();
    let _ = stream.read_to_string(&mut response);

    assert!(
        response.contains("400"),
        "path traversal with %2e%2e should return 400, got: {}",
        response.lines().next().unwrap_or("")
    );
}

#[test]
fn path_traversal_double_encoded_returns_400() {
    let srv = server();
    let addr = format!("127.0.0.1:{}", srv.http_port);
    let mut stream = TcpStream::connect(&addr).expect("TCP connect failed");
    stream.set_read_timeout(Some(Duration::from_secs(5))).ok();

    let request = "GET /unit/%252e%252e/%252e%252e/etc/passwd/full/max/0/default.jpg HTTP/1.1\r\nHost: localhost\r\n\r\n";
    stream.write_all(request.as_bytes()).expect("write failed");

    let mut response = String::new();
    let _ = stream.read_to_string(&mut response);

    assert!(
        response.contains("400"),
        "double-encoded path traversal should return 400, got: {}",
        response.lines().next().unwrap_or("")
    );
}

#[test]
fn path_traversal_no_content_leaked() {
    let srv = server();
    let addr = format!("127.0.0.1:{}", srv.http_port);
    let mut stream = TcpStream::connect(&addr).expect("TCP connect failed");
    stream.set_read_timeout(Some(Duration::from_secs(5))).ok();

    let request = "GET /unit/%2e%2e/%2e%2e/etc/passwd/full/max/0/default.jpg HTTP/1.1\r\nHost: localhost\r\n\r\n";
    stream.write_all(request.as_bytes()).expect("write failed");

    let mut response = String::new();
    let _ = stream.read_to_string(&mut response);

    // R3: Must not leak internal filesystem paths
    let lower = response.to_lowercase();
    assert!(
        !lower.contains("root:"),
        "path traversal must not leak file content"
    );
    assert!(
        !lower.contains("/etc/") && !lower.contains("/sipi/images"),
        "error response must not leak internal paths in: {}",
        &lower[..lower.len().min(500)]
    );
}

#[test]
fn path_traversal_mixed_case_encoded() {
    let srv = server();
    let addr = format!("127.0.0.1:{}", srv.http_port);
    let mut stream = TcpStream::connect(&addr).expect("TCP connect failed");
    stream.set_read_timeout(Some(Duration::from_secs(5))).ok();

    // Mixed case: %2E%2e
    let request = "GET /unit/%2E%2e/%2E%2e/etc/passwd/full/max/0/default.jpg HTTP/1.1\r\nHost: localhost\r\n\r\n";
    stream.write_all(request.as_bytes()).expect("write failed");

    let mut response = String::new();
    let _ = stream.read_to_string(&mut response);

    assert!(
        response.contains("400"),
        "mixed-case encoded traversal should return 400, got: {}",
        response.lines().next().unwrap_or("")
    );
}

// =============================================================================
// Null Byte Injection Tests (R5-R7)
// =============================================================================

#[test]
fn null_byte_in_iiif_url_returns_400() {
    let srv = server();
    // Use raw TCP since null byte in URL causes HTTP client issues
    let addr = format!("127.0.0.1:{}", srv.http_port);
    let mut stream = TcpStream::connect(&addr).expect("TCP connect failed");
    stream.set_read_timeout(Some(Duration::from_secs(5))).ok();

    let request = "GET /unit/lena512%00.jp2/full/max/0/default.jpg HTTP/1.1\r\nHost: localhost\r\n\r\n";
    stream.write_all(request.as_bytes()).expect("write failed");

    let mut response = String::new();
    let _ = stream.read_to_string(&mut response);

    assert!(
        response.contains("400"),
        "null byte in URL should return 400, got: {}",
        response.lines().next().unwrap_or("")
    );
}

#[test]
fn null_byte_server_stays_healthy() {
    let srv = server();

    // Send null byte request via raw TCP (server may close connection)
    let addr = format!("127.0.0.1:{}", srv.http_port);
    if let Ok(mut stream) = TcpStream::connect(&addr) {
        stream.set_read_timeout(Some(Duration::from_secs(2))).ok();
        let request = "GET /unit/test%00evil/full/max/0/default.jpg HTTP/1.1\r\nHost: localhost\r\n\r\n";
        let _ = stream.write_all(request.as_bytes());
        let mut buf = [0u8; 1024];
        let _ = stream.read(&mut buf);
    }

    // Small delay for server to process
    std::thread::sleep(Duration::from_millis(100));

    // Verify server is still healthy
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("server should stay healthy after null byte");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "server should still serve images after null byte attempt"
    );
}

#[test]
fn null_byte_on_non_iiif_route() {
    let srv = server();
    // R7: null byte check is in the HTTP parsing layer, protects all routes
    let addr = format!("127.0.0.1:{}", srv.http_port);
    let mut stream = TcpStream::connect(&addr).expect("TCP connect failed");
    stream.set_read_timeout(Some(Duration::from_secs(5))).ok();

    let request = "GET /server/test%00.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    stream.write_all(request.as_bytes()).expect("write failed");

    let mut response = String::new();
    let _ = stream.read_to_string(&mut response);

    assert!(
        response.contains("400"),
        "null byte should be rejected on all routes (R7), got: {}",
        response.lines().next().unwrap_or("")
    );
}

// =============================================================================
// Header Injection Tests (R8-R10)
// =============================================================================

#[test]
fn crlf_in_identifier_no_header_injection() {
    let srv = server();
    // Use raw TCP to send CRLF in URL
    let addr = format!("127.0.0.1:{}", srv.http_port);
    let mut stream = TcpStream::connect(&addr).expect("TCP connect failed");
    stream.set_read_timeout(Some(Duration::from_secs(5))).ok();

    // Null byte will be caught first (%0d%0a doesn't contain %00), so this tests
    // the CRLF handling specifically. The server should reject or sanitize.
    let request = "GET /unit/lena512%0d%0aX-Injected:%20evil.jp2/full/max/0/default.jpg HTTP/1.1\r\nHost: localhost\r\n\r\n";
    stream.write_all(request.as_bytes()).expect("write failed");

    let mut response = String::new();
    let _ = stream.read_to_string(&mut response);

    // Verify no injected header appears in the response
    assert!(
        !response.contains("X-Injected: evil"),
        "CRLF injection must not create new response headers"
    );
}
