mod common;

use common::{client, server};
use sipi_e2e::sipi_bin_path;
use std::process::Command;

#[test]
fn health_returns_200_with_json() {
    let srv = server();
    let resp = client()
        .get(format!("{}/health", srv.base_url))
        .send()
        .expect("health request failed");

    assert_eq!(resp.status().as_u16(), 200);

    let ct = resp
        .headers()
        .get("Content-Type")
        .unwrap()
        .to_str()
        .unwrap();
    assert!(
        ct.contains("application/json"),
        "Content-Type should be JSON, got: {}",
        ct
    );

    let body: serde_json::Value = resp.json().expect("health should return JSON");
    assert_eq!(body["status"], "ok");
    assert!(body["version"].is_string(), "version should be a string");
    assert!(
        body["uptime_seconds"].is_number(),
        "uptime_seconds should be a number"
    );
    assert!(
        body["uptime_seconds"].as_u64().is_some(),
        "uptime should be a non-negative integer"
    );
}

#[test]
fn health_responds_quickly() {
    let srv = server();
    let url = format!("{}/health", srv.base_url);

    // Warm up the shared blocking client first: its very first request pays a
    // one-time cost (spinning up reqwest's internal async runtime + opening the
    // connection) that is the client's, not the server's. Timing that would
    // measure the test harness, not /health — and it drifts with the reqwest/
    // hyper/tokio versions. Time a second, warm request over the pooled
    // keep-alive connection so this asserts the server's actual response time.
    client()
        .get(url.as_str())
        .send()
        .expect("health warm-up request failed");

    let start = std::time::Instant::now();
    let resp = client()
        .get(url.as_str())
        .send()
        .expect("health request failed");
    let elapsed = start.elapsed();

    assert_eq!(resp.status().as_u16(), 200);
    assert!(
        elapsed.as_millis() < 100,
        "health should respond within 100ms, took {}ms",
        elapsed.as_millis()
    );
}

// `sipi health` against a live server returns exit 0 (healthy). The `--port`
// flag lets the probe target the harness's ephemeral server port.
#[test]
fn health_subcommand_exits_zero_when_healthy() {
    let srv = server();
    let mut cmd = Command::new(sipi_bin_path());
    cmd.args(["health", "--port", &srv.http_port.to_string()]);
    // Drop ASAN_OPTIONS' log_path (inherited from `.bazelrc`'s `test:asan`)
    // so a sanitizer report, if any, lands on this process's inherited
    // stderr — visible below — instead of a file this test never reads.
    if let Ok(asan_options) = std::env::var("ASAN_OPTIONS") {
        let without_log_path = asan_options
            .split(':')
            .filter(|opt| !opt.starts_with("log_path="))
            .collect::<Vec<_>>()
            .join(":");
        cmd.env("ASAN_OPTIONS", without_log_path);
    }
    let status = cmd.status().expect("failed to run sipi health");
    assert!(
        status.success(),
        "sipi health should exit 0 against a healthy server, got: {status:?}"
    );
}

// `sipi health` with nothing listening returns exit 1 (unhealthy). Port 1 is
// never bound, so the connection is refused.
#[test]
fn health_subcommand_exits_one_when_no_server() {
    let status = Command::new(sipi_bin_path())
        .args(["health", "--port", "1"])
        .status()
        .expect("failed to run sipi health");
    assert_eq!(
        status.code(),
        Some(1),
        "sipi health should exit 1 when nothing is listening"
    );
}
