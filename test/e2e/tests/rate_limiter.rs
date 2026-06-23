use sipi_e2e::{http_client, test_data_dir, SipiServer};

/// Start a server with rate limiting in enforce mode (500k pixel budget).
fn start_enforce_server() -> SipiServer {
    let test_data = test_data_dir();
    SipiServer::start("config/sipi.rate-limit-test.lua", &test_data)
}

/// Start a server with rate limiting in monitor mode (500k pixel budget).
fn start_monitor_server() -> SipiServer {
    let test_data = test_data_dir();
    SipiServer::start("config/sipi.rate-limit-monitor-test.lua", &test_data)
}

// =============================================================================
// Enforce mode tests
// =============================================================================

#[test]
fn enforce_first_request_within_budget_succeeds() {
    let srv = start_enforce_server();
    let c = http_client();

    // 512x512 = 262144 pixels, within 500k budget
    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("first request should succeed");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "first request within budget should return 200"
    );
}

#[test]
fn enforce_exceeding_budget_returns_429() {
    let srv = start_enforce_server();
    let c = http_client();

    // First request: 512x512 = 262144 pixels (within 500k budget)
    let resp1 = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("first request failed");
    assert_eq!(resp1.status().as_u16(), 200);
    let _ = resp1.bytes(); // consume body

    // Second request: another 262144 pixels → total 524288 > 500k budget
    let resp2 = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("second request failed");

    assert_eq!(
        resp2.status().as_u16(),
        429,
        "request exceeding budget should return 429 Too Many Requests"
    );
}

#[test]
fn enforce_429_includes_retry_after_header() {
    let srv = start_enforce_server();
    let c = http_client();

    // Exhaust budget with two requests
    let resp1 = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("first request failed");
    assert_eq!(resp1.status().as_u16(), 200);
    let _ = resp1.bytes();

    let resp2 = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("second request failed");

    assert_eq!(resp2.status().as_u16(), 429);

    let retry_after = resp2
        .headers()
        .get("Retry-After")
        .expect("429 response should include Retry-After header");

    let retry_seconds: u64 = retry_after
        .to_str()
        .expect("Retry-After should be a string")
        .parse()
        .expect("Retry-After should be a number");

    assert!(
        retry_seconds > 0 && retry_seconds <= 60,
        "Retry-After should be between 1 and 60 seconds, got {}",
        retry_seconds
    );
}

#[test]
fn enforce_small_request_exempt_from_rate_limit() {
    let srv = start_enforce_server();
    let c = http_client();

    // Exhaust budget with a large request
    let resp1 = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("first request failed");
    assert_eq!(resp1.status().as_u16(), 200);
    let _ = resp1.bytes();

    let resp2 = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("second request failed");
    assert_eq!(resp2.status().as_u16(), 429);
    let _ = resp2.bytes();

    // Small request (10x10 = 100 pixels, below 1000 pixel_threshold) should still work.
    // info.json doesn't go through the pixel rate limiter path, so use a tiny image request.
    let resp3 = c
        .get(format!(
            "{}/unit/lena512.jp2/full/10,10/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("small request failed");

    assert_eq!(
        resp3.status().as_u16(),
        200,
        "request below pixel_threshold should be exempt from rate limiting"
    );
}

// =============================================================================
// Monitor mode tests
// =============================================================================

#[test]
fn monitor_over_budget_still_returns_200() {
    let srv = start_monitor_server();
    let c = http_client();

    // First request: 262144 pixels
    let resp1 = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("first request failed");
    assert_eq!(resp1.status().as_u16(), 200);
    let _ = resp1.bytes();

    // Second request: exceeds budget, but monitor mode allows it
    let resp2 = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("second request failed");

    assert_eq!(
        resp2.status().as_u16(),
        200,
        "monitor mode should allow requests even when over budget"
    );
}

// =============================================================================
// Metrics tests
// =============================================================================

#[test]
fn rate_limit_metrics_exposed() {
    let srv = start_enforce_server();
    let c = http_client();

    // Make a request to trigger rate limiter
    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("image request failed");
    assert_eq!(resp.status().as_u16(), 200);
    let _ = resp.bytes();

    // Check metrics endpoint
    let metrics = c
        .get(format!("{}/metrics", srv.base_url))
        .send()
        .expect("metrics request failed")
        .text()
        .unwrap_or_default();

    assert!(
        metrics.contains("sipi_rate_limit_decisions_total"),
        "metrics should include rate_limit_decisions_total"
    );
    assert!(
        metrics.contains("sipi_rate_limit_clients_tracked"),
        "metrics should include rate_limit_clients_tracked"
    );
}
