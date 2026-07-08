//! Differential parity gate — THE strangler parity check: replay every
//! replayable request the e2e suite exercises against BOTH binaries (the Rust
//! shell, subject, `$SIPI_BIN`; and the retained C++ server, reference,
//! `$SIPI_BIN_REF`) and assert they agree modulo the §5 allowlist. A green run
//! over the whole corpus is the definition of "Rust is at parity" — the
//! deploy gate for deleting the C++ transport (plan 02 §1 / §9).
//!
//! Coverage model (back-to-back testing; see plan 02 §7): `CASES` below is the
//! deduped corpus of every idempotent GET/HEAD across `test/e2e/tests/*.rs`.
//! Non-replayable behaviours (uploads/state mutation, raw-TCP byte tests,
//! timing/concurrency, lifecycle/SIGTERM, config-writing, CLI subprocesses,
//! proptest, insta snapshots) stay in their single-server e2e files — the
//! drift guard (`tools/differential_coverage_check`) enforces that every e2e
//! test is either represented here or annotated non-replayable.
//!
//! A `Case` with `gap: Some(reason)` is a known divergence or crash-risk
//! skipped today; `gap: None` is asserted at parity. Shrinking the set of
//! `Some` entries is the measurable definition of cutover progress.
//!
//! Bazel-only (needs `$SIPI_BIN_REF`); run via `just bazel-test-differential`.

use std::net::TcpStream;
use std::sync::OnceLock;

use reqwest::Method;
use sipi_e2e::{diff_get, diff_request, DiffAllowlist, SipiServer};

/// Per-case allowlist profile.
#[derive(Clone, Copy)]
enum Allow {
    /// Transport framing only (plan 02 §5 always-ignore).
    Default,
    /// `X-Forwarded-Proto` present: the Rust shell derives the canonical scheme
    /// from XFP while the C++ transport hardcodes `http://` (§5 #9, intentional).
    /// Mask the host-bearing `id` so that scheme divergence is tolerated.
    Xfp,
    /// `/health`: the version string (Rust `CARGO_PKG_VERSION` vs C++
    /// `SipiVersion`) and the uptime counter differ by design (§5 #2,
    /// observability). Mask both; the `status: ok` shape still asserts.
    Health,
    /// The docroot fileserver's `Last-Modified`: the Rust shell formats it
    /// RFC-1123-correct (`…GMT`, via `httpdate`), while the C++ `file_handler`
    /// uses `strftime("%Z", gmtime)`, whose zone label is platform-dependent
    /// (`UTC` on macOS, `GMT` on Linux) and non-RFC. The instant matches; only
    /// the label differs (the shared C++ engine `/file` path is unaffected, so
    /// this is scoped to the pure-Rust static handler). Mask the header.
    DocrootStatic,
}

impl Allow {
    fn build(self) -> DiffAllowlist {
        match self {
            Allow::Default => DiffAllowlist::default_transport(),
            Allow::Xfp => DiffAllowlist::default_transport().masking_json("/id"),
            Allow::Health => DiffAllowlist::default_transport()
                .masking_json("/version")
                .masking_json("/uptime_seconds"),
            Allow::DocrootStatic => DiffAllowlist::default_transport().ignoring("last-modified"),
        }
    }
}

/// One replayable request in the differential corpus.
struct Case {
    name: &'static str,
    method: Method,
    path: &'static str,
    headers: &'static [(&'static str, &'static str)],
    allow: Allow,
    /// `Some(reason)` = a known divergence or crash-risk skipped today; `None`
    /// = asserted at parity. Removing a `Some` is the unit of cutover progress.
    gap: Option<&'static str>,
}

/// The corpus: every replayable GET/HEAD the e2e suite exercises, deduped by
/// (method, path, headers). Generated from the suite and kept complete by the
/// drift guard.
const CASES: &[Case] = &[
    Case { name: "info_json_has_required_fields", method: Method::GET, path: "/unit/lena512.jp2/info.json", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "info_json_x_forwarded_proto_https", method: Method::GET, path: "/unit/lena512.jp2/info.json", headers: &[("X-Forwarded-Proto", "https")], allow: Allow::Xfp, gap: None },
    Case { name: "cors_info_json_with_origin", method: Method::GET, path: "/unit/lena512.jp2/info.json", headers: &[("Origin", "https://example.org")], allow: Allow::Default, gap: None },
    Case { name: "cors_image_with_origin", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/default.jpg", headers: &[("Origin", "https://example.org")], allow: Allow::Default, gap: None },
    Case { name: "cors_image_without_origin", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "base_uri_redirect", method: Method::GET, path: "/unit/lena512.jp2", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "jsonld_media_type_with_accept", method: Method::GET, path: "/unit/lena512.jp2/info.json", headers: &[("Accept", "application/ld+json")], allow: Allow::Default, gap: None },
    Case { name: "region_square", method: Method::GET, path: "/unit/lena512.jp2/square/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "region_percent", method: Method::GET, path: "/unit/lena512.jp2/pct:10,10,50,50/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "region_pixel", method: Method::GET, path: "/unit/lena512.jp2/0,0,100,100/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "region_pixel_offset", method: Method::GET, path: "/unit/lena512.jp2/50,50,200,200/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "region_beyond_bounds_is_cropped", method: Method::GET, path: "/unit/lena512.jp2/400,400,9999,9999/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "region_start_beyond_image", method: Method::GET, path: "/unit/lena512.jp2/600,600,100,100/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "region_zero_width", method: Method::GET, path: "/unit/lena512.jp2/0,0,0,100/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "region_invalid_syntax", method: Method::GET, path: "/unit/lena512.jp2/invalid/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_by_width", method: Method::GET, path: "/unit/lena512.jp2/full/256,/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_by_height", method: Method::GET, path: "/unit/lena512.jp2/full/,256/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_exact", method: Method::GET, path: "/unit/lena512.jp2/full/200,200/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_best_fit", method: Method::GET, path: "/unit/lena512.jp2/full/!200,200/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_percent", method: Method::GET, path: "/unit/lena512.jp2/full/pct:50/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_upscaling", method: Method::GET, path: "/unit/lena512.jp2/full/^1000,/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_no_upscale_beyond_original", method: Method::GET, path: "/unit/lena512.jp2/full/1000,/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_after_region", method: Method::GET, path: "/unit/lena512.jp2/0,0,200,200/100,/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_invalid_syntax", method: Method::GET, path: "/unit/lena512.jp2/full/invalid/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "mirror_rotation", method: Method::GET, path: "/unit/lena512.jp2/full/max/!0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "rotation_180", method: Method::GET, path: "/unit/lena512.jp2/full/max/180/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "rotation_270", method: Method::GET, path: "/unit/lena512.jp2/full/max/270/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "rotation_arbitrary", method: Method::GET, path: "/unit/lena512.jp2/full/max/45/default.png", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "mirror_plus_180", method: Method::GET, path: "/unit/lena512.jp2/full/max/!180/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "rotation_after_region", method: Method::GET, path: "/unit/lena512.jp2/square/max/90/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "rotation_invalid", method: Method::GET, path: "/unit/lena512.jp2/full/max/abc/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "quality_gray", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/gray.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "quality_color", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/color.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "quality_bitonal", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/bitonal.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "quality_invalid", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/invalid.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "format_png_content_type", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/default.png", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "format_tiff_content_type", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/default.tif", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "format_jp2_content_type", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/default.jp2", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "unsupported_formats_rejected_gif", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/default.gif", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "unsupported_formats_rejected_pdf", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/default.pdf", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "unsupported_formats_rejected_webp", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/default.webp", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "unsupported_formats_rejected_bmp", method: Method::GET, path: "/unit/lena512.jp2/full/max/0/default.bmp", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "id_escaped", method: Method::GET, path: "/unit/test%23image.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "id_escaped_slash_decoded", method: Method::GET, path: "/unit%2Flena512.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "id_random_gives_404", method: Method::GET, path: "/nonexistent-random-id/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "id_incomplete_iiif_url", method: Method::GET, path: "/unit/lena512.jp2/full/max/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "id_malformed_iiif_url", method: Method::GET, path: "/unit/lena512.jp2/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "invalid_iiif_url_empty_identifier", method: Method::GET, path: "/unit//lena512.jp2", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_upscale_max", method: Method::GET, path: "/unit/lena512.jp2/full/^max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_upscale_height", method: Method::GET, path: "/unit/lena512.jp2/full/^,1024/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_upscale_exact", method: Method::GET, path: "/unit/lena512.jp2/full/^1024,1024/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_upscale_confined", method: Method::GET, path: "/unit/lena512.jp2/full/^!1024,1024/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_upscale_percent", method: Method::GET, path: "/unit/lena512.jp2/full/^pct:150/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "region_dimension_verification", method: Method::GET, path: "/unit/lena512.jp2/100,100,100,100/max/0/default.png", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "size_dimension_verification", method: Method::GET, path: "/unit/lena512.jp2/full/256,/0/default.png", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "rotation_dimension_verification", method: Method::GET, path: "/unit/lena512.jp2/0,0,200,100/max/90/default.png", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "operation_ordering", method: Method::GET, path: "/unit/lena512.jp2/0,0,200,100/100,/90/default.png", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "fractional_percent_region", method: Method::GET, path: "/unit/lena512.jp2/pct:0.5,0.5,99.0,99.0/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "id_non_ascii", method: Method::GET, path: "/unit/caf%C3%A9.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "iiif_region_crop", method: Method::GET, path: "/unit/lena512.jp2/0,0,256,256/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "iiif_rotation_90", method: Method::GET, path: "/unit/lena512.jp2/full/max/90/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "deny_unauthorized_image", method: Method::GET, path: "/knora/DenyLeaves.jpg/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "head_iiif_image_empty_body", method: Method::HEAD, path: "/unit/lena512.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "path_traversal_rejected_variant_1", method: Method::GET, path: "/unit/%2E%2E%2F%2E%2E%2Fetc%2Fpasswd/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "path_traversal_rejected_variant_2", method: Method::GET, path: "/unit/..%2F..%2Fetc%2Fpasswd/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "metadata_iiif_pipeline", method: Method::GET, path: "/unit/lena512.jp2/0,0,256,256/128,128/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "double_encoded_url", method: Method::GET, path: "/unit%252Flena512.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: Some("intentional (FINDING, DEV-6700): C++ double-decodes %252F → serves the file (200); the Rust shell decodes once → 404. Rust's single-decode is the safer contract (no double-decoded path separators). Move to the §5 allowlist once confirmed (DEV-6700).") },
    Case { name: "cmyk_through_iiif_pipeline", method: Method::GET, path: "/unit/cmyk.tif/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "cielab_through_iiif_pipeline", method: Method::GET, path: "/unit/cielab.tif/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "progressive_jpeg_input_full_max", method: Method::GET, path: "/unit/MaoriFigure.jpg/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "progressive_jpeg_input_region_size_transform", method: Method::GET, path: "/unit/MaoriFigure.jpg/0,0,200,200/100,100/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "file_access_allowed", method: Method::GET, path: "/unit/test.csv/file", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "file_access_denied", method: Method::GET, path: "/unit/test2.csv/file", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "iiif_file_download_range_first_bytes", method: Method::GET, path: "/unit/test.csv/file", headers: &[("Range", "bytes=0-99")], allow: Allow::Default, gap: None },
    Case { name: "iiif_file_download_range_middle", method: Method::GET, path: "/unit/test.csv/file", headers: &[("Range", "bytes=1000-1999")], allow: Allow::Default, gap: None },
    Case { name: "video_knora_json", method: Method::GET, path: "/unit/8pdET49BfoJ-EeRcIbgcLch.mp4/knora.json", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "missing_sidecar_handled_gracefully", method: Method::GET, path: "/unit/has-missing-sidecar-file.mp4/knora.json", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "sqlite_api", method: Method::GET, path: "/sqlite", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_test_functions", method: Method::GET, path: "/test_functions", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_mediatype", method: Method::GET, path: "/test_mediatype", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_mimetype_func", method: Method::GET, path: "/test_mimetype_func", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_knora_session_cookie", method: Method::GET, path: "/test_knora_session_cookie", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_orientation", method: Method::GET, path: "/test_orientation", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_exif_gps", method: Method::GET, path: "/test_exif_gps", headers: &[], allow: Allow::Default, gap: Some("BUG (Rust shell, DEV-6699): emits a duplicate content-type (application/json + text/html) on this Lua-route response where C++ sends only application/json. Un-gap when fixed.") },
    Case { name: "lua_read_write", method: Method::GET, path: "/read_write_lua", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "video_knora_json_x_forwarded_proto", method: Method::GET, path: "/unit/8pdET49BfoJ-EeRcIbgcLch.mp4/knora.json", headers: &[("X-Forwarded-Proto", "https")], allow: Allow::Xfp, gap: None },
    Case { name: "knora_json_image_required_fields", method: Method::GET, path: "/unit/lena512.jp2/knora.json", headers: &[], allow: Allow::Default, gap: Some("cluster D (DEV-6659 step 7): info.rs defers originalFilename/originalMimeType on the image knora.json path (the video path emits them) — plan 02 §3 cluster D") },
    Case { name: "knora_json_nonexistent_file", method: Method::GET, path: "/unit/no-such-file.jp2/knora.json", headers: &[], allow: Allow::Default, gap: Some("intentional: missing knora.json source → Rust 404, C++ 500 (the e2e test accepts either; 404 is the more correct status).") },
    Case { name: "knora_json_csv_file", method: Method::GET, path: "/unit/test.csv/knora.json", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "prometheus_metrics", method: Method::GET, path: "/metrics", headers: &[], allow: Allow::Default, gap: Some("§5 #1 intentional: /metrics is removed in the Rust shell (OTLP replaces the Prometheus scrape) — C++ serves 200, Rust has no route. Permanent divergence.") },
    Case { name: "restricted_image_reduction", method: Method::GET, path: "/test_restrict/lena512.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_route_error_handling", method: Method::GET, path: "/test_lua_error", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_image_crop_verify", method: Method::GET, path: "/test_image_ops?op=crop&param=0,0,256,256&file=unit/lena512.jp2", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_image_scale_verify", method: Method::GET, path: "/test_image_ops?op=scale&param=200,&file=unit/lena512.jp2", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_image_rotate_verify", method: Method::GET, path: "/test_image_ops?op=rotate&param=90&file=unit/lena512.jp2", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_jwt_round_trip", method: Method::GET, path: "/test_jwt_round_trip", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "lua_uuid_round_trip", method: Method::GET, path: "/test_uuid_round_trip", headers: &[], allow: Allow::Default, gap: Some("non-replayable: the response embeds a freshly-generated random UUID (non-deterministic across the two calls); round_trip_ok is covered by the single-server e2e test.") },
    Case { name: "lua_http_client_error_handling", method: Method::GET, path: "/test_http_error", headers: &[], allow: Allow::Default, gap: Some("non-replayable: the error message embeds a non-deterministic connection-timeout duration (e.g. \"2004 ms\" vs \"2002 ms\"); http_success=false is covered by the single-server e2e test.") },
    Case { name: "watermark_applied_via_http_watermarked", method: Method::GET, path: "/test_watermark/lena512.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "restrict_plus_watermark", method: Method::GET, path: "/test_restrict_wm/lena512.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "iiif_auth_api", method: Method::GET, path: "/auth/lena512.jp2/info.json", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "temp_directory_cleanup", method: Method::GET, path: "/test_clean_temp_dir", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "cache_returns_consistent_results", method: Method::GET, path: "/unit/lena512.jp2/pct:5,5,90,90/100,/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "jwt_expired_token", method: Method::GET, path: "/auth/lena512.jp2/full/max/0/default.jpg", headers: &[("Authorization", "Bearer <HS256-JWT allow:true exp:past>")], allow: Allow::Default, gap: Some("cluster F: read-only preflight crashes on a Bearer token (response-sink DoS, DEV-6670) — plan 02 §8.1") },
    Case { name: "jwt_alg_none_bypass", method: Method::GET, path: "/auth/lena512.jp2/full/max/0/default.jpg", headers: &[("Authorization", "Bearer <alg:none JWT allow:true no-signature>")], allow: Allow::Default, gap: Some("cluster F: read-only preflight crashes on a Bearer token (response-sink DoS, DEV-6670) — plan 02 §8.1") },
    Case { name: "jwt_tampered_payload", method: Method::GET, path: "/auth/lena512.jp2/full/max/0/default.jpg", headers: &[("Authorization", "Bearer <HS256-signed allow:false token with payload swapped to allow:true>")], allow: Allow::Default, gap: Some("cluster F: read-only preflight crashes on a Bearer token (response-sink DoS, DEV-6670) — plan 02 §8.1") },
    Case { name: "crlf_header_injection", method: Method::GET, path: "/unit/lena512%0d%0aX-Injected:%20evil/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "error_no_path_disclosure", method: Method::GET, path: "/unit/nonexistent-file-for-path-test.jp2/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "decompression_bomb_rejection", method: Method::GET, path: "/unit/lena512.jp2/full/^100000,100000/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "bilevel_tiff_info_json", method: Method::GET, path: "/bilevel/bilevel_lzw_miniswhite.tif/info.json", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "bilevel_tiff_full_default_jpg", method: Method::GET, path: "/bilevel/bilevel_lzw_miniswhite.tif/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "bilevel_tiff_region_default_jpg", method: Method::GET, path: "/bilevel/bilevel_roi_test.tif/32,32,64,64/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "bilevel_tiff_minisblack_full_default_jpg", method: Method::GET, path: "/bilevel/bilevel_lzw_minisblack.tif/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "bilevel_tiff_uncompressed_full_default_jpg", method: Method::GET, path: "/bilevel/bilevel_none_miniswhite.tif/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "heritage_jpeg_o_full_default_jpg", method: Method::GET, path: "/jpeg/35-2421d-o.jpg/full/max/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "heritage_jpeg_r_info_json", method: Method::GET, path: "/jpeg/35-2421d-r.jpg/info.json", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "iiif_region_size_jpeg_has_non_trivial_body", method: Method::GET, path: "/unit/lena512.jp2/0,0,256,256/128,/0/default.jpg", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "returns_404_for_missing_file", method: Method::GET, path: "/file-should-be-missing-123", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "head_request_returns_headers", method: Method::HEAD, path: "/unit/lena512.jp2/info.json", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "health_returns_200_with_json", method: Method::GET, path: "/health", headers: &[], allow: Allow::Health, gap: None },
    // /server docroot fileserver (plan 02 step 5). Fixtures are materialised by
    // `differential_corpus_parity` before the loop; both binaries serve the same
    // on-disk file, so size / mtime / content (→ Content-Range / Last-Modified /
    // body) match. The binary branch carries Content-Type / Cache-Control /
    // Pragma / Accept-Ranges / Last-Modified; the 206 branch adds Content-Range +
    // the (replicated) full-path Content-Disposition; `.html` is the hardcoded
    // text/html branch.
    Case { name: "docroot_static_full", method: Method::GET, path: "/server/parity_probe.bin", headers: &[], allow: Allow::DocrootStatic, gap: None },
    Case { name: "docroot_static_range_first_bytes", method: Method::GET, path: "/server/parity_probe.bin", headers: &[("Range", "bytes=0-99")], allow: Allow::DocrootStatic, gap: None },
    Case { name: "docroot_static_open_ended_range", method: Method::GET, path: "/server/parity_probe.bin", headers: &[("Range", "bytes=500-")], allow: Allow::DocrootStatic, gap: None },
    Case { name: "docroot_static_head", method: Method::HEAD, path: "/server/parity_probe.bin", headers: &[], allow: Allow::DocrootStatic, gap: Some("Rust supports HEAD on the docroot fileserver (file headers, no body); C++ registers the file_handler for GET+POST only, so HEAD falls through to the IIIF handler → 303 redirect. Rust's HEAD support is the more correct behaviour.") },
    Case { name: "docroot_static_html", method: Method::GET, path: "/server/parity_probe.html", headers: &[], allow: Allow::Default, gap: None },
    Case { name: "docroot_missing_file_404", method: Method::GET, path: "/server/no-such-docroot-file.bin", headers: &[], allow: Allow::Default, gap: None },
];

/// Materialise the docroot fixtures the `/server` cases replay (created at
/// runtime, not committed, mirroring `range_requests.rs`'s `TestFile`). Both
/// binaries serve from `test_data_dir/server` with the same on-disk file.
fn write_docroot_fixtures() {
    let server_dir = sipi_e2e::test_data_dir().join("server");
    std::fs::create_dir_all(&server_dir).expect("create docroot dir");
    std::fs::write(
        server_dir.join("parity_probe.bin"),
        b"0123456789".repeat(100),
    )
    .expect("write parity_probe.bin");
    std::fs::write(
        server_dir.join("parity_probe.html"),
        b"<!DOCTYPE html><html><head><title>parity</title></head><body><h1>parity probe</h1></body></html>",
    )
    .expect("write parity_probe.html");
}

/// Shared subject+reference pair — the corpus is read-only GETs, so one pair
/// is reused (each `start_pair` spawns two processes). `--test-threads=1`
/// (Bazel macro) keeps the single corpus test serial.
fn pair() -> &'static (SipiServer, SipiServer) {
    static PAIR: OnceLock<(SipiServer, SipiServer)> = OnceLock::new();
    PAIR.get_or_init(|| {
        SipiServer::start_pair(
            "config/sipi.e2e-test-config.lua",
            &sipi_e2e::test_data_dir(),
            &[],
        )
    })
}

/// Replay the whole corpus against both binaries, collecting every divergence
/// into one report (rather than failing on the first), so a CI run shows the
/// complete parity picture. `gap` cases are skipped and logged.
#[test]
fn differential_corpus_parity() {
    write_docroot_fixtures();
    let (subject, reference) = pair();
    let mut failures: Vec<String> = Vec::new();
    let mut skipped: Vec<(&str, &str)> = Vec::new();
    let mut asserted = 0usize;

    for case in CASES {
        if let Some(reason) = case.gap {
            skipped.push((case.name, reason));
            continue;
        }
        asserted += 1;
        // Isolate each case: a request that crashes a server (a real finding)
        // is reported as a failure instead of aborting the whole corpus run.
        let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            diff_request(
                subject,
                reference,
                case.method.clone(),
                case.path,
                case.headers,
                None,
                &case.allow.build(),
            )
        }));
        match outcome {
            Ok(diff) => {
                if !diff.is_parity() {
                    failures.push(format!(
                        "[{}] {} {}\n      {}",
                        case.name,
                        case.method,
                        case.path,
                        diff.divergences().join("\n      ")
                    ));
                }
            }
            Err(_) => failures.push(format!(
                "[{}] {} {} — request PANICKED (connection error / server crash)",
                case.name, case.method, case.path
            )),
        }
    }

    eprintln!(
        "[differential] {asserted} asserted at parity, {} skipped (known divergences / crash-risks)",
        skipped.len()
    );
    for (name, reason) in &skipped {
        eprintln!("[differential]   SKIP {name}: {reason}");
    }

    assert!(
        failures.is_empty(),
        "{} differential parity failure(s) of {asserted} asserted:\n\n{}",
        failures.len(),
        failures.join("\n\n")
    );
}

/// Plan 02 §7.5 M6 pin: the C++ oracle binds `--adminuser` to the misspelled
/// `SIPI_ADMIINUSER` env var (`cli_app.cpp:1822`, a latent typo nobody can
/// intentionally rely on); the Rust shell binds the correct `SIPI_ADMINUSER`.
/// This is a documented divergence, not a bug to fix — no e2e config wires an
/// admin endpoint, so there is no behavioural probe, only this parse-level
/// `--help` pin on each binary's own env-name rendering.
#[test]
fn adminuser_env_name_documented_divergence() {
    let subject = std::process::Command::new(sipi_e2e::sipi_bin_path())
        .args(["server", "--help"])
        .output()
        .expect("run the Rust shell's `server --help`");
    assert!(subject.status.success());
    let subject_help = String::from_utf8_lossy(&subject.stdout);
    assert!(
        subject_help.contains("SIPI_ADMINUSER"),
        "Rust shell must bind the correctly-spelled SIPI_ADMINUSER:\n{subject_help}"
    );
    assert!(
        !subject_help.contains("SIPI_ADMIINUSER"),
        "Rust shell must not replicate the C++ oracle's env-name typo:\n{subject_help}"
    );

    let reference = std::process::Command::new(sipi_e2e::sipi_oracle_bin_path())
        .args(["server", "--help"])
        .output()
        .expect("run the C++ oracle's `server --help`");
    assert!(reference.status.success());
    let reference_help = String::from_utf8_lossy(&reference.stdout);
    assert!(
        reference_help.contains("SIPI_ADMIINUSER"),
        "C++ oracle should still carry its documented env-name typo \
         (cli_app.cpp:1822) — if this fails, the typo was fixed upstream and \
         this divergence pin (plan 02 §7.5 M6) should be revisited:\n{reference_help}"
    );
}

// =============================================================================
// §7.7 flag→probe matrix (plan 02 step 2, A1). Each fn below spins up its own
// `start_pair`/`start_pair_env`/`start_with_args` — unlike the shared corpus
// above (one config for every `Case`), a flag probe needs the flag under
// test to vary per spawn. See plan 02 §7.7 for the P/R/A probe-class
// definitions, the mechanics, and the out-of-scope list.
// =============================================================================

/// Every probe below that enables real caching runs CONCURRENTLY with the
/// corpus's shared `pair()` (a `OnceLock`, alive for the whole test-binary
/// run under the mandated `--test-threads=1`) — both would otherwise default
/// to `config/sipi.e2e-test-config.lua`'s `cache_dir = './cache'`. Two
/// `SipiCache` instances validating/writing the same `.sipicache` index
/// concurrently is a real race (the same hazard `resource_limits.rs`'s
/// `pixel_limit_rejects_oversized_request` isolates against with its own
/// `--cache-dir`). Probes below that don't care about caching instead pass
/// `--cache-size 0` (disables `SipiCache` construction entirely, sidestepping
/// the hazard with no tempdir needed); this helper is only for the few that
/// specifically want a *working* cache under a non-default `--cache-size`.
///
/// Its callers (the A-batches) still point subject and reference at the SAME
/// isolated directory — only isolated from the long-lived shared `pair()`,
/// not from each other. That is deliberately bounded: those tests assert
/// response parity, never cache-directory contents, so two processes racing
/// on `.sipicache` there can't produce a false pass.
fn isolated_cache_dir() -> (tempfile::TempDir, String) {
    let dir = tempfile::tempdir().expect("isolated cache dir");
    let path = dir.path().to_str().expect("utf-8 path").to_owned();
    (dir, path)
}

// ---- Bad-value set ---------------------------------------------------------

/// Assert both binaries refuse to start under `extra_args`. Only the exit
/// status is checked (nonzero) — NOT the error text or exact code, which
/// differ by design (clap vs CLI11; plan 02 §7.7's bad-value-set note).
///
/// Exempt from the cache-isolation convention used elsewhere in this file
/// (no `--cache-size 0` / isolated `--cache-dir`): every bad value here is
/// rejected during CLI/env argument validation, before `sipi_init` — and
/// therefore before any `SipiCache` — ever runs (confirmed empirically: both
/// binaries exit within milliseconds, never touching the shared config's
/// `cache_dir`).
fn assert_both_reject_at_startup(extra_args: &[&str]) {
    for (label, bin) in [
        ("subject(rust)", sipi_e2e::sipi_bin_path()),
        ("reference(c++)", sipi_e2e::sipi_oracle_bin_path()),
    ] {
        let status = std::process::Command::new(&bin)
            .arg("server")
            .arg("--config")
            .arg("config/sipi.e2e-test-config.lua")
            .args(extra_args)
            .current_dir(sipi_e2e::test_data_dir())
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .unwrap_or_else(|e| panic!("failed to spawn {label} with {extra_args:?}: {e}"));
        assert!(
            !status.success(),
            "{label} should reject {extra_args:?} at startup (exited 0)"
        );
    }
}

#[test]
fn bad_serverport_zero_rejected_by_both() {
    assert_both_reject_at_startup(&["--serverport", "0"]);
}

#[test]
fn bad_serverport_out_of_range_rejected_by_both() {
    assert_both_reject_at_startup(&["--serverport", "70000"]);
}

#[test]
fn bad_cache_nfiles_negative_rejected_by_both() {
    assert_both_reject_at_startup(&["--cache-nfiles", "-1"]);
}

// ---- Ordering pins ----------------------------------------------------------

/// §7.7: `--serverport` (CLI) beats `SIPI_SERVERPORT` (env) — "free" per the
/// plan: every `start_pair` spawn already passes `--serverport` explicitly,
/// so setting `SIPI_SERVERPORT` to a different free port and confirming both
/// binaries still listen on the CLI-allocated port (not the env one) is a
/// zero-extra-spawn proof that CLI beats env, on both binaries.
#[test]
fn serverport_cli_beats_env_on_both() {
    let (decoy_http, _decoy_ssl) = sipi_e2e::allocate_ports();
    let (subject, reference) = SipiServer::start_pair_env(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &["--cache-size", "0"],
        &[("SIPI_SERVERPORT", &decoy_http.to_string())],
    );
    // `start_pair_env` already had to bind subject.http_port /
    // reference.http_port for its readiness probe to succeed — had env won,
    // both would be listening on `decoy_http` instead and that probe would
    // have timed out. Confirm the decoy is free as a stronger, explicit check.
    assert!(
        TcpStream::connect(("127.0.0.1", decoy_http)).is_err(),
        "SIPI_SERVERPORT={decoy_http} must NOT be where either binary listens \
         (CLI --serverport must win)"
    );
    drop(subject);
    drop(reference);
}

/// §7.7: the `--cache-dir` / `SIPI_CACHE_DIR` precedence pin — the ONE flag
/// this plan pins the full `config < env < CLI` order on. A throwaway Lua
/// config sets a baseline `cache_dir`; three tiers (via `assert_tier`)
/// request the same derivative and assert it lands under the expected
/// tier's directory on BOTH binaries: config-only, +env (beats config),
/// +env+CLI (beats env).
///
/// Unlike every other probe in this file, subject and reference here
/// deliberately point at the SAME directory per tier — the whole point is
/// proving both binaries resolve the identical override to the identical
/// path, which two different directories couldn't show. Originally run as a
/// concurrent `start_pair` (like everywhere else); that crashed the C++
/// oracle with `std::bad_alloc` on linux-amd64 CI (two live `SipiCache`
/// instances racing on the same `.sipicache` index — a real crash, not a
/// benign duplicate write). `assert_tier` now runs subject and reference
/// SEQUENTIALLY against the shared directory instead.
#[test]
fn cache_dir_precedence_config_env_cli() {
    let base = tempfile::tempdir().expect("tempdir");
    let config_tier_dir = base.path().join("config-tier");
    let env_tier_dir = base.path().join("env-tier");
    let cli_tier_dir = base.path().join("cli-tier");

    // Matches the full field set every OTHER differential-tested Lua config
    // in this suite carries (`sipi.e2e-test-config.lua`,
    // `resource_limits.rs`'s pixel-limit template, `cache.rs`'s template).
    // A trimmed config omitting `initscript`/`ssl_*`/`jwt_secret`/
    // `fileserver` crashed the C++ oracle with `std::bad_alloc` on
    // linux-amd64 CI only, the one time subject and reference also raced on
    // this same `cache_dir` concurrently (see `assert_tier` below, which
    // removes that race) — the two are not confirmed independent causes, but
    // matching the proven config shape costs nothing, so both are fixed.
    let config_content = format!(
        r#"sipi = {{
    port = 1024,
    nthreads = 2,
    jpeg_quality = 60,
    scaling_quality = {{ jpeg = "medium", tiff = "high", png = "high", j2k = "high" }},
    keep_alive = 5,
    max_post_size = '300M',
    imgroot = './images',
    prefix_as_path = true,
    subdir_levels = 0,
    subdir_excludes = {{ "tmp", "thumb" }},
    initscript = './config/sipi.init-knora.lua',
    cache_dir = '{config_dir}',
    cache_size = '20M',
    cache_nfiles = 8,
    scriptdir = './scripts',
    thumb_size = '!128,128',
    tmpdir = '/tmp',
    max_temp_file_age = 86400,
    knora_path = 'localhost',
    knora_port = '3434',
    ssl_port = 1025,
    ssl_certificate = './certificate/certificate.pem',
    ssl_key = './certificate/key.pem',
    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',
    logfile = "sipi.log",
    loglevel = "DEBUG"
}}
admin = {{ user = 'admin', password = 'Sipi-Admin' }}
fileserver = {{ docroot = './server', wwwroute = '/server' }}
routes = {{}}
"#,
        config_dir = config_tier_dir.display(),
    );
    let config_path = base.path().join("sipi.cache-precedence-test.lua");
    std::fs::write(&config_path, &config_content).expect("write precedence config");
    let config_arg = config_path.to_str().expect("utf-8 path");

    // Each tier runs subject then reference SEQUENTIALLY (never concurrently)
    // against the same directory: two live `SipiCache` instances racing on
    // the same `.sipicache` index crashed the C++ oracle with `std::bad_alloc`
    // on linux-amd64 CI when this test ran them as a `start_pair` (both
    // processes alive at once). `assert_tier` fully stops one binary
    // (`Drop` blocks until the process exits) before starting the other.
    assert_tier(config_arg, &[], &[], &config_tier_dir, "config-only");
    assert_tier(
        config_arg,
        &[],
        &[("SIPI_CACHE_DIR", env_tier_dir.to_str().unwrap())],
        &env_tier_dir,
        "env-over-config",
    );
    assert_tier(
        config_arg,
        &["--cache-dir", cli_tier_dir.to_str().unwrap()],
        &[("SIPI_CACHE_DIR", env_tier_dir.to_str().unwrap())],
        &cli_tier_dir,
        "cli-over-env",
    );
}

/// Run `derivative` against subject, then (after subject fully stops)
/// against reference, and assert both got 200 and `expected_dir` ended up
/// populated.
fn assert_tier(
    config_arg: &str,
    extra_args: &[&str],
    extra_env: &[(&str, &str)],
    expected_dir: &std::path::Path,
    label: &str,
) {
    let derivative = "/unit/lena512.jp2/0,0,64,64/max/0/default.jpg";
    let working_dir = sipi_e2e::test_data_dir();
    let c = sipi_e2e::http_client();

    {
        let subject = SipiServer::start_env(config_arg, &working_dir, extra_args, extra_env);
        let status = c
            .get(format!("{}{derivative}", subject.base_url))
            .send()
            .unwrap_or_else(|e| panic!("[{label}] subject request failed: {e}"))
            .status()
            .as_u16();
        assert_eq!(status, 200, "[{label}] subject request");
    }
    {
        let reference =
            SipiServer::start_reference_env(config_arg, &working_dir, extra_args, extra_env);
        let status = c
            .get(format!("{}{derivative}", reference.base_url))
            .send()
            .unwrap_or_else(|e| panic!("[{label}] reference request failed: {e}"))
            .status()
            .as_u16();
        assert_eq!(status, 200, "[{label}] reference request");
    }

    assert_cache_populated(expected_dir, label);
}

/// At least one file exists under `dir` within a short settle window (the
/// cache write may lag a hair behind the HTTP response, §7.7's
/// `cache-nfiles` row note).
fn assert_cache_populated(dir: &std::path::Path, label: &str) {
    let count = poll_cache_file_count(dir, |c| c > 0);
    assert!(
        count > 0,
        "[{label}] expected cache file(s) under {} within 2s",
        dir.display()
    );
}

/// Poll `dir`'s file count until `stop` is satisfied or 2s elapses (a cache
/// write may lag a hair behind the HTTP response, §7.7's `cache-nfiles` row
/// note), returning the last sampled count either way.
fn poll_cache_file_count(dir: &std::path::Path, stop: impl Fn(usize) -> bool) -> usize {
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(2);
    loop {
        let count = std::fs::read_dir(dir)
            .map(|entries| {
                entries
                    .filter_map(Result::ok)
                    .filter(|e| e.path().is_file())
                    .count()
            })
            .unwrap_or(0);
        if stop(count) || std::time::Instant::now() >= deadline {
            return count;
        }
        std::thread::sleep(std::time::Duration::from_millis(50));
    }
}

// ---- P-class per-flag honour probes -----------------------------------------

/// §7.7: `--maxpost` — tightens the existing loose `upload_size_enforcement`
/// assertion (`status == 413 || status >= 400`) to `413` OR a connection
/// reset (the same two outcomes `upload_size_enforcement` already treats as
/// equally valid enforcement — sending a body larger than `max_post_size`
/// can trip the transport before the response is even readable) on both
/// binaries, then confirms `--maxpost 0` means unlimited (the same body, now
/// cleanly accepted) on both. Uses a real fixture (`unit/lena512.tif`, the
/// exact file `upload_tiff_converts_to_jp2` proves converts cleanly) rather
/// than a synthetic byte blob, so the "accepted" case can't fail on invalid
/// image data instead of the size cap.
#[test]
fn maxpost_flag_honoured_on_both() {
    let payload = std::fs::read(sipi_e2e::test_data_dir().join("images/unit/lena512.tif"))
        .expect("read lena512.tif fixture");
    assert!(
        payload.len() > 1024,
        "fixture must exceed the 1K cap under test"
    );

    let post_body = |srv: &SipiServer| -> Result<reqwest::blocking::Response, reqwest::Error> {
        let form = reqwest::blocking::multipart::Form::new().part(
            "file",
            reqwest::blocking::multipart::Part::bytes(payload.clone())
                .file_name("lena512.tif")
                .mime_str("image/tiff")
                .expect("valid mime"),
        );
        sipi_e2e::http_client()
            .post(format!("{}/api/upload", srv.base_url))
            .multipart(form)
            .send()
    };

    // Tier: --maxpost 1K -> 413, or the connection resets mid-body (both
    // equally valid enforcement — see doc comment above), on both binaries.
    {
        let (subject, reference) = SipiServer::start_pair(
            "config/sipi.e2e-test-config.lua",
            &sipi_e2e::test_data_dir(),
            &["--cache-size", "0", "--maxpost", "1K"],
        );
        for (label, srv) in [("subject", &subject), ("reference", &reference)] {
            match post_body(srv) {
                Ok(resp) => assert_eq!(
                    resp.status().as_u16(),
                    413,
                    "[{label}] --maxpost 1K should reject the oversized fixture with 413"
                ),
                Err(e) => assert!(
                    e.is_body() || e.is_request() || e.is_connect(),
                    "[{label}] expected a clean 413 or a connection-level failure, got: {e}"
                ),
            }
        }
    }

    // Tier: --maxpost 0 -> unlimited, same fixture cleanly accepted (200).
    {
        let (subject, reference) = SipiServer::start_pair(
            "config/sipi.e2e-test-config.lua",
            &sipi_e2e::test_data_dir(),
            &["--cache-size", "0", "--maxpost", "0"],
        );
        for (label, srv) in [("subject", &subject), ("reference", &reference)] {
            assert_eq!(
                post_body(srv)
                    .unwrap_or_else(|e| panic!("[{label}] upload request failed: {e}"))
                    .status()
                    .as_u16(),
                200,
                "[{label}] --maxpost 0 (unlimited) should accept the same fixture"
            );
        }
    }
}

/// §7.7: `--max-pixel-limit` — a full-resolution request (512×512 =
/// 262,144px) exceeds a 10,000px limit (4xx on both); an explicit `100,100`
/// (10,000px) output size stays within it (200 on both). NOTE: the pixel-
/// limit pre-check (`serve_image.cpp`'s output pixel-count guard) sizes
/// against the SOURCE image for a `max`/`full` SIZE spec — region cropping
/// happens in a later step it doesn't see — so a pixel-coordinate region
/// like `0,0,64,64/max` is checked against the full 512×512, not the 64×64
/// crop, and would still be rejected. An explicit `<W>,<H>` SIZE spec (the
/// same pattern `resource_limits.rs::pixel_limit_rejects_oversized_request`
/// already relies on) is what's actually pixel-limited to the requested
/// output.
#[test]
fn max_pixel_limit_flag_honoured_on_both() {
    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &["--cache-size", "0", "--max-pixel-limit", "10000"],
    );

    let over = diff_get(
        &subject,
        &reference,
        "/unit/lena512.jp2/full/max/0/default.jpg",
    );
    over.assert_parity();
    assert!(
        over.subject_status.is_client_error(),
        "512x512 (262144px) should exceed a 10000px limit with a 4xx, got {}",
        over.subject_status
    );

    let within = diff_get(
        &subject,
        &reference,
        "/unit/lena512.jp2/full/100,100/0/default.jpg",
    );
    within.assert_parity();
    assert_eq!(within.subject_status.as_u16(), 200);
}

/// §7.7 joint probe: `--max-decode-memory` + `--decode-memory-mode`. NOTE:
/// the plan's suggested "1M" budget is too generous for the lena512 fixture
/// (~768KB decode buffer per `memory_budget.rs`) — empirically it returns
/// 200 on both in enforce mode, which would prove nothing. Using "100" bytes
/// instead (the value `memory_budget.rs`'s own enforce tests rely on for
/// reliable rejection) actually exercises the flag.
#[test]
fn decode_memory_budget_flags_honoured_on_both() {
    let path = "/unit/lena512.jp2/full/max/0/default.jpg";

    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &[
            "--cache-size",
            "0",
            "--max-decode-memory",
            "100",
            "--decode-memory-mode",
            "enforce",
        ],
    );
    let enforced = diff_get(&subject, &reference, path);
    enforced.assert_parity();
    assert_eq!(enforced.subject_status.as_u16(), 503);
    drop((subject, reference));

    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &[
            "--cache-size",
            "0",
            "--max-decode-memory",
            "100",
            "--decode-memory-mode",
            "monitor",
        ],
    );
    let monitored = diff_get(&subject, &reference, path);
    monitored.assert_parity();
    assert_eq!(monitored.subject_status.as_u16(), 200);
}

/// §7.7: `--imgroot` — an alt root containing a fixture at a fresh identifier
/// (not served by the default imgroot) proves the override is actually used,
/// not silently ignored (a 404-vs-404 "parity" would otherwise pass without
/// proving anything).
#[test]
fn imgroot_flag_honoured_on_both() {
    let alt_root = tempfile::tempdir().expect("alt imgroot tempdir");
    std::fs::create_dir_all(alt_root.path().join("unit")).expect("create alt unit dir");
    std::fs::copy(
        sipi_e2e::test_data_dir().join("images/bilevel/bilevel_lzw_miniswhite.tif"),
        alt_root.path().join("unit").join("imgroot-probe.tif"),
    )
    .expect("stage alt-imgroot fixture");

    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &[
            "--cache-size",
            "0",
            "--imgroot",
            alt_root.path().to_str().expect("utf-8 path"),
        ],
    );
    let diff = diff_get(&subject, &reference, "/unit/imgroot-probe.tif/info.json");
    diff.assert_parity();
    assert_eq!(
        diff.subject_status.as_u16(),
        200,
        "the alt-imgroot fixture must actually be served (not silently falling back to the default imgroot)"
    );
}

/// §7.7: `--scriptdir` — an alt scriptdir shadows an already-configured
/// route's script with one returning a distinguishable marker. No `require`
/// in the marker script: overriding `--scriptdir` replaces the Lua `require`
/// path too (`LuaServer::setLuaPath`), so a helper like `send_response.lua`
/// (which lives in the DEFAULT scriptdir) would not resolve — the bare
/// `server.*` API is used instead. For the same reason, `--initscript` is
/// ALSO overridden to a no-op: the shared config's default initscript
/// (`sipi.init-knora.lua`) does `require "get_knora_session"`, which every
/// new Lua VM re-executes (incl. the ones behind the `/health` readiness
/// probe on the C++ oracle), and that require would no longer resolve once
/// `--scriptdir` points elsewhere — confirmed empirically (the oracle failed
/// to ever become ready, spamming "module 'get_knora_session' not found"
/// until the spawn timeout).
#[test]
fn scriptdir_flag_honoured_on_both() {
    let alt_scriptdir = tempfile::tempdir().expect("alt scriptdir tempdir");
    std::fs::write(
        alt_scriptdir.path().join("test_mediatype.lua"),
        "server.sendHeader(\"Content-Type\", \"application/json\")\n\
         server.sendStatus(200)\n\
         server.print('{\"marker\":\"scriptdir-override\"}')\n",
    )
    .expect("write alt script");
    let noop_initscript = alt_scriptdir.path().join("noop_init.lua");
    std::fs::write(&noop_initscript, "-- no-op init script (scriptdir probe)\n")
        .expect("write noop initscript");

    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &[
            "--cache-size",
            "0",
            "--scriptdir",
            alt_scriptdir.path().to_str().expect("utf-8 path"),
            "--initscript",
            noop_initscript.to_str().expect("utf-8 path"),
        ],
    );
    diff_get(&subject, &reference, "/test_mediatype").assert_parity();

    let body: serde_json::Value = sipi_e2e::http_client()
        .get(format!("{}/test_mediatype", subject.base_url))
        .send()
        .expect("test_mediatype request")
        .json()
        .expect("test_mediatype JSON");
    assert_eq!(
        body["marker"], "scriptdir-override",
        "the alt scriptdir's script must run instead of the configured default"
    );
}

/// §7.7: `--docroot` — an alt docroot's `marker.html` must be served at the
/// configured `wwwroute` (`/server`, unchanged) on both binaries.
#[test]
fn docroot_flag_honoured_on_both() {
    let alt_docroot = tempfile::tempdir().expect("alt docroot tempdir");
    std::fs::write(
        alt_docroot.path().join("marker.html"),
        b"<html>docroot-override-marker</html>",
    )
    .expect("write alt docroot fixture");

    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &[
            "--cache-size",
            "0",
            "--docroot",
            alt_docroot.path().to_str().expect("utf-8 path"),
        ],
    );
    let diff = diff_get(&subject, &reference, "/server/marker.html");
    diff.assert_parity();
    assert_eq!(diff.subject_status.as_u16(), 200);
}

/// §7.7: `--wwwroute` — moves the docroot fileserver's URL mount point. The
/// new mount point serves the (unmodified default) docroot's fixture; the
/// old mount point's miss status may legitimately differ between binaries
/// (falls through to the IIIF catch-all differently) — assert per-binary,
/// don't diff it, per the table's note.
#[test]
fn wwwroute_flag_honoured_on_both() {
    write_docroot_fixtures();
    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &["--cache-size", "0", "--wwwroute", "/altroute"],
    );
    let diff = diff_get(&subject, &reference, "/altroute/parity_probe.html");
    diff.assert_parity();
    assert_eq!(diff.subject_status.as_u16(), 200);

    let c = sipi_e2e::http_client();
    for (label, srv) in [("subject", &subject), ("reference", &reference)] {
        let missed = c
            .get(format!("{}/server/parity_probe.html", srv.base_url))
            .send()
            .unwrap_or_else(|e| panic!("{label} old-mount request failed: {e}"));
        assert_ne!(
            missed.status().as_u16(),
            200,
            "[{label}] the old /server mount should no longer serve the docroot once --wwwroute moves it"
        );
    }
}

/// §7.7: `--pathprefix` — the never-tested `prefix_as_path = false` branch
/// (plan 02 §3 P2). An alt imgroot holds the fixture at its ROOT (no `unit/`
/// subdir). With the prefix stripped (`--pathprefix=false`), the resolved
/// path drops "unit" -> `<alt>/<file>` exists -> 200 on both. With the
/// prefix kept (bare `--pathprefix`, forcing true), the resolved path keeps
/// "unit" -> `<alt>/unit/<file>` does not exist -> 404 on both.
///
/// Also confirmed empirically: the C++ CLI11 arm DOES accept
/// `--pathprefix=false` (both binaries start cleanly with it), so no
/// config-driven fallback is needed for the false case.
#[test]
fn pathprefix_flag_honoured_on_both() {
    let alt_root = tempfile::tempdir().expect("alt imgroot tempdir");
    std::fs::copy(
        sipi_e2e::test_data_dir().join("images/unit/lena512.jp2"),
        alt_root.path().join("pathprefix-probe.jp2"),
    )
    .expect("stage alt-imgroot fixture at the root (no unit/ subdir)");
    let alt_root_str = alt_root.path().to_str().expect("utf-8 path");

    {
        let (subject, reference) = SipiServer::start_pair(
            "config/sipi.e2e-test-config.lua",
            &sipi_e2e::test_data_dir(),
            &[
                "--cache-size",
                "0",
                "--imgroot",
                alt_root_str,
                "--pathprefix=false",
            ],
        );
        let diff = diff_get(&subject, &reference, "/unit/pathprefix-probe.jp2/info.json");
        diff.assert_parity();
        assert_eq!(diff.subject_status.as_u16(), 200);
    }
    {
        let (subject, reference) = SipiServer::start_pair(
            "config/sipi.e2e-test-config.lua",
            &sipi_e2e::test_data_dir(),
            &[
                "--cache-size",
                "0",
                "--imgroot",
                alt_root_str,
                "--pathprefix",
            ],
        );
        let diff = diff_get(&subject, &reference, "/unit/pathprefix-probe.jp2/info.json");
        diff.assert_parity();
        assert_eq!(diff.subject_status.as_u16(), 404);
    }
}

/// §7.7: `--cache-nfiles 1` bounds the cache directory to ≤1 file after two
/// distinct derivatives, on both binaries. Subject and reference get their
/// OWN isolated `--cache-dir` (via `start_pair_split`) rather than sharing
/// one — two independent `SipiCache` instances racing on the same
/// `.sipicache` index would make the file count meaningless.
#[test]
fn cache_nfiles_flag_honoured_on_both() {
    let subject_cache = tempfile::tempdir().expect("subject cache dir");
    let reference_cache = tempfile::tempdir().expect("reference cache dir");
    let subject_cache_str = subject_cache
        .path()
        .to_str()
        .expect("utf-8 path")
        .to_owned();
    let reference_cache_str = reference_cache
        .path()
        .to_str()
        .expect("utf-8 path")
        .to_owned();

    let (subject, reference) = SipiServer::start_pair_split(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &["--cache-nfiles", "1", "--cache-dir", &subject_cache_str],
        &["--cache-nfiles", "1", "--cache-dir", &reference_cache_str],
    );

    let c = sipi_e2e::http_client();
    for path in [
        "/unit/lena512.jp2/0,0,64,64/max/0/default.jpg",
        "/unit/lena512.jp2/0,0,128,128/max/0/default.jpg",
    ] {
        assert_eq!(
            c.get(format!("{}{path}", subject.base_url))
                .send()
                .unwrap()
                .status()
                .as_u16(),
            200
        );
        assert_eq!(
            c.get(format!("{}{path}", reference.base_url))
                .send()
                .unwrap()
                .status()
                .as_u16(),
            200
        );
    }

    for (label, dir) in [
        ("subject", subject_cache.path()),
        ("reference", reference_cache.path()),
    ] {
        let count = poll_cache_file_count(dir, |c| c <= 1);
        assert!(
            count <= 1,
            "[{label}] --cache-nfiles 1 should bound the cache directory to \u{2264}1 file, found {count}"
        );
    }
}

/// §7.7 joint probe: the four `--rate-limit-*` flags together. `Retry-After`
/// presence only is asserted — the exact seconds-remaining is
/// timing-dependent and deliberately unpinned (§3 P3).
#[test]
fn rate_limit_flags_honoured_on_both() {
    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &[
            "--cache-size",
            "0",
            "--rate-limit-mode",
            "enforce",
            "--rate-limit-max-pixels",
            "300000", // just above one 512x512 request's 262144px
            "--rate-limit-window",
            "60",
            "--rate-limit-pixel-threshold",
            "0",
        ],
    );
    let path = "/unit/lena512.jp2/full/max/0/default.jpg";
    let c = sipi_e2e::http_client();

    for (label, srv) in [("subject", &subject), ("reference", &reference)] {
        let first = c
            .get(format!("{}{path}", srv.base_url))
            .send()
            .expect("first request");
        assert_eq!(
            first.status().as_u16(),
            200,
            "[{label}] first request within budget"
        );

        let second = c
            .get(format!("{}{path}", srv.base_url))
            .send()
            .expect("second request");
        assert_eq!(
            second.status().as_u16(),
            429,
            "[{label}] second request exceeds budget"
        );
        assert!(
            second.headers().contains_key("retry-after"),
            "[{label}] 429 should include a Retry-After header"
        );
    }
}

// ---- P-conditional probes ----------------------------------------------------

/// §7.7 P-conditional: `--thumbsize`/`--knorapath`/`--knoraport` have no
/// direct HTTP-visible effect, but `sipiConfGlobals` (`cli_app.cpp`) exposes
/// them to every Lua VM as `config.thumb_size`/`config.knora_path`/
/// `config.knora_port` — the shared `/config_echo` route
/// (`test/_test_data/scripts/config_echo.lua`) echoes them as JSON.
#[test]
fn config_echo_flags_honoured_on_both() {
    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &[
            "--cache-size",
            "0",
            "--thumbsize",
            "!256,256",
            "--knorapath",
            "example.org",
            "--knoraport",
            "9999",
        ],
    );
    diff_get(&subject, &reference, "/config_echo").assert_parity();

    // `diff_get` alone would also pass if BOTH binaries silently ignored the
    // overrides (agreeing on the unmodified default) — confirm the override
    // actually took by checking the subject's own echoed values.
    let body: serde_json::Value = sipi_e2e::http_client()
        .get(format!("{}/config_echo", subject.base_url))
        .send()
        .expect("config_echo request")
        .json()
        .expect("config_echo JSON");
    assert_eq!(body["thumb_size"], "!256,256");
    assert_eq!(body["knora_path"], "example.org");
    assert_eq!(body["knora_port"], "9999");
}

// ---- R-class: positive path only ----------------------------------------------

/// §7.7: `--jwtkey` — positive path ONLY. A valid, non-expired token signed
/// by the alt secret must be accepted on each binary. The negative path
/// (invalid/expired/malformed tokens) crashes the subject today — the
/// preflight response-sink DoS, DEV-6670 — and lands at step 11; do not add
/// it here (the §7.7 trap).
#[test]
fn jwtkey_flag_honoured_positive_path_only() {
    const ALT_SECRET: &str = "alt-jwt-secret-for-plan-02-step-2-probe";
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .expect("system clock")
        .as_secs();
    let claims = serde_json::json!({ "allow": true, "iat": now, "exp": now + 3600 });
    let token = jsonwebtoken::encode(
        &jsonwebtoken::Header::new(jsonwebtoken::Algorithm::HS256),
        &claims,
        &jsonwebtoken::EncodingKey::from_secret(ALT_SECRET.as_bytes()),
    )
    .expect("encode alt-secret JWT");

    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &["--cache-size", "0", "--jwtkey", ALT_SECRET],
    );
    let c = sipi_e2e::http_client();
    for (label, srv) in [("subject", &subject), ("reference", &reference)] {
        let resp = c
            .get(format!(
                "{}/auth/lena512.jp2/full/max/0/default.jpg",
                srv.base_url
            ))
            .header("Authorization", format!("Bearer {token}"))
            .send()
            .unwrap_or_else(|e| panic!("[{label}] request failed: {e}"));
        assert_eq!(
            resp.status().as_u16(),
            200,
            "[{label}] a valid token signed by the --jwtkey alt secret should be accepted"
        );
    }
}

// ---- A-batch: acceptance-only flags --------------------------------------------

/// §7.7 A-batch: every acceptance-only flag safe to share between both
/// binaries, set to a harmless non-default all at once, plus a baseline
/// probe. These flags either have no HTTP-visible effect worth a dedicated
/// probe (no admin endpoint in the e2e config; `--logfile` NYI in the
/// engine; syslog-vs-JSON `--loglevel` output is noise, §5 #2) or model
/// concurrency differently between the two transports
/// (hostname/keepalive/max-waiting/queue-timeout — XFH-derived / tokio-async
/// / semaphore, §5 #6/#9; `--nthreads` — M7-resolved parse-only). `SIPI_*`
/// env bindings are covered separately by `a_batch_env_flags_accepted_on_both`.
#[test]
fn a_batch_paired_flags_accepted_on_both() {
    let (_cache, cache_dir) = isolated_cache_dir();
    let (subject, reference) = SipiServer::start_pair(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &[
            "--cache-dir",
            &cache_dir,
            "--hostname",
            "sipi-a-batch-probe.example.org",
            "--keepalive",
            "7",
            "--max-waiting",
            "3",
            "--queue-timeout",
            "9",
            "--nthreads",
            "3",
            "--adminuser",
            "a-batch-admin",
            "--adminpasswd",
            "a-batch-password",
            "--cache-size",
            "5M",
            "--loglevel",
            "INFO",
            "--logfile",
            "sipi-a-batch-probe.log",
        ],
    );
    diff_get(&subject, &reference, "/unit/lena512.jp2/info.json").assert_parity();
}

/// §7.7 A-batch, env variant: the same flags via their `SIPI_*` env bindings
/// instead of CLI flags — this is what would catch an `ADMIINUSER`-class
/// env-name typo on a newly added A-class flag. (The C++ oracle's documented
/// `SIPI_ADMIINUSER` typo, M6, means `SIPI_ADMINUSER` here is a no-op on the
/// reference — harmless, since no admin endpoint is configured to observe it
/// either way.)
#[test]
fn a_batch_env_flags_accepted_on_both() {
    let (_cache, cache_dir) = isolated_cache_dir();
    let (subject, reference) = SipiServer::start_pair_env(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &["--cache-dir", &cache_dir],
        &[
            ("SIPI_HOSTNAME", "sipi-a-batch-env-probe.example.org"),
            ("SIPI_KEEPALIVE", "7"),
            ("SIPI_MAX_WAITING", "3"),
            ("SIPI_QUEUE_TIMEOUT", "9"),
            ("SIPI_NTHREADS", "3"),
            ("SIPI_ADMINUSER", "a-batch-admin"),
            ("SIPI_ADMINPASSWD", "a-batch-password"),
            ("SIPI_CACHE_SIZE", "5M"),
            ("SIPI_LOGLEVEL", "INFO"),
            ("SIPI_LOGFILE", "sipi-a-batch-env-probe.log"),
        ],
    );
    diff_get(&subject, &reference, "/unit/lena512.jp2/info.json").assert_parity();
}

/// §7.7 A-batch, subject-only: flags either dead-transport on the Rust shell
/// (`--sslcert`/`--sslkey` — TLS terminates at Traefik; `--sslport` is
/// already added automatically by `start_with_args`) or unsafe to probe
/// against the C++ oracle (`--subdirlevels`/`--subdirexcludes` — the
/// oracle's OWN `run_server()` command handler migrates the imgroot's
/// on-disk layout at startup when these change, `cli_app.cpp:1419-1452`;
/// `sipi_init`, which only the Rust shell calls, has no such migration, so
/// this is safe on the subject).
#[test]
fn a_batch_subject_only_flags_accepted() {
    let subject = SipiServer::start_with_args(
        "config/sipi.e2e-test-config.lua",
        &sipi_e2e::test_data_dir(),
        &[
            "--cache-size",
            "0",
            "--sslcert",
            "./certificate/certificate.pem",
            "--sslkey",
            "./certificate/key.pem",
            "--subdirlevels",
            "1",
            "--subdirexcludes",
            "tmp,thumb",
        ],
    );
    let resp = sipi_e2e::http_client()
        .get(format!("{}/unit/lena512.jp2/info.json", subject.base_url))
        .send()
        .expect("baseline probe");
    assert_eq!(resp.status().as_u16(), 200);
}

/// §6 R3: the Rust shell falls back to the Lua config's `sipi.port` when
/// neither `--serverport`/`SIPI_SERVERPORT` nor the dev/test `SIPI_RS_PORT`
/// selects one. Single-binary (subject-only) — this is an internal
/// precedence chain in `server-rs::serve()`, not a flag to compare against
/// the C++ oracle (the oracle has always read its own config's `port`
/// natively via `run_server()`, unaffected by this fix). `SipiServer::spawn`
/// always injects `--serverport`, so this uses a raw spawn instead,
/// mirroring `adminuser_env_name_documented_divergence`'s pattern.
#[test]
fn r3_lua_config_port_is_listener_fallback() {
    // Draws from the same PID-offset counter every other spawn in this file
    // uses, so this literal-port raw spawn can't collide with a concurrently
    // running test binary the way a hardcoded port could.
    let (port, _) = sipi_e2e::allocate_ports();
    let tmp = tempfile::tempdir().expect("tempdir");
    let cache_dir = tmp.path().join("cache");
    let config_content = format!(
        r#"sipi = {{
    port = {port},
    nthreads = 2,
    jpeg_quality = 60,
    scaling_quality = {{ jpeg = "medium", tiff = "high", png = "high", j2k = "high" }},
    keep_alive = 5,
    max_post_size = '300M',
    imgroot = './images',
    prefix_as_path = true,
    subdir_levels = 0,
    subdir_excludes = {{ "tmp", "thumb" }},
    cache_dir = '{cache_dir}',
    cache_size = '5M',
    cache_nfiles = 8,
    scriptdir = './scripts',
    thumb_size = '!128,128',
    tmpdir = '/tmp',
    max_temp_file_age = 86400,
    knora_path = 'localhost',
    knora_port = '3434',
    logfile = "sipi.log",
    loglevel = "DEBUG"
}}
admin = {{ user = 'admin', password = 'Sipi-Admin' }}
routes = {{}}
"#,
        cache_dir = cache_dir.display(),
    );
    let config_path = tmp.path().join("sipi.r3-port-test.lua");
    std::fs::write(&config_path, &config_content).expect("write config");

    let mut child = std::process::Command::new(sipi_e2e::sipi_bin_path())
        .arg("server")
        .arg("--config")
        .arg(&config_path)
        .current_dir(sipi_e2e::test_data_dir())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .expect("spawn subject without --serverport");

    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(10);
    let mut ready = false;
    while std::time::Instant::now() < deadline {
        if sipi_e2e::http_client()
            .get(format!("http://127.0.0.1:{port}/health"))
            .send()
            .map(|r| r.status().is_success())
            .unwrap_or(false)
        {
            ready = true;
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(50));
    }

    child.kill().ok();
    child.wait().ok();

    assert!(
        ready,
        "the Rust shell must listen on the Lua config's `port` ({port}) when no \
         --serverport/SIPI_SERVERPORT/SIPI_RS_PORT overrides it"
    );
}

/// §6 R3, the actual precedence INVERSION: `SIPI_RS_PORT` now wins even over
/// an explicit `--serverport`, not just the implicit Lua-config fallback
/// above. Raw spawn, not `SipiServer` (which always injects `--serverport`
/// itself and polls readiness against that same port — it cannot express
/// "give an explicit --serverport, then watch something ELSE win").
#[test]
fn sipi_rs_port_beats_explicit_serverport() {
    let (explicit_port, _) = sipi_e2e::allocate_ports();
    let (winning_port, _) = sipi_e2e::allocate_ports();

    let mut child = std::process::Command::new(sipi_e2e::sipi_bin_path())
        .arg("server")
        .arg("--config")
        .arg("config/sipi.e2e-test-config.lua")
        .arg("--serverport")
        .arg(explicit_port.to_string())
        .arg("--cache-size")
        .arg("0")
        .env("SIPI_RS_PORT", winning_port.to_string())
        .current_dir(sipi_e2e::test_data_dir())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .expect("spawn subject");

    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(10);
    let mut ready = false;
    while std::time::Instant::now() < deadline {
        if sipi_e2e::http_client()
            .get(format!("http://127.0.0.1:{winning_port}/health"))
            .send()
            .map(|r| r.status().is_success())
            .unwrap_or(false)
        {
            ready = true;
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(50));
    }
    let explicit_port_free = TcpStream::connect(("127.0.0.1", explicit_port)).is_err();

    child.kill().ok();
    child.wait().ok();

    assert!(
        ready,
        "SIPI_RS_PORT ({winning_port}) must win even over an explicit --serverport ({explicit_port})"
    );
    assert!(
        explicit_port_free,
        "the explicit --serverport ({explicit_port}) must NOT be where the shell \
         listens once SIPI_RS_PORT wins"
    );
}
