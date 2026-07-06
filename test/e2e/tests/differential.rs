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

use std::sync::OnceLock;

use reqwest::Method;
use sipi_e2e::{diff_request, DiffAllowlist, SipiServer};

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
