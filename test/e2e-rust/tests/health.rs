mod common;

use common::{client, server};

#[test]
fn health_returns_200_with_json() {
    let srv = server();
    let resp = client()
        .get(format!("{}/health", srv.base_url))
        .send()
        .expect("health request failed");

    assert_eq!(resp.status().as_u16(), 200);

    let ct = resp.headers().get("Content-Type").unwrap().to_str().unwrap();
    assert!(ct.contains("application/json"), "Content-Type should be JSON, got: {}", ct);

    let body: serde_json::Value = resp.json().expect("health should return JSON");
    assert_eq!(body["status"], "ok");
    assert!(body["version"].is_string(), "version should be a string");
    assert!(body["uptime_seconds"].is_number(), "uptime_seconds should be a number");
    assert!(body["uptime_seconds"].as_u64().unwrap() >= 0, "uptime should be non-negative");
}

#[test]
fn health_responds_quickly() {
    let srv = server();
    let start = std::time::Instant::now();
    let resp = client()
        .get(format!("{}/health", srv.base_url))
        .send()
        .expect("health request failed");
    let elapsed = start.elapsed();

    assert_eq!(resp.status().as_u16(), 200);
    assert!(elapsed.as_millis() < 100, "health should respond within 100ms, took {}ms", elapsed.as_millis());
}
