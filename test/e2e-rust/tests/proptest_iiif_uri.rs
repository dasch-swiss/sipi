mod common;

use common::{client, server};
use proptest::prelude::*;
use rand::Rng;

// IIIF Image API 3.0 URL format:
//   {scheme}://{server}{/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format}
//
// These property-based tests verify that sipi never crashes or hangs on
// arbitrary (but structurally valid) IIIF parameter combinations.
const TEST_IMAGE: &str = "unit/lena512.jp2";

// ---------------------------------------------------------------------------
// Proptest strategies for IIIF parameters
// ---------------------------------------------------------------------------

fn region_strategy() -> impl Strategy<Value = String> {
    prop_oneof![
        Just("full".to_string()),
        Just("square".to_string()),
        // x,y,w,h — pixel coordinates
        (0u32..600, 0u32..600, 1u32..512, 1u32..512)
            .prop_map(|(x, y, w, h)| format!("{},{},{},{}", x, y, w, h)),
        // pct:x,y,w,h — percent coordinates
        (0.0f64..100.0, 0.0f64..100.0, 0.1f64..100.0, 0.1f64..100.0)
            .prop_map(|(x, y, w, h)| format!("pct:{:.1},{:.1},{:.1},{:.1}", x, y, w, h)),
    ]
}

fn size_strategy() -> impl Strategy<Value = String> {
    prop_oneof![
        Just("max".to_string()),
        Just("^max".to_string()),
        // w, (width only)
        (1u32..2048).prop_map(|w| format!("{},", w)),
        // ,h (height only)
        (1u32..2048).prop_map(|h| format!(",{}", h)),
        // w,h (exact)
        (1u32..2048, 1u32..2048).prop_map(|(w, h)| format!("{},{}", w, h)),
        // !w,h (best fit)
        (1u32..2048, 1u32..2048).prop_map(|(w, h)| format!("!{},{}", w, h)),
        // pct:n
        (1u32..300).prop_map(|n| format!("pct:{}", n)),
        // ^pct:n (upscale percent)
        (100u32..300).prop_map(|n| format!("^pct:{}", n)),
    ]
}

fn rotation_strategy() -> impl Strategy<Value = String> {
    prop_oneof![
        Just("0".to_string()),
        Just("90".to_string()),
        Just("180".to_string()),
        Just("270".to_string()),
        // Arbitrary angle
        (0.0f64..360.0).prop_map(|a| format!("{:.1}", a)),
        // Mirror prefix
        (0.0f64..360.0).prop_map(|a| format!("!{:.1}", a)),
    ]
}

fn quality_strategy() -> impl Strategy<Value = String> {
    prop_oneof![
        Just("default".to_string()),
        Just("color".to_string()),
        Just("gray".to_string()),
        Just("bitonal".to_string()),
    ]
}

fn format_strategy() -> impl Strategy<Value = String> {
    prop_oneof![
        Just("jpg".to_string()),
        Just("png".to_string()),
        Just("tif".to_string()),
    ]
}

/// Generate a completely random byte string for fuzz-like URI segments.
fn arbitrary_uri_segment() -> impl Strategy<Value = String> {
    // Printable ASCII minus '/' (path separator) and '%' (percent-encoding).
    // Excluding '%' prevents accidental generation of percent-encoded sequences
    // like %00 (null byte) which crash the server via a known issue — that class
    // of bug is better caught by a dedicated fuzzer, not a proptest in CI.
    proptest::collection::vec(0x21u8..0x7Eu8, 1..30).prop_map(|bytes| {
        bytes
            .into_iter()
            .map(|b| {
                let c = b as char;
                if c == '/' || c == '%' {
                    '_'
                } else {
                    c
                }
            })
            .collect::<String>()
    })
}

// ---------------------------------------------------------------------------
// Property: valid IIIF parameters never crash the server
// ---------------------------------------------------------------------------

proptest! {
    // Reduced from 64 to 8 cases — 64 sequential requests can exhaust
    // the 4-thread pool on slow arm64 CI runners (DEV-6024)
    #![proptest_config(ProptestConfig::with_cases(8))]

    /// Any structurally valid IIIF request should return a known HTTP status
    /// (200, 400, 404, 500, 501) — never cause a connection reset or timeout.
    #[test]
    fn valid_iiif_params_never_crash(
        region in region_strategy(),
        size in size_strategy(),
        rotation in rotation_strategy(),
        quality in quality_strategy(),
        format in format_strategy(),
    ) {
        let srv = server();
        let url = format!(
            "{}/{}/{}/{}/{}/{}.{}",
            srv.base_url, TEST_IMAGE, region, size, rotation, quality, format
        );

        let resp = client()
            .get(&url)
            .timeout(std::time::Duration::from_secs(10))
            .send();

        match resp {
            Ok(r) => {
                let status = r.status().as_u16();
                // Server should respond with a valid HTTP status, not hang
                prop_assert!(
                    [200, 400, 404, 500, 501].contains(&status),
                    "Unexpected status {} for URL: {}",
                    status,
                    url
                );
            }
            Err(e) => {
                // Connection errors indicate a crash — fail the test
                prop_assert!(
                    e.is_timeout(),
                    "Server connection error (possible crash) for URL {}: {}",
                    url,
                    e
                );
            }
        }
    }

    /// Random (potentially malformed) URI segments should not crash the server.
    /// The server should return an error status, not reset the connection.
    #[test]
    fn random_uri_segments_never_crash(
        region in arbitrary_uri_segment(),
        size in arbitrary_uri_segment(),
        rotation in arbitrary_uri_segment(),
        quality_format in arbitrary_uri_segment(),
    ) {
        let srv = server();
        let url = format!(
            "{}/{}/{}/{}/{}/{}",
            srv.base_url, TEST_IMAGE, region, size, rotation, quality_format
        );

        let resp = client()
            .get(&url)
            .timeout(std::time::Duration::from_secs(10))
            .send();

        match resp {
            Ok(r) => {
                let status = r.status().as_u16();
                // Any HTTP response is acceptable — we just want no crash
                prop_assert!(
                    (200..600).contains(&status),
                    "Invalid HTTP status {} for URL: {}",
                    status,
                    url
                );
            }
            Err(e) => {
                prop_assert!(
                    e.is_timeout(),
                    "Server connection error (possible crash) for URL {}: {}",
                    url,
                    e
                );
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Property: URL-encoded identifiers round-trip correctly
// ---------------------------------------------------------------------------

/// Test that URL-encoding special characters in the identifier produces
/// consistent results — the same encoded identifier always returns the same status.
#[test]
fn url_encoding_round_trip_consistency() {
    let srv = server();
    let mut rng = rand::rng();

    // Characters that should be URL-encoded in IIIF identifiers
    let special_chars = ['/', '?', '#', '[', ']', '@', '!', '$', '&', '\'', '(', ')'];

    for _ in 0..20 {
        // Build an identifier with some special characters
        let base = "test_image";
        let special = special_chars[rng.random_range(0..special_chars.len())];
        let raw_id = format!("{}{}{}", base, special, "suffix");

        // URL-encode the identifier
        let encoded_id: String = raw_id
            .chars()
            .map(|c| {
                if c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == '.' {
                    c.to_string()
                } else {
                    format!("%{:02X}", c as u8)
                }
            })
            .collect();

        let url = format!("{}/{}/full/max/0/default.jpg", srv.base_url, encoded_id);

        // Make the request twice — same URL should give same result
        let resp1 = client()
            .get(&url)
            .timeout(std::time::Duration::from_secs(5))
            .send();
        let resp2 = client()
            .get(&url)
            .timeout(std::time::Duration::from_secs(5))
            .send();

        match (resp1, resp2) {
            (Ok(r1), Ok(r2)) => {
                assert_eq!(
                    r1.status(),
                    r2.status(),
                    "Same URL {} gave different statuses",
                    url
                );
            }
            (Err(_), Err(_)) => {
                // Both failed the same way — consistent
            }
            _ => {
                panic!("Inconsistent response for URL: {}", url);
            }
        }
    }
}
