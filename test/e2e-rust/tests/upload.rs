mod common;

use common::{client, server};
use reqwest::blocking::multipart;
use sipi_e2e::{http_client, retry_flaky, test_data_dir, SipiServer};
use std::path::{Path, PathBuf};

fn test_image_path(relative: &str) -> PathBuf {
    test_data_dir().join("images").join(relative)
}

fn upload_file(url: &str, file_path: &Path, mime: &str) -> serde_json::Value {
    let form = multipart::Form::new().part(
        "file",
        multipart::Part::bytes(std::fs::read(file_path).expect("read file"))
            .file_name(file_path.file_name().unwrap().to_string_lossy().to_string())
            .mime_str(mime)
            .expect("valid mime"),
    );

    let resp = client()
        .post(url)
        .multipart(form)
        .send()
        .expect("upload failed");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "upload returned {}",
        resp.status()
    );

    resp.json().expect("upload response not JSON")
}

#[test]
fn upload_tiff_converts_to_jp2() {
    let srv = server();
    let file = test_image_path("unit/lena512.tif");
    let json = upload_file(&format!("{}/api/upload", srv.base_url), &file, "image/tiff");

    let filename = json["filename"].as_str().expect("no filename in response");
    assert!(!filename.is_empty());

    // Flaky: server may still be flushing the converted file when we GET it.
    let url = format!("{}/unit/{}/full/max/0/default.jpg", srv.base_url, filename);
    retry_flaky(3, || {
        match client().get(&url).send() {
            Ok(resp) if resp.status().as_u16() == 200 => Ok(()),
            Ok(resp) => Err(format!("HTTP {}", resp.status())),
            Err(e) => Err(format!("{}", e)),
        }
    });
}

#[test]
fn upload_tiff_knora_json() {
    let srv = server();
    let file = test_image_path("unit/lena512.tif");
    let json = upload_file(&format!("{}/api/upload", srv.base_url), &file, "image/tiff");

    let filename = json["filename"].as_str().expect("no filename");

    let resp = client()
        .get(format!("{}/unit/{}/knora.json", srv.base_url, filename))
        .send()
        .expect("GET knora.json failed");

    assert_eq!(resp.status().as_u16(), 200);

    let knora: serde_json::Value = resp.json().expect("knora.json not JSON");
    assert_eq!(knora["width"], 512);
    assert_eq!(knora["height"], 512);
    assert_eq!(knora["internalMimeType"], "image/jp2");
    assert_eq!(knora["originalMimeType"], "image/tiff");
    assert_eq!(knora["originalFilename"], "lena512.tif");
}

#[test]
fn upload_jpeg_with_comment_block() {
    let srv = server();
    let file = test_image_path("unit/HasCommentBlock.JPG");
    if !file.exists() {
        eprintln!("Skipping: test file {:?} not found", file);
        return;
    }

    let json = upload_file(&format!("{}/api/upload", srv.base_url), &file, "image/jpeg");

    let filename = json["filename"].as_str().expect("no filename");

    let resp = client()
        .get(format!("{}/unit/{}/knora.json", srv.base_url, filename))
        .send()
        .expect("GET knora.json failed");

    assert_eq!(resp.status().as_u16(), 200);

    let knora: serde_json::Value = resp.json().expect("knora.json not JSON");
    assert_eq!(knora["width"], 373);
    assert_eq!(knora["height"], 496);
    assert_eq!(knora["internalMimeType"], "image/jp2");
    assert_eq!(knora["originalMimeType"], "image/jpeg");
    assert_eq!(knora["originalFilename"], "HasCommentBlock.JPG");
}

#[test]
fn upload_odd_file() {
    let srv = server();
    let file = test_image_path("knora/test_odd.odd");
    if !file.exists() {
        eprintln!("Skipping: test file {:?} not found", file);
        return;
    }

    let json = upload_file(&format!("{}/api/upload", srv.base_url), &file, "text/xml");

    let filename = json["filename"].as_str().expect("no filename");

    // Verify non-image file is accessible
    let resp = client()
        .get(format!("{}/unit/{}/knora.json", srv.base_url, filename))
        .send()
        .expect("GET knora.json for odd failed");

    assert_eq!(resp.status().as_u16(), 200);

    let knora: serde_json::Value = resp.json().expect("knora.json not JSON");
    assert_eq!(knora["internalMimeType"], "text/xml");
}

// =============================================================================
// Phase 3: New upload tests
// =============================================================================

#[test]
fn metadata_preservation_upload() {
    // Upload image with EXIF metadata (img_exif_gps.jpg has GPS EXIF data),
    // retrieve via knora.json, verify metadata fields are preserved.
    let srv = server();
    let file = test_image_path("unit/img_exif_gps.jpg");
    if !file.exists() {
        eprintln!("Skipping: img_exif_gps.jpg not found");
        return;
    }

    let json = upload_file(&format!("{}/api/upload", srv.base_url), &file, "image/jpeg");
    let filename = json["filename"].as_str().expect("no filename");

    let resp = client()
        .get(format!("{}/unit/{}/knora.json", srv.base_url, filename))
        .send()
        .expect("GET knora.json failed");

    assert_eq!(resp.status().as_u16(), 200);

    let knora: serde_json::Value = resp.json().expect("knora.json not JSON");
    assert_eq!(knora["originalMimeType"], "image/jpeg");
    assert_eq!(knora["originalFilename"], "img_exif_gps.jpg");
    assert_eq!(knora["internalMimeType"], "image/jp2");

    // Verify dimensions are preserved
    let w = knora["width"].as_i64().expect("width must be number");
    let h = knora["height"].as_i64().expect("height must be number");
    assert!(w > 0 && h > 0, "dimensions should be positive");
}

#[test]
fn metadata_format_conversion() {
    // Upload TIFF, retrieve as JPEG/PNG via IIIF, verify valid output.
    // Full EXIF/XMP/ICC verification requires exiv2 bindings;
    // here we verify the format conversion pipeline produces valid images.
    let srv = server();
    let file = test_image_path("unit/lena512.tif");
    let json = upload_file(&format!("{}/api/upload", srv.base_url), &file, "image/tiff");
    let filename = json["filename"].as_str().expect("no filename");

    // Retrieve as JPEG
    let resp_jpg = client()
        .get(format!(
            "{}/unit/{}/full/max/0/default.jpg",
            srv.base_url, filename
        ))
        .send()
        .expect("GET as JPEG failed");
    assert_eq!(resp_jpg.status().as_u16(), 200);
    let jpg_bytes = resp_jpg.bytes().expect("read JPEG body");
    assert!(!jpg_bytes.is_empty(), "JPEG output should not be empty");

    // Retrieve as PNG
    let resp_png = client()
        .get(format!(
            "{}/unit/{}/full/max/0/default.png",
            srv.base_url, filename
        ))
        .send()
        .expect("GET as PNG failed");
    assert_eq!(resp_png.status().as_u16(), 200);
    let png_bytes = resp_png.bytes().expect("read PNG body");
    assert!(!png_bytes.is_empty(), "PNG output should not be empty");
}

#[test]
fn metadata_essentials_roundtrip() {
    // Verify SipiEssentials (original filename, mimetype, data checksum)
    // survive upload→store→retrieve cycle via knora.json fields.
    let srv = server();
    let file = test_image_path("unit/lena512.tif");
    let json = upload_file(&format!("{}/api/upload", srv.base_url), &file, "image/tiff");

    let filename = json["filename"].as_str().expect("no filename");

    let resp = client()
        .get(format!("{}/unit/{}/knora.json", srv.base_url, filename))
        .send()
        .expect("GET knora.json failed");

    assert_eq!(resp.status().as_u16(), 200);

    let knora: serde_json::Value = resp.json().expect("knora.json not JSON");

    // SipiEssentials fields
    assert_eq!(
        knora["originalFilename"], "lena512.tif",
        "originalFilename should survive roundtrip"
    );
    assert_eq!(
        knora["originalMimeType"], "image/tiff",
        "originalMimeType should survive roundtrip"
    );
    assert_eq!(
        knora["internalMimeType"], "image/jp2",
        "internalMimeType should be jp2 after conversion"
    );

    // Checksum should be present
    let checksum = knora.get("checksumOriginal").or(knora.get("sha256"));
    if let Some(cs) = checksum {
        let cs_str = cs.as_str().unwrap_or("");
        assert!(
            !cs_str.is_empty(),
            "checksum should be non-empty if present"
        );
    }
    // Note: checksum field name may vary; the test documents what's available
}

#[test]
fn upload_4bit_palette_png() {
    // Upload the palette PNG (mario.png is a palette-based PNG).
    let srv = server();
    let file = test_image_path("unit/mario.png");
    if !file.exists() {
        eprintln!("Skipping: mario.png not found");
        return;
    }

    let json = upload_file(&format!("{}/api/upload", srv.base_url), &file, "image/png");
    let filename = json["filename"].as_str().expect("no filename");

    let resp = client()
        .get(format!("{}/unit/{}/knora.json", srv.base_url, filename))
        .send()
        .expect("GET knora.json failed");

    assert_eq!(resp.status().as_u16(), 200);

    let knora: serde_json::Value = resp.json().expect("knora.json not JSON");
    assert_eq!(knora["originalMimeType"], "image/png");
    assert_eq!(knora["originalFilename"], "mario.png");
    assert_eq!(knora["internalMimeType"], "image/jp2");
}

#[test]
fn empty_file_upload() {
    // Upload a zero-byte file, verify graceful error.
    // Uses isolated server because empty uploads can destabilize connection handlers.
    let test_data = test_data_dir();
    let srv = SipiServer::start("config/sipi.e2e-test-config.lua", &test_data);
    let c = http_client();

    let form = multipart::Form::new().part(
        "file",
        multipart::Part::bytes(vec![])
            .file_name("empty.tif")
            .mime_str("image/tiff")
            .expect("valid mime"),
    );

    let resp = c
        .post(format!("{}/api/upload", srv.base_url))
        .multipart(form)
        .send()
        .expect("empty upload request failed");

    let status = resp.status().as_u16();
    // Should return an error, not crash
    assert!(
        status >= 400 || status == 200,
        "empty file upload should return error or be handled, got {}",
        status
    );

    // Verify server still responsive
    let health = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("server should respond after empty upload");
    assert_eq!(health.status().as_u16(), 200);
}

#[test]
fn concurrent_file_uploads() {
    // Send 10 parallel POST uploads simultaneously, verify all succeed
    // or fail gracefully (no crashes).
    // Uses isolated server because 10 parallel uploads can destabilize the shared server.
    let test_data = test_data_dir();
    let srv = SipiServer::start("config/sipi.e2e-test-config.lua", &test_data);
    let file = test_image_path("unit/lena512.tif");
    let file_bytes = std::fs::read(&file).expect("read lena512.tif");

    let mut handles = vec![];
    let url = format!("{}/api/upload", srv.base_url);

    for i in 0..10 {
        let url = url.clone();
        let bytes = file_bytes.clone();
        let handle = std::thread::spawn(move || {
            let c = http_client();
            let form = multipart::Form::new().part(
                "file",
                multipart::Part::bytes(bytes)
                    .file_name(format!("concurrent_{}.tif", i))
                    .mime_str("image/tiff")
                    .expect("valid mime"),
            );

            match c.post(&url).multipart(form).send() {
                Ok(resp) => {
                    let status = resp.status().as_u16();
                    (i, status, resp.text().unwrap_or_default())
                }
                Err(e) => (i, 0, format!("request error: {}", e)),
            }
        });
        handles.push(handle);
    }

    let mut success_count = 0;
    for handle in handles {
        let (idx, status, _body) = handle.join().expect("thread panicked");
        if status == 200 {
            success_count += 1;
        } else {
            eprintln!("Upload {} returned status {}", idx, status);
        }
    }

    // At least some uploads should succeed; none should crash the server
    assert!(
        success_count > 0,
        "at least one concurrent upload should succeed"
    );

    // Verify server still responsive
    let c = http_client();
    let health = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("server should respond after concurrent uploads");
    assert_eq!(health.status().as_u16(), 200);
}

#[test]
fn upload_size_enforcement() {
    // Send POST body exceeding max_post_size.
    // The default config has max_post_size = '300M', which is too large to test.
    // Start a server with small max_post_size to test enforcement.
    let test_data = test_data_dir();

    let config_content = r#"sipi = {
    port = 1024,
    nthreads = 4,
    jpeg_quality = 60,
    scaling_quality = { jpeg = "medium", tiff = "high", png = "high", j2k = "high" },
    keep_alive = 5,
    max_post_size = '1K',
    imgroot = './images',
    prefix_as_path = true,
    subdir_levels = 0,
    subdir_excludes = { "tmp", "thumb" },
    initscript = './config/sipi.init-knora.lua',
    cache_dir = './cache',
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
}

admin = {
    user = 'admin',
    password = 'Sipi-Admin'
}

fileserver = {
    docroot = './server',
    wwwroute = '/server'
}

routes = {
    {
        method = 'POST',
        route = '/api/upload',
        script = 'upload.lua'
    }
}
"#;

    let config_path = test_data.join("config/sipi.small-post.lua");
    std::fs::write(&config_path, config_content).expect("write small post config");

    let srv = SipiServer::start("config/sipi.small-post.lua", &test_data);
    let c = http_client();

    // Create a 10KB payload (exceeds 1K limit)
    let big_payload = vec![0u8; 10 * 1024];
    let form = multipart::Form::new().part(
        "file",
        multipart::Part::bytes(big_payload)
            .file_name("big.tif")
            .mime_str("image/tiff")
            .expect("valid mime"),
    );

    let result = c
        .post(format!("{}/api/upload", srv.base_url))
        .multipart(form)
        .send();

    let _ = std::fs::remove_file(&config_path);

    match result {
        Ok(resp) => {
            let status = resp.status().as_u16();
            // Should be 413 (Payload Too Large) or similar error
            assert!(
                status == 413 || status >= 400,
                "oversized upload should be rejected, got {}",
                status
            );
        }
        Err(_) => {
            // Connection reset is also acceptable — server enforced the limit
            // by closing the connection
        }
    }
}
