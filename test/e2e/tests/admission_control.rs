//! End-to-end coverage for cost-based two-lane admission control (ADR-0022):
//! advanced-mode full-lane budget rejection (413), the tile bypass, an
//! in-budget full decode, and basic-mode observe-only. The oracle and its
//! differential parity gate were removed (ADR-0020), so this suite is the
//! advanced-mode regression net alongside the engine-free
//! `//src/throttling/rust:admission` crate tests and the engine budget unit
//! tests.
//!
//! Scope note — **413 is tested here, transient 503 is not.** A single request
//! whose estimate alone exceeds the budget is deterministic (it 413s on the
//! first CAS, no load needed). A *transient* 503 needs two full decodes holding
//! the budget at the same instant; with the fast lena512 decode that is a
//! timing race that cannot be made deterministic without knowing the exact
//! `estimate_peak_memory` to size the budget window. That 503-vs-413 split is
//! covered deterministically at the engine level
//! (`MemoryBudgetTest.TransientOverBudgetIsNotFlaggedAsTooLarge` /
//! `SingleRequestExceedingBudgetIsFlaggedAsTooLarge`) and the thread shed by
//! the crate's `full_partition_is_hard_capped_even_when_tiles_idle`.
//!
//! Every advanced-mode test runs against its **own empty cache dir**: a cache hit
//! makes the engine return before the memory-budget gate (serve_image.cpp), so
//! a shared/warm cache would mask the very rejection under test. Each server
//! also lowers `--large-decode-threshold-bytes` to 100 KB so the small
//! `lena512.jp2` decode (~768 KB) is classified full-lane (the 32 MiB default
//! is far above it, and a tile bypasses the budget entirely).

use sipi_e2e::{http_client, test_data_dir, SipiServer};
use tempfile::TempDir;

/// Start an isolated server with the given admission mode + RAM envelope, its
/// own empty cache dir, and the full-lane classifier threshold lowered so
/// lena512 counts as a full decode. The `TempDir` is returned so the caller
/// keeps it alive; each test ends with an explicit `drop(srv); drop(cache);` so
/// the server is SIGTERM'd and shut down *before* its cache dir is removed —
/// that explicit order is load-bearing (destructured locals otherwise drop
/// LIFO, i.e. cache first, yanking the dir out from under a still-running sipi).
fn start(mode: &str, memory_limit: &str, tiles_memory_ratio: &str) -> (SipiServer, TempDir) {
    let cache = tempfile::tempdir().expect("create isolated cache dir");
    let cache_arg = cache.path().to_string_lossy().to_string();
    let srv = SipiServer::start_with_args(
        "config/sipi.e2e-test-config.lua",
        &test_data_dir(),
        &[
            "--admission-mode",
            mode,
            "--memory-limit",
            memory_limit,
            "--tiles-memory-ratio",
            tiles_memory_ratio,
            "--large-decode-threshold-bytes",
            "100000",
            "--cache-dir",
            &cache_arg,
        ],
    );
    (srv, cache)
}

const FULL_MAX: &str = "/unit/lena512.jp2/full/max/0/default.jpg";

// =============================================================================
// 413 — a single request whose estimate alone exceeds the full-lane budget
// =============================================================================

#[test]
fn advanced_estimate_alone_exceeds_budget_returns_413() {
    // full_mem = 200 * (1 - 0.5) = 100 bytes; lena512's ~768 KB estimate exceeds
    // it on the very first CAS, independent of load -> 413, no concurrency needed.
    let (srv, cache) = start("advanced", "200", "0.5");
    let resp = http_client()
        .get(format!("{}{}", srv.base_url, FULL_MAX))
        .send()
        .expect("request should return a response");

    assert_eq!(
        resp.status().as_u16(),
        413,
        "an estimate exceeding the full-lane budget alone is permanently unservable"
    );
    assert!(
        resp.headers().get("Retry-After").is_none(),
        "413 (permanently unservable) must not carry Retry-After"
    );
    drop(srv);
    drop(cache);
}

// =============================================================================
// Tiles bypass the full-lane budget
// =============================================================================

#[test]
fn advanced_tile_bypasses_full_budget() {
    // A 64x64 region (~12 KB estimate) is below the 100 KB threshold, so it is a
    // tile: it never enters the full-lane budget and serves even though the
    // full budget (100 bytes) could not admit any real decode.
    let (srv, cache) = start("advanced", "200", "0.5");
    let resp = http_client()
        .get(format!(
            "{}/unit/lena512.jp2/0,0,64,64/64,/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("tile request should return a response");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "a tile decode bypasses the full-lane budget"
    );
    drop(srv);
    drop(cache);
}

// =============================================================================
// Full decode within budget succeeds
// =============================================================================

#[test]
fn advanced_full_within_budget_succeeds() {
    // full_mem = 64M * (1 - 0.5) = 32 MiB, far above lena512's ~768 KB estimate.
    let (srv, cache) = start("advanced", "64M", "0.5");
    let resp = http_client()
        .get(format!("{}{}", srv.base_url, FULL_MAX))
        .send()
        .expect("full request should return a response");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "a full decode within budget should serve normally"
    );
    let body = resp.bytes().expect("read body");
    assert!(
        body.len() > 2 && body[0] == 0xFF && body[1] == 0xD8,
        "response should be a valid JPEG"
    );
    drop(srv);
    drop(cache);
}

// =============================================================================
// Basic mode never rejects the advanced tier (observe-only)
// =============================================================================

#[test]
fn basic_over_budget_still_serves() {
    // The default binary ships `basic`: it shadow-counts what advanced would
    // reject but changes no behaviour, so an over-budget full still serves 200.
    let (srv, cache) = start("basic", "200", "0.5");
    let resp = http_client()
        .get(format!("{}{}", srv.base_url, FULL_MAX))
        .send()
        .expect("basic-mode request should return a response");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "basic mode leaves the advanced tier observe-only and must never reject"
    );
    drop(srv);
    drop(cache);
}
