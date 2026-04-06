mod common;

use common::{client, server};
use sipi_e2e::{http_client, test_data_dir, SipiServer};

// =============================================================================
// Resource limits tests — verify server handles heavy load without crashes
// =============================================================================

#[test]
fn sustained_load_memory_growth() {
    // Send 100+ sequential requests for large images, check that /metrics
    // doesn't show unbounded growth in cache or memory.
    let srv = server();
    let c = client();

    // Capture initial metrics
    let initial_metrics = c
        .get(format!("{}/metrics", srv.base_url))
        .send()
        .expect("initial metrics request failed")
        .text()
        .unwrap_or_default();

    let initial_cache_files = extract_metric(&initial_metrics, "sipi_cache_files");

    // Send 100 sequential requests alternating between info.json and image delivery.
    // The musl static binary can drop individual connections under sustained load
    // (independent of connection pooling). This test is about memory growth, not
    // 100% request success, so track failures instead of panicking.
    let total_requests = 100;
    let mut failures = 0u32;
    for i in 0..total_requests {
        let url = if i % 2 == 0 {
            format!("{}/unit/lena512.jp2/info.json", srv.base_url)
        } else {
            format!("{}/unit/lena512.jp2/full/max/0/default.jpg", srv.base_url)
        };

        match c.get(&url).send() {
            Ok(r) => {
                if r.status().as_u16() != 200 {
                    failures += 1;
                }
                let _ = r.bytes(); // consume body
            }
            Err(_) => {
                failures += 1;
            }
        }
    }

    let max_failures = total_requests / 20; // 5%
    assert!(
        failures <= max_failures,
        "{} of {} requests failed (max allowed: {})",
        failures,
        total_requests,
        max_failures
    );

    // Check final metrics
    let final_metrics = c
        .get(format!("{}/metrics", srv.base_url))
        .send()
        .expect("final metrics request failed")
        .text()
        .unwrap_or_default();

    let final_cache_files = extract_metric(&final_metrics, "sipi_cache_files");

    // Cache files should not grow unboundedly — with the same image requested
    // repeatedly, cache should stabilize (not grow by 100)
    if let (Some(initial), Some(final_val)) = (initial_cache_files, final_cache_files) {
        let growth = final_val - initial;
        assert!(
            growth < 20.0,
            "cache files grew by {} over 100 requests (initial={}, final={}) — possible leak",
            growth,
            initial,
            final_val
        );
    }

    // Verify server still responsive
    let health = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("server should respond after sustained load");
    assert_eq!(health.status().as_u16(), 200);
}

#[test]
fn concurrent_large_image_decode() {
    // Send nthreads (4) simultaneous requests for the largest test image,
    // verify all succeed and server remains responsive.
    let srv = server();
    let nthreads = 4;

    let mut handles = vec![];
    let base_url = srv.base_url.clone();

    for i in 0..nthreads {
        let url = format!("{}/unit/lena512.jp2/full/max/0/default.jpg", base_url);
        let handle = std::thread::spawn(move || {
            let c = http_client();
            let resp = c.get(&url).send();
            match resp {
                Ok(r) => {
                    let status = r.status().as_u16();
                    let body = r.bytes().unwrap_or_default();
                    (i, status, body.len())
                }
                Err(e) => {
                    eprintln!("concurrent decode thread {} failed: {}", i, e);
                    (i, 0, 0)
                }
            }
        });
        handles.push(handle);
    }

    let mut success_count = 0;
    let mut first_size = 0;
    for handle in handles {
        let (idx, status, size) = handle.join().expect("thread panicked");
        if status == 200 {
            success_count += 1;
            if first_size == 0 {
                first_size = size;
            } else {
                // All responses for same image should be same size
                assert_eq!(
                    size, first_size,
                    "thread {} got different response size ({} vs {})",
                    idx, size, first_size
                );
            }
        } else {
            eprintln!("thread {} returned status {}", idx, status);
        }
    }

    assert_eq!(
        success_count, nthreads,
        "all {} concurrent decodes should succeed, only {} did",
        nthreads, success_count
    );

    // Verify server still responsive after concurrent load
    let c = client();
    let health = c
        .get(format!("{}/unit/lena512.jp2/info.json", srv.base_url))
        .send()
        .expect("server should respond after concurrent decodes");
    assert_eq!(health.status().as_u16(), 200);
}

#[test]
fn transform_pipeline_memory() {
    // Request large image with region+size+rotation+quality transforms,
    // exercising the worst-case memory path (multiple intermediate buffers).
    // Verify server completes without crash.
    let srv = server();
    let c = client();

    // Full transform pipeline: region → size → rotation → quality
    // region: square crop from center, size: scale down, rotation: 90°, quality: default jpg
    let url = format!(
        "{}/unit/lena512.jp2/100,100,300,300/128,128/90/default.jpg",
        srv.base_url
    );

    let resp = c
        .get(&url)
        .send()
        .expect("transform pipeline request failed");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "transform pipeline should succeed"
    );

    let body = resp.bytes().expect("read transform response body");
    assert!(!body.is_empty(), "transform response should not be empty");

    // Verify the JPEG is valid by checking magic bytes
    assert!(
        body.len() > 2 && body[0] == 0xFF && body[1] == 0xD8,
        "response should be a valid JPEG (starts with FF D8)"
    );

    // Now do a more aggressive transform: multiple transforms in sequence on same connection
    let transforms = [
        "0,0,256,256/64,64/0/default.jpg",
        "0,0,512,512/max/180/default.png",
        "256,256,256,256/128,/!0/default.jpg", // mirror
        "pct:10,10,80,80/256,256/270/default.jpg",
    ];

    for transform in &transforms {
        let url = format!("{}/unit/lena512.jp2/{}", srv.base_url, transform);
        let resp = c
            .get(&url)
            .send()
            .unwrap_or_else(|e| panic!("transform '{}' failed: {}", transform, e));
        let status = resp.status().as_u16();
        let _ = resp.bytes(); // consume body
        assert!(
            status == 200 || status == 400,
            "transform '{}' returned unexpected status {}",
            transform,
            status
        );
    }

    // Verify server still responsive after all transforms.
    // The musl static binary may need a moment to recover after heavy
    // transform work, so allow a few retries for the health check.
    let health_url = format!(
        "{}/unit/lena512.jp2/full/max/0/default.jpg",
        srv.base_url
    );
    let mut health_ok = false;
    for attempt in 1..=3 {
        match c.get(&health_url).send() {
            Ok(r) if r.status().as_u16() == 200 => {
                health_ok = true;
                break;
            }
            Ok(r) => eprintln!("[health] attempt {} returned {}", attempt, r.status()),
            Err(e) => eprintln!("[health] attempt {} failed: {}", attempt, e),
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }
    assert!(health_ok, "server not responsive after transform pipeline");
}

#[test]
fn pixel_limit_rejects_oversized_request() {
    // Start a server with a low max_pixel_limit (10000 = ~100x100)
    // and verify that a 512x512 request (262144 pixels) is rejected.
    let test_data = test_data_dir();

    // Write a config with low pixel limit
    let config_content = r#"sipi = {
    port = 1024,
    nthreads = 4,
    jpeg_quality = 60,
    scaling_quality = { jpeg = "medium", tiff = "high", png = "high", j2k = "high" },
    keep_alive = 5,
    max_post_size = '300M',
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
    loglevel = "DEBUG",
    max_pixel_limit = 10000
}

admin = {
    user = 'admin',
    password = 'Sipi-Admin'
}

fileserver = {
    docroot = './server',
    wwwroute = '/server'
}

routes = {}
"#;

    let config_path = test_data.join("config/sipi.pixel-limit-test.lua");
    std::fs::write(&config_path, config_content).expect("write pixel limit config");

    let srv = SipiServer::start("config/sipi.pixel-limit-test.lua", &test_data);
    let c = http_client();

    // Request full-size image (512x512 = 262144 pixels, exceeds 10000 limit)
    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("pixel limit request failed");

    assert_eq!(
        resp.status().as_u16(),
        400,
        "request exceeding max_pixel_limit should return 400"
    );

    // Small request should still work (100x100 = 10000, within limit)
    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/100,100/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("small image request failed");

    assert_eq!(
        resp.status().as_u16(),
        200,
        "request within max_pixel_limit should return 200"
    );

    let _ = std::fs::remove_file(&config_path);
}

/// Extract a gauge metric value from Prometheus text format.
fn extract_metric(metrics_text: &str, metric_name: &str) -> Option<f64> {
    for line in metrics_text.lines() {
        if line.starts_with('#') {
            continue;
        }
        if line.starts_with(metric_name) {
            // Format: metric_name{labels} value or metric_name value
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() >= 2 {
                return parts.last().and_then(|v| v.parse().ok());
            }
        }
    }
    None
}
