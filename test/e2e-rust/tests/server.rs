mod common;

use common::{client, server};
use image::GenericImageView;
use std::thread;

// Non-IIIF server tests. IIIF-related tests have been moved to iiif_compliance.rs.

#[test]
fn file_access_allowed() {
    let srv = server();
    let resp = client()
        .get(format!("{}/unit/test.csv/file", srv.base_url))
        .send()
        .expect("GET file access failed");

    assert_eq!(resp.status().as_u16(), 200);
}

#[test]
fn file_access_denied() {
    let srv = server();
    let resp = client()
        .get(format!("{}/unit/test2.csv/file", srv.base_url))
        .send()
        .expect("GET denied file access failed");

    assert_eq!(resp.status().as_u16(), 401);
}

#[test]
fn video_knora_json() {
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/unit/8pdET49BfoJ-EeRcIbgcLch.mp4/knora.json",
            srv.base_url
        ))
        .send()
        .expect("GET video knora.json failed");

    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    assert_eq!(json["internalMimeType"], "video/mp4");
    assert_eq!(
        json["width"].as_f64().expect("width must be number") as i64,
        320
    );
    assert_eq!(
        json["height"].as_f64().expect("height must be number") as i64,
        240
    );
    assert_eq!(json["fps"].as_f64().expect("fps must be number") as i64, 30);
    assert_eq!(json["originalFilename"], "Dummy.mp4");
}

#[test]
fn missing_sidecar_handled_gracefully() {
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/unit/has-missing-sidecar-file.mp4/knora.json",
            srv.base_url
        ))
        .send()
        .expect("GET missing sidecar knora.json failed");

    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    assert_eq!(json["internalMimeType"], "video/mp4");
    assert_eq!(json["fileSize"], 475205);
}

#[test]
fn sqlite_api() {
    let srv = server();
    let resp = client()
        .get(format!("{}/sqlite", srv.base_url))
        .send()
        .expect("GET sqlite failed");

    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    assert_eq!(json["result"]["512"], "Dies ist ein erster Text");
    assert_eq!(
        json["result"]["1024"],
        "Un der zweite Streich folgt sogleich"
    );
}

#[test]
fn lua_test_functions() {
    let srv = server();
    let resp = client()
        .get(format!("{}/test_functions", srv.base_url))
        .send()
        .expect("GET /test_functions failed");

    assert_eq!(resp.status().as_u16(), 200);
}

#[test]
fn lua_mediatype() {
    let srv = server();
    let resp = client()
        .get(format!("{}/test_mediatype", srv.base_url))
        .send()
        .expect("GET /test_mediatype failed");

    assert_eq!(resp.status().as_u16(), 200);
}

#[test]
fn lua_mimetype_func() {
    let srv = server();
    let resp = client()
        .get(format!("{}/test_mimetype_func", srv.base_url))
        .send()
        .expect("GET /test_mimetype_func failed");

    assert_eq!(resp.status().as_u16(), 200);
}

#[test]
fn lua_knora_session_cookie() {
    let srv = server();
    let resp = client()
        .get(format!("{}/test_knora_session_cookie", srv.base_url))
        .send()
        .expect("GET /test_knora_session_cookie failed");

    assert_eq!(resp.status().as_u16(), 200);
}

// --- Phase 9: Ported from Python test_02_server.py ---

#[test]
fn lua_orientation() {
    // Python: test_orientation — exercises image orientation correction via Lua
    let srv = server();
    let resp = client()
        .get(format!("{}/test_orientation", srv.base_url))
        .send()
        .expect("GET /test_orientation failed");

    assert_eq!(resp.status().as_u16(), 200);
}

#[test]
fn lua_exif_gps() {
    // Python: test_exif_gps — exercises EXIF metadata extraction via Lua
    let srv = server();
    let resp = client()
        .get(format!("{}/test_exif_gps", srv.base_url))
        .send()
        .expect("GET /test_exif_gps failed");

    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    // Verify key EXIF fields from the test image
    assert_eq!(json["Make"], "Apple");
    assert_eq!(json["Model"], "iPhone 12 Pro");
    assert_eq!(json["LensMake"], "Apple");
}

#[test]
fn lua_read_write() {
    // Python: test_read_write — exercises image read/write/convert via Lua
    let srv = server();
    let resp = client()
        .get(format!("{}/read_write_lua", srv.base_url))
        .send()
        .expect("GET /read_write_lua failed");

    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    assert_eq!(json["status"], 0);
    assert_eq!(json["message"], "OK");
}

#[test]
#[ignore = "10 concurrent requests saturate 4-thread pool, deadlocking shared server (DEV-6024)"]
fn concurrent_requests() {
    // Python: test_concurrency — verify sipi handles parallel image requests
    let srv = server();
    let base_url = srv.base_url.clone();

    let paths = [
        "/unit/lena512.jp2/full/max/0/default.jpg",
        "/unit/lena512.jp2/full/pct:50/0/default.jpg",
        "/unit/lena512.jp2/full/max/90/default.jpg",
        "/unit/lena512.jp2/pct:10,10,40,40/max/0/default.jpg",
        "/unit/lena512.jp2/square/max/0/default.jpg",
    ];

    let handles: Vec<_> = paths
        .iter()
        .flat_map(|path| {
            let url = format!("{}{}", base_url, path);
            // Send 2 requests per path = 10 concurrent
            (0..2).map(move |_| {
                let url = url.clone();
                thread::spawn(move || {
                    let client = sipi_e2e::http_client();
                    let resp = client.get(&url).send().expect("concurrent GET failed");
                    assert!(
                        resp.status().is_success(),
                        "concurrent request to {} returned {}",
                        url,
                        resp.status()
                    );
                })
            })
        })
        .collect();

    for handle in handles {
        handle.join().expect("thread panicked");
    }
}

#[test]
fn video_knora_json_x_forwarded_proto() {
    // Python: test_knora_json_for_video with X-Forwarded-Proto: https
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/unit/8pdET49BfoJ-EeRcIbgcLch.mp4/knora.json",
            srv.base_url
        ))
        .header("X-Forwarded-Proto", "https")
        .send()
        .expect("GET video knora.json with forwarded proto failed");

    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    let id = json["id"].as_str().expect("id must be string");
    assert!(
        id.starts_with("https://"),
        "video knora.json id should use https:// with X-Forwarded-Proto, got: {}",
        id
    );
}

#[test]
fn video_knora_json_checksums() {
    // Python: test_knora_json_for_video — verify checksum fields from sidecar
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/unit/8pdET49BfoJ-EeRcIbgcLch.mp4/knora.json",
            srv.base_url
        ))
        .send()
        .expect("GET video knora.json failed");

    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    // Sidecar .info file provides checksums
    assert!(
        json["checksumOriginal"].is_string(),
        "checksumOriginal should be present"
    );
    assert!(
        json["checksumDerivative"].is_string(),
        "checksumDerivative should be present"
    );
    assert!(
        (json["duration"].as_f64().expect("duration must be number") - 4.7).abs() < 0.01,
        "duration should be ~4.7"
    );
}

// --- Phase 11: Lua Scripting Integration ---

#[test]
fn knora_json_image_required_fields() {
    // knora.json for images: verify all required fields from SipiHttpServer.cpp:880-1062
    let srv = server();
    let json: serde_json::Value = client()
        .get(format!("{}/unit/lena512.jp2/knora.json", srv.base_url))
        .send()
        .expect("GET image knora.json failed")
        .json()
        .expect("invalid JSON");

    assert_eq!(json["@context"], "http://sipi.io/api/file/3/context.json");
    assert!(json["id"].is_string(), "id must be string");
    assert!(json["width"].is_number(), "width must be number");
    assert!(json["height"].is_number(), "height must be number");
    assert!(
        json["internalMimeType"].is_string(),
        "internalMimeType must be string"
    );
}

#[test]
fn knora_json_image_dimensions() {
    let srv = server();
    let json: serde_json::Value = client()
        .get(format!("{}/unit/lena512.jp2/knora.json", srv.base_url))
        .send()
        .expect("GET image knora.json failed")
        .json()
        .expect("invalid JSON");

    assert_eq!(json["width"], 512);
    assert_eq!(json["height"], 512);
    assert_eq!(json["internalMimeType"], "image/jp2");
}

#[test]
fn knora_json_nonexistent_file() {
    let srv = server();
    let resp = client()
        .get(format!("{}/unit/no-such-file.jp2/knora.json", srv.base_url))
        .send()
        .expect("GET nonexistent knora.json failed");

    assert!(
        resp.status().as_u16() == 404 || resp.status().as_u16() == 500,
        "nonexistent file knora.json should return 404 or 500, got {}",
        resp.status()
    );
}

#[test]
fn knora_json_csv_file() {
    // Non-image file: should return internalMimeType and fileSize
    let srv = server();
    let json: serde_json::Value = client()
        .get(format!("{}/unit/test.csv/knora.json", srv.base_url))
        .send()
        .expect("GET csv knora.json failed")
        .json()
        .expect("invalid JSON");

    assert_eq!(json["@context"], "http://sipi.io/api/file/3/context.json");
    assert!(
        json["internalMimeType"].is_string(),
        "internalMimeType must be string"
    );
    assert!(json["fileSize"].is_number(), "fileSize must be number");
}

// =============================================================================
// Phase 3: Sipi Extension Gap Tests
// =============================================================================

#[test]
fn prometheus_metrics() {
    let srv = server();
    let resp = client()
        .get(format!("{}/metrics", srv.base_url))
        .send()
        .expect("GET /metrics failed");

    assert_eq!(resp.status().as_u16(), 200);

    let body = resp.text().expect("read metrics body");

    // Verify Prometheus text format
    assert!(
        body.contains("# TYPE") || body.contains("# HELP"),
        "/metrics should return Prometheus text format"
    );

    // Verify key cache gauges exist
    assert!(
        body.contains("sipi_cache_hits_total"),
        "should contain sipi_cache_hits_total"
    );
    assert!(
        body.contains("sipi_cache_misses_total"),
        "should contain sipi_cache_misses_total"
    );
    assert!(
        body.contains("sipi_cache_size_bytes"),
        "should contain sipi_cache_size_bytes"
    );
    assert!(
        body.contains("sipi_cache_files"),
        "should contain sipi_cache_files"
    );
}

#[test]
fn ssl_endpoints() {
    let srv = server();
    let resp = client()
        .get(format!("{}/unit/lena512.jp2/info.json", srv.ssl_base_url))
        .send()
        .expect("GET HTTPS info.json failed");

    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    let id = json["id"].as_str().expect("id must be string");
    assert!(
        id.starts_with("https://"),
        "info.json id should use https:// scheme over SSL, got: {}",
        id
    );
}

#[test]
fn mime_consistency() {
    let srv = server();

    // Upload a JPEG file to the mimetest endpoint
    let file_path = sipi_e2e::test_data_dir()
        .join("images")
        .join("unit")
        .join("MaoriFigure.jpg");
    let file_bytes = std::fs::read(&file_path).expect("read test JPEG");
    let part = reqwest::blocking::multipart::Part::bytes(file_bytes)
        .file_name("MaoriFigure.jpg")
        .mime_str("image/jpeg")
        .expect("set mime");
    let form = reqwest::blocking::multipart::Form::new().part("file", part);

    let resp = client()
        .post(format!("{}/api/mimetest", srv.base_url))
        .multipart(form)
        .send()
        .expect("POST /api/mimetest failed");

    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");
    assert_eq!(
        json["consistency"], true,
        "JPEG file with image/jpeg mimetype should be consistent"
    );
}

#[test]
fn thumbnail_generation() {
    let srv = server();

    // Upload a TIFF file to make_thumbnail
    let file_path = sipi_e2e::test_data_dir()
        .join("images")
        .join("unit")
        .join("lena512.tif");
    let file_bytes = std::fs::read(&file_path).expect("read test TIFF");
    let part = reqwest::blocking::multipart::Part::bytes(file_bytes)
        .file_name("lena512.tif")
        .mime_str("image/tiff")
        .expect("set mime");
    let form = reqwest::blocking::multipart::Form::new().part("file", part);

    let resp = client()
        .post(format!("{}/make_thumbnail", srv.base_url))
        .multipart(form)
        .send()
        .expect("POST /make_thumbnail failed");

    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");

    // Thumbnail should be created with dimensions <= 128x128
    let nx = json["nx_thumb"].as_i64().expect("nx_thumb must be number");
    let ny = json["ny_thumb"].as_i64().expect("ny_thumb must be number");
    assert!(
        nx <= 128 && ny <= 128,
        "thumbnail should be at most 128x128, got {}x{}",
        nx,
        ny
    );
    assert_eq!(json["mimetype_thumb"], "image/jpeg");
    assert_eq!(json["file_type"], "IMAGE");
}

#[test]
fn restricted_image_reduction() {
    let srv = server();

    // Request via test_restrict prefix — pre-flight returns restrict with thumb_size
    let resp = client()
        .get(format!(
            "{}/test_restrict/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET restricted image failed");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "restricted image should return 200"
    );

    // Decode the image and verify it was reduced to thumbnail size
    let bytes = resp.bytes().expect("read body");
    let img = image::load_from_memory(&bytes).expect("decode restricted image");
    let (w, h) = img.dimensions();
    assert!(
        w <= 128 && h <= 128,
        "restricted image should be at most 128x128, got {}x{}",
        w,
        h
    );
}

#[test]
fn temp_directory_cleanup() {
    let srv = server();
    // The Lua clean_temp_dir script reads imgroot/tmp — ensure it exists
    let tmp_dir = sipi_e2e::test_data_dir().join("images/tmp");
    std::fs::create_dir_all(&tmp_dir).ok();
    let resp = client()
        .get(format!("{}/test_clean_temp_dir", srv.base_url))
        .send()
        .expect("GET /test_clean_temp_dir failed");

    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");
    assert_eq!(json["result"], "OK", "temp cleanup should succeed");
}

#[test]
fn lua_route_error_handling() {
    let srv = server();
    let resp = client()
        .get(format!("{}/test_lua_error", srv.base_url))
        .send()
        .expect("GET /test_lua_error failed");

    // Should return 500, not crash the server
    assert_eq!(
        resp.status().as_u16(),
        500,
        "Lua error should return 500, not crash"
    );

    // Verify server is still responsive after the Lua error
    let health = client()
        .get(format!("{}/unit/lena512.jp2/info.json", srv.base_url))
        .send()
        .expect("health check after Lua error failed");
    assert_eq!(
        health.status().as_u16(),
        200,
        "server should still respond after Lua error"
    );
}

#[test]
fn lua_image_crop_verify() {
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/test_image_ops?op=crop&param=0,0,256,256&file=unit/lena512.jp2",
            srv.base_url
        ))
        .send()
        .expect("GET /test_image_ops crop failed");

    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");

    let result_nx = json["result"]["nx"].as_i64().expect("result.nx");
    let result_ny = json["result"]["ny"].as_i64().expect("result.ny");
    assert_eq!(result_nx, 256, "cropped width should be 256");
    assert_eq!(result_ny, 256, "cropped height should be 256");
}

#[test]
fn lua_image_scale_verify() {
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/test_image_ops?op=scale&param=200,&file=unit/lena512.jp2",
            srv.base_url
        ))
        .send()
        .expect("GET /test_image_ops scale failed");

    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");

    let result_nx = json["result"]["nx"].as_i64().expect("result.nx");
    assert_eq!(result_nx, 200, "scaled width should be 200");
}

#[test]
fn lua_image_rotate_verify() {
    let srv = server();
    let resp = client()
        .get(format!(
            "{}/test_image_ops?op=rotate&param=90&file=unit/lena512.jp2",
            srv.base_url
        ))
        .send()
        .expect("GET /test_image_ops rotate failed");

    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");

    let orig_nx = json["original"]["nx"].as_i64().expect("original.nx");
    let orig_ny = json["original"]["ny"].as_i64().expect("original.ny");
    let result_nx = json["result"]["nx"].as_i64().expect("result.nx");
    let result_ny = json["result"]["ny"].as_i64().expect("result.ny");

    // For a square image, dimensions stay the same after 90° rotation
    // For non-square, width and height should swap
    if orig_nx != orig_ny {
        assert_eq!(result_nx, orig_ny, "90° rotation should swap width/height");
        assert_eq!(result_ny, orig_nx, "90° rotation should swap width/height");
    }
    // For square images (lena512 is 512x512), just verify dimensions are preserved
    assert_eq!(result_nx, orig_ny);
    assert_eq!(result_ny, orig_nx);
}

#[test]
fn lua_jwt_round_trip() {
    let srv = server();
    let resp = client()
        .get(format!("{}/test_jwt_round_trip", srv.base_url))
        .send()
        .expect("GET /test_jwt_round_trip failed");

    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");

    assert!(
        json["token"].is_string(),
        "should return a JWT token string"
    );
    assert_eq!(
        json["round_trip_ok"], true,
        "JWT payload should survive encode/decode round-trip"
    );
    assert_eq!(json["decoded"]["iss"], "sipi-test");
    assert_eq!(json["decoded"]["sub"], "test-user");
    assert_eq!(json["decoded"]["custom_field"], "hello-world");
}

#[test]
fn lua_uuid_round_trip() {
    let srv = server();
    let resp = client()
        .get(format!("{}/test_uuid_round_trip", srv.base_url))
        .send()
        .expect("GET /test_uuid_round_trip failed");

    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");

    assert!(json["uuid"].is_string(), "should return a UUID");
    assert!(json["base62"].is_string(), "should return a base62 string");
    assert_eq!(
        json["round_trip_ok"], true,
        "UUID should survive uuid->base62->uuid round-trip"
    );
    assert_eq!(
        json["uuid"], json["back"],
        "round-tripped UUID should match original"
    );
}

#[test]
fn lua_http_client_error_handling() {
    let srv = server();
    let resp = client()
        .get(format!("{}/test_http_error", srv.base_url))
        .send()
        .expect("GET /test_http_error failed");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "Lua script should handle HTTP error gracefully and return 200"
    );
    let json: serde_json::Value = resp.json().expect("invalid JSON");
    assert_eq!(
        json["http_success"], false,
        "HTTP request to unreachable host should fail"
    );
}

#[test]
fn watermark_applied_via_http() {
    let srv = server();

    // Request normal image (via unit prefix)
    let normal_resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET normal image failed");
    assert_eq!(normal_resp.status().as_u16(), 200);
    let normal_bytes = normal_resp.bytes().expect("read normal body").to_vec();

    // Request watermarked image (via test_watermark prefix)
    let wm_resp = client()
        .get(format!(
            "{}/test_watermark/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET watermarked image failed");
    assert_eq!(wm_resp.status().as_u16(), 200);
    let wm_bytes = wm_resp.bytes().expect("read watermarked body").to_vec();

    // Watermarked image should differ from the normal image
    assert_ne!(
        normal_bytes, wm_bytes,
        "watermarked image should differ from non-watermarked"
    );
}

#[test]
fn restrict_plus_watermark() {
    let srv = server();

    // Request via test_restrict_wm prefix (both size reduction AND watermark)
    let resp = client()
        .get(format!(
            "{}/test_restrict_wm/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET restrict+watermark image failed");

    assert_eq!(resp.status().as_u16(), 200);
    let bytes = resp.bytes().expect("read body");

    // Decode and verify size reduction
    let img = image::load_from_memory(&bytes).expect("decode restrict+watermark image");
    let (w, h) = img.dimensions();
    assert!(
        w <= 128 && h <= 128,
        "restrict+watermark should be at most 128x128, got {}x{}",
        w,
        h
    );
}

#[test]
fn iiif_auth_api() {
    let srv = server();

    // Request /auth/lena512.jp2/info.json without any auth cookie or header.
    // The pre_flight function returns a 'login' type, which causes sipi to return
    // 401 with an IIIF Auth service block in the info.json response.
    let resp = client()
        .get(format!("{}/auth/lena512.jp2/info.json", srv.base_url))
        .send()
        .expect("GET /auth/lena512.jp2/info.json failed");

    assert_eq!(
        resp.status().as_u16(),
        401,
        "IIIF Auth info.json should return 401 without auth credentials"
    );

    let json: serde_json::Value = resp.json().expect("response should be valid JSON");

    // Verify IIIF Image API 3.0 structure
    assert_eq!(json["@context"], "http://iiif.io/api/image/3/context.json");
    assert_eq!(json["type"], "ImageService3");
    assert_eq!(json["protocol"], "http://iiif.io/api/image");
    assert_eq!(json["profile"], "level2");

    // Verify dimensions (lena512.jp2 is 512x512)
    assert_eq!(json["width"], 512);
    assert_eq!(json["height"], 512);

    // Verify IIIF Auth v1 service block is present
    let service = &json["service"];
    assert!(service.is_array(), "service should be an array");
    let auth_service = &service[0];
    assert_eq!(
        auth_service["@context"],
        "http://iiif.io/api/auth/1/context.json"
    );
    assert_eq!(auth_service["profile"], "http://iiif.io/api/auth/1/login");
    assert_eq!(auth_service["header"], "Please Log In");
    assert_eq!(auth_service["label"], "Login to SIPI");
    assert_eq!(auth_service["confirmLabel"], "Login to SIPI");

    // Verify token service nested inside auth service
    let token_service = &auth_service["service"];
    assert!(token_service.is_array(), "token service should be an array");
    assert_eq!(
        token_service[0]["profile"],
        "http://iiif.io/api/auth/1/token"
    );
}

#[test]
fn image_conversion_from_binaries() {
    let srv = server();

    // POST to /convert_from_binaries with source file path
    let file_path = sipi_e2e::test_data_dir()
        .join("images")
        .join("knora")
        .join("Leaves.jpg");

    let params = [
        ("originalfilename", "Leaves.jpg"),
        ("originalmimetype", "image/jpeg"),
        (
            "source",
            file_path.to_str().expect("path should be valid UTF-8"),
        ),
    ];

    let resp = client()
        .post(format!("{}/convert_from_binaries", srv.base_url))
        .form(&params)
        .send()
        .expect("POST /convert_from_binaries failed");

    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");

    // Verify the conversion response contains expected fields
    let filename_full = json["filename_full"]
        .as_str()
        .expect("filename_full missing");
    let filename_thumb = json["filename_thumb"]
        .as_str()
        .expect("filename_thumb missing");

    assert!(
        filename_full.ends_with(".jpx"),
        "full image should be JPEG 2000 (.jpx), got: {}",
        filename_full
    );
    assert!(
        filename_thumb.ends_with(".jpg"),
        "thumbnail should be JPEG (.jpg), got: {}",
        filename_thumb
    );

    assert_eq!(json["mimetype_full"], "image/jp2");
    assert_eq!(json["mimetype_thumb"], "image/jpeg");
    assert!(json["nx_full"].as_u64().unwrap_or(0) > 0, "nx_full > 0");
    assert!(json["ny_full"].as_u64().unwrap_or(0) > 0, "ny_full > 0");
    assert!(json["nx_thumb"].as_u64().unwrap_or(0) > 0, "nx_thumb > 0");
    assert!(json["ny_thumb"].as_u64().unwrap_or(0) > 0, "ny_thumb > 0");
    assert_eq!(json["original_mimetype"], "image/jpeg");
    assert_eq!(json["original_filename"], "Leaves.jpg");
    assert_eq!(json["file_type"], "image");
}

#[test]
fn thumbnail_convert_from_file() {
    let srv = server();

    // Step 1: Upload image to /make_thumbnail
    let file_path = sipi_e2e::test_data_dir()
        .join("images")
        .join("knora")
        .join("Leaves.jpg");
    let file_bytes = std::fs::read(&file_path).expect("read test JPEG");
    let part = reqwest::blocking::multipart::Part::bytes(file_bytes)
        .file_name("Leaves.jpg")
        .mime_str("image/jpeg")
        .expect("set mime");
    let form = reqwest::blocking::multipart::Form::new().part("file", part);

    let resp = client()
        .post(format!("{}/make_thumbnail", srv.base_url))
        .multipart(form)
        .send()
        .expect("POST /make_thumbnail failed");
    assert_eq!(resp.status().as_u16(), 200);
    let json: serde_json::Value = resp.json().expect("invalid JSON");
    let filename = json["filename"].as_str().expect("filename missing");

    // Step 2: Verify thumbnail is accessible
    let resp_thumb = client()
        .get(format!(
            "{}/thumbs/{}.jpg/full/max/0/default.jpg",
            srv.base_url, filename
        ))
        .send()
        .expect("GET thumbnail failed");
    assert_eq!(resp_thumb.status().as_u16(), 200);

    // Step 3: POST to /convert_from_file to create full-size and thumbnail
    let params = [
        ("filename", filename),
        ("originalfilename", "Leaves.jpg"),
        ("originalmimetype", "image/jpeg"),
    ];

    let resp2 = client()
        .post(format!("{}/convert_from_file", srv.base_url))
        .form(&params)
        .send()
        .expect("POST /convert_from_file failed");
    assert_eq!(resp2.status().as_u16(), 200);
    let json2: serde_json::Value = resp2.json().expect("invalid JSON");

    // Step 4: Verify the conversion response contains expected fields
    let filename_full = json2["filename_full"]
        .as_str()
        .expect("filename_full missing");
    let filename_thumb = json2["filename_thumb"]
        .as_str()
        .expect("filename_thumb missing");

    assert!(
        filename_full.ends_with(".jpx"),
        "full image should be .jpx, got: {}",
        filename_full
    );
    assert!(
        filename_thumb.ends_with(".jpg"),
        "thumbnail should be .jpg, got: {}",
        filename_thumb
    );

    assert_eq!(json2["mimetype_full"], "image/jp2");
    assert_eq!(json2["mimetype_thumb"], "image/jpeg");
    assert!(json2["nx_full"].as_u64().unwrap_or(0) > 0, "nx_full > 0");
    assert!(json2["ny_full"].as_u64().unwrap_or(0) > 0, "ny_full > 0");
}

#[test]
#[ignore = "10 concurrent requests saturate 4-thread pool, killing shared server (DEV-6024)"]
fn lua_state_thread_isolation() {
    let srv = server();
    let base_url = srv.base_url.clone();

    // Send parallel requests, each with a unique ID
    let handles: Vec<_> = (0..10)
        .map(|i| {
            let url = format!("{}/test_thread_isolation?id=request_{}", base_url, i);
            thread::spawn(move || {
                let c = sipi_e2e::http_client();
                let resp = c.get(&url).send().expect("GET thread isolation failed");
                assert_eq!(resp.status().as_u16(), 200);
                let json: serde_json::Value = resp.json().expect("invalid JSON");
                json
            })
        })
        .collect();

    let mut all_isolated = true;
    for handle in handles {
        let json = handle.join().expect("thread panicked");
        if json["isolated"] != true {
            all_isolated = false;
            eprintln!(
                "Thread isolation failure: sent {}, got back {}",
                json["request_id"], json["read_back"]
            );
        }
    }

    assert!(
        all_isolated,
        "Lua global variables should be isolated between request threads"
    );
}
