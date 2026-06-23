mod common;

use common::{client, server};

#[test]
fn server_starts_and_responds() {
    let srv = server();
    // Use info.json as a basic health check — it always works if the server is up
    let resp = client()
        .get(format!("{}/unit/lena512.jp2/info.json", srv.base_url))
        .send()
        .expect("GET info.json failed");

    assert!(
        resp.status().is_success(),
        "Expected 2xx, got {}",
        resp.status()
    );
}

#[test]
fn returns_404_for_missing_file() {
    let srv = server();
    let resp = client()
        .get(format!("{}/file-should-be-missing-123", srv.base_url))
        .send()
        .expect("GET missing file failed");

    assert_eq!(resp.status().as_u16(), 404);
}

#[test]
fn head_request_returns_headers() {
    let srv = server();
    let resp = client()
        .head(format!("{}/unit/lena512.jp2/info.json", srv.base_url))
        .send()
        .expect("HEAD info.json failed");

    assert!(
        resp.status().is_success(),
        "Expected 2xx for HEAD, got {}",
        resp.status()
    );
    // HEAD should return Content-Length but no body
    assert_eq!(resp.text().unwrap_or_default().len(), 0);
}
