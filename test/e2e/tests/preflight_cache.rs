//! End-to-end guards for the preflight access-cache (`server-rs/preflight_cache.rs`).
//!
//! The cache is opt-in (disabled by default), so these tests start a dedicated
//! server with `--preflight-cache-ttl 2` to exercise it; a second request for the
//! same `(prefix, identifier, credential)` within the TTL is served from the cache.
//! They assert the cache never changes the access decision: a restrict decision
//! keeps its size/watermark, a deny stays denied, and — the security-critical
//! property — a decision made for one credential is never served to another. The
//! cache's hit/miss/TTL mechanics are unit-tested in the module.

use serde_json::json;
use sipi_e2e::jwt::create_jwt;
use sipi_e2e::{http_client, test_data_dir, SipiServer};

/// Matches the `jwt_key` in `config/sipi.e2e-test-config.lua` (shared with
/// `security.rs`); the `auth` prefix validates the HS256 signature against it.
const JWT_SECRET: &str = "UP 4888, nice 4-8-4 steam engine";

/// Start a server with the preflight access-cache enabled (2s TTL). The default is
/// disabled, so the cache is turned on explicitly here — otherwise these tests
/// would pass without ever exercising the cache path.
fn start_cached() -> SipiServer {
    let test_data = test_data_dir();
    SipiServer::start_with_args(
        "config/sipi.e2e-test-config.lua",
        &test_data,
        &["--preflight-cache-ttl", "2"],
    )
}

/// A `restrict` decision (size + watermark) must be applied identically on the
/// cached second request. If the cache dropped the restrict kv, the second
/// (preflight-cache-hit) response would come back full-size / unwatermarked and
/// the bytes would differ.
#[test]
fn restrict_decision_survives_the_cache() {
    let srv = start_cached();
    let c = http_client();
    let url = format!(
        "{}/test_restrict_wm/lena512.jp2/full/max/0/default.jpg",
        srv.base_url
    );

    let first = c.get(&url).send().expect("GET restrict #1 failed");
    assert_eq!(first.status().as_u16(), 200, "first restrict request");
    let first_bytes = first.bytes().expect("read #1 body");

    let second = c.get(&url).send().expect("GET restrict #2 failed");
    assert_eq!(second.status().as_u16(), 200, "second restrict request");
    let second_bytes = second.bytes().expect("read #2 body");

    assert_eq!(
        first_bytes, second_bytes,
        "cached restrict decision must reapply size + watermark identically"
    );
}

/// A `deny` decision stays a 401 across the cached second request (negatives are
/// cached too, and must not decay into an allow).
#[test]
fn deny_decision_survives_the_cache() {
    let srv = start_cached();
    let c = http_client();
    let url = format!("{}/tmp/lena512.jp2/full/max/0/default.jpg", srv.base_url);

    for attempt in 1..=2 {
        let resp = c.get(&url).send().expect("GET deny failed");
        assert_eq!(
            resp.status().as_u16(),
            401,
            "deny must stay 401 (attempt {attempt})"
        );
    }
}

/// The security-critical property: a decision cached for one credential must never
/// be served to a different credential. An authenticated request (200) must not
/// leak its `allow` to an unauthenticated one, and vice versa — the key includes
/// the credential, so anonymous and token requests never share an entry.
#[test]
fn cache_does_not_leak_across_credentials() {
    let srv = start_cached();
    let c = http_client();
    let url = format!("{}/auth/lena512.jp2/full/max/0/default.jpg", srv.base_url);
    let token = create_jwt(&json!({ "allow": true }), JWT_SECRET);

    // Authenticated → allow (200), cached under the token's credential key.
    let authed = c
        .get(&url)
        .header("Authorization", format!("Bearer {token}"))
        .send()
        .expect("GET auth (token) failed");
    assert_eq!(
        authed.status().as_u16(),
        200,
        "token request must be allowed"
    );

    // Anonymous → login (401). If the cache ignored the credential, this would hit
    // the cached 200 and leak access; it must be its own miss → 401.
    let anon = c.get(&url).send().expect("GET auth (anon) failed");
    assert_eq!(
        anon.status().as_u16(),
        401,
        "anonymous request must not inherit the token's allow"
    );

    // And the token request still succeeds on its own key (not clobbered by the anon entry).
    let authed_again = c
        .get(&url)
        .header("Authorization", format!("Bearer {token}"))
        .send()
        .expect("GET auth (token, repeat) failed");
    assert_eq!(
        authed_again.status().as_u16(),
        200,
        "token request must stay allowed on the cached second hit"
    );
}
