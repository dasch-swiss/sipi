//! Metrics collection must not kill the server (SIPI-1R regression gate).
//!
//! Release 6.3.0 crash-looped on ls-test: the OTel periodic reader's first
//! collection invoked the `sipi.malloc.*` gauge callbacks, whose mis-declared
//! `mi_stats_get` FFI segfaulted inside mimalloc ~60s after every boot. No
//! test executed that path — telemetry is fail-open (without
//! `OTEL_EXPORTER_OTLP_ENDPOINT` the meter provider is never installed and
//! the callbacks never registered), and the one e2e that does set the
//! endpoint (`tracing.rs`) never outlives the 60-second default export
//! interval.
//!
//! This closes the gap end-to-end: enable the metrics pipeline against a
//! dead OTLP endpoint (export failures are fail-open and dropped), shrink
//! the export interval via the standard `OTEL_METRIC_EXPORT_INTERVAL` so
//! collections fire inside the test window, serve a real request, sit
//! through several collection cycles — each one invokes every observable
//! callback, including the allocator stats reader — and assert the server
//! is still alive and serving.

use std::time::Duration;

use sipi_e2e::{http_client, test_data_dir, SipiServer};

#[test]
fn metrics_collection_does_not_kill_the_server() {
    let test_data = test_data_dir();
    let srv = SipiServer::start_env(
        "config/sipi.e2e-test-config.lua",
        &test_data,
        &[],
        &[
            ("OTEL_EXPORTER_OTLP_ENDPOINT", "http://127.0.0.1:1"),
            // Milliseconds (OTel spec). Several collections per test run
            // instead of the 60s default that outlives every test.
            ("OTEL_METRIC_EXPORT_INTERVAL", "250"),
        ],
    );
    let client = http_client();

    // One real decode request so the engine-side counters the gauges
    // snapshot (cache, decode memory, pool permits) are warm.
    let resp = client
        .get(format!(
            "{}/unit/lena512.jp2/full/256,/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("image request to sipi");
    assert_eq!(resp.status().as_u16(), 200, "decode request must succeed");

    // Sit through several collection cycles.
    std::thread::sleep(Duration::from_millis(1500));

    // The server must have survived collection: still up, still serving.
    let health = client
        .get(format!("{}/health", srv.base_url))
        .send()
        .expect("server still accepts connections after metrics collections");
    assert_eq!(
        health.status().as_u16(),
        200,
        "healthy after metrics collections"
    );
}
