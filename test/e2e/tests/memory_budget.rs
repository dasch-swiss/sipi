use sipi_e2e::{http_client, test_data_dir, SipiServer};

/// Start with enforce mode and a given budget.
fn start_enforce(budget: &str) -> SipiServer {
    let test_data = test_data_dir();
    SipiServer::start_with_args(
        "config/sipi.e2e-test-config.lua",
        &test_data,
        &[
            "--max-decode-memory",
            budget,
            "--decode-memory-mode",
            "enforce",
        ],
    )
}

/// Start with monitor mode and a given budget.
fn start_monitor(budget: &str) -> SipiServer {
    let test_data = test_data_dir();
    SipiServer::start_with_args(
        "config/sipi.e2e-test-config.lua",
        &test_data,
        &[
            "--max-decode-memory",
            budget,
            "--decode-memory-mode",
            "monitor",
        ],
    )
}

/// Start with memory budget disabled (off mode).
fn start_off() -> SipiServer {
    let test_data = test_data_dir();
    SipiServer::start_with_args(
        "config/sipi.e2e-test-config.lua",
        &test_data,
        &["--decode-memory-mode", "off"],
    )
}

// =============================================================================
// Enforce mode tests
// =============================================================================

#[test]
fn enforce_tile_request_within_budget_succeeds() {
    // Tile requests use tiny decode buffers — should always pass even with small budgets
    let srv = start_enforce("10M");
    let c = http_client();

    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/0,0,256,256/256,/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("tile request should succeed");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "tile within budget should return 200"
    );
}

#[test]
fn enforce_full_resolution_within_budget_succeeds() {
    // lena512 is 512x512, 3ch 8bit → ~768KB decode buffer.
    // 10MB budget is plenty.
    let srv = start_enforce("10M");
    let c = http_client();

    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("full resolution request should succeed");

    assert_eq!(resp.status().as_u16(), 200);
}

#[test]
fn enforce_budget_exhaustion_returns_503() {
    // Set budget to 100 bytes — any real image decode will exceed this.
    let srv = start_enforce("100");
    let c = http_client();

    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("request should return response");

    assert_eq!(
        resp.status().as_u16(),
        503,
        "request exceeding tiny budget should return 503"
    );
}

#[test]
fn enforce_503_includes_retry_after_header() {
    let srv = start_enforce("100");
    let c = http_client();

    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("request should return response");

    assert_eq!(resp.status().as_u16(), 503);

    let retry_after = resp
        .headers()
        .get("Retry-After")
        .expect("503 should include Retry-After header");
    assert_eq!(retry_after.to_str().unwrap(), "5");
}

// =============================================================================
// Monitor mode tests
// =============================================================================

#[test]
fn monitor_over_budget_still_returns_200() {
    // Budget of 100 bytes, but monitor mode allows the request through.
    let srv = start_monitor("100");
    let c = http_client();

    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("request should succeed in monitor mode");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "monitor mode should allow requests even when over budget"
    );
}

// =============================================================================
// Off mode tests
// =============================================================================

#[test]
fn off_mode_no_budget_tracking() {
    let srv = start_off();
    let c = http_client();

    // Should work fine with no budget tracking
    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("request should succeed with budget off");

    assert_eq!(resp.status().as_u16(), 200);
}
