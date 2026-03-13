mod common;

use common::{client, server};
use std::time::Instant;

// Smoke latency thresholds — these catch gross regressions (10x slowdown),
// not subtle performance changes. They are NOT strict SLAs.
//
// If these thresholds are too tight for CI hardware, increase them or
// mark individual tests `#[ignore]`.
const INFO_JSON_MAX_MS: u128 = 50;
const IMAGE_CACHE_MISS_MAX_MS: u128 = 500;
const IMAGE_CACHE_HIT_MAX_MS: u128 = 100;

/// Warm up the server by making a few requests before timing.
/// This avoids measuring JIT-like startup costs on the first request.
fn warmup() {
    let srv = server();
    for _ in 0..3 {
        let _ = client()
            .get(format!("{}/unit/lena512.jp2/info.json", srv.base_url))
            .send();
    }
}

#[test]
fn info_json_responds_within_threshold() {
    warmup();
    let srv = server();

    // Take the median of 5 runs to reduce noise
    let mut durations = Vec::new();
    for _ in 0..5 {
        let start = Instant::now();
        let resp = client()
            .get(format!("{}/unit/lena512.jp2/info.json", srv.base_url))
            .send()
            .expect("GET info.json failed");
        let elapsed = start.elapsed().as_millis();

        assert!(resp.status().is_success());
        durations.push(elapsed);
    }

    durations.sort();
    let median = durations[durations.len() / 2];

    assert!(
        median <= INFO_JSON_MAX_MS,
        "info.json median latency {}ms exceeds threshold {}ms (all: {:?})",
        median,
        INFO_JSON_MAX_MS,
        durations
    );
}

#[test]
fn image_delivery_cache_miss_within_threshold() {
    warmup();
    let srv = server();

    // Request a 512x512 JPEG — cache miss (first request with unique params)
    // Use a specific size to make it a deterministic cache miss
    let start = Instant::now();
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/512,512/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET image (cache miss) failed");
    let elapsed = start.elapsed().as_millis();

    assert!(resp.status().is_success());
    assert!(
        elapsed <= IMAGE_CACHE_MISS_MAX_MS,
        "Image delivery (cache miss) took {}ms, threshold is {}ms",
        elapsed,
        IMAGE_CACHE_MISS_MAX_MS
    );
}

#[test]
fn image_delivery_cache_hit_within_threshold() {
    warmup();
    let srv = server();

    let url = format!(
        "{}/unit/lena512.jp2/full/256,256/0/default.jpg",
        srv.base_url
    );

    // First request — populate cache
    let resp = client()
        .get(&url)
        .send()
        .expect("GET image (populate cache) failed");
    assert!(resp.status().is_success());

    // Second request — should be a cache hit
    // Take median of 5 runs
    let mut durations = Vec::new();
    for _ in 0..5 {
        let start = Instant::now();
        let resp = client()
            .get(&url)
            .send()
            .expect("GET image (cache hit) failed");
        let elapsed = start.elapsed().as_millis();

        assert!(resp.status().is_success());
        durations.push(elapsed);
    }

    durations.sort();
    let median = durations[durations.len() / 2];

    assert!(
        median <= IMAGE_CACHE_HIT_MAX_MS,
        "Image delivery (cache hit) median latency {}ms exceeds threshold {}ms (all: {:?})",
        median,
        IMAGE_CACHE_HIT_MAX_MS,
        durations
    );
}
