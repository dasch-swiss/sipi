mod common;

use common::client;
use sipi_e2e::{http_client, test_data_dir, SipiServer};
use std::collections::HashMap;
use std::sync::OnceLock;
use std::thread;
use std::time::Duration;

/// Cache tests use their own shared server with a minimal config.
/// The full fake-knora config (24 routes) triggers a dispatch bug in the
/// static musl binary; the minimal config (3 routes) avoids this.
static CACHE_SERVER: OnceLock<SipiServer> = OnceLock::new();

fn server() -> &'static SipiServer {
    CACHE_SERVER.get_or_init(|| {
        let test_data = test_data_dir();
        let cache_dir = test_data.join("cache");
        std::fs::create_dir_all(&cache_dir).ok();
        // Clean existing cache files
        if let Ok(entries) = std::fs::read_dir(&cache_dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_file() {
                    let _ = std::fs::remove_file(&path);
                }
            }
        }
        SipiServer::start("config/sipi.cache-test-config.lua", &test_data)
    })
}

/// Parse Prometheus text format into a map of metric name → value.
/// Only handles simple metrics (no histograms/summaries).
fn parse_metrics(body: &str) -> HashMap<String, f64> {
    let mut map = HashMap::new();
    for line in body.lines() {
        if line.starts_with('#') || line.trim().is_empty() {
            continue;
        }
        // Format: metric_name value
        // or: metric_name{labels} value
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() >= 2 {
            let name = if let Some(brace) = parts[0].find('{') {
                &parts[0][..brace]
            } else {
                parts[0]
            };
            if let Ok(val) = parts[1].parse::<f64>() {
                map.insert(name.to_string(), val);
            }
        }
    }
    map
}

/// Fetch metrics from the server and parse them.
fn get_metrics(base_url: &str) -> HashMap<String, f64> {
    let resp = client()
        .get(format!("{}/metrics", base_url))
        .send()
        .expect("GET /metrics failed");
    assert_eq!(resp.status().as_u16(), 200);
    let body = resp.text().expect("read metrics body");
    parse_metrics(&body)
}

/// Counter for unique cache directories across custom server instances.
static CACHE_DIR_COUNTER: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);

/// Start a server with a custom config generated from the given overrides.
/// Each custom server gets its own isolated cache directory to avoid
/// interfering with the shared server or other custom servers.
fn start_server_with_cache_config(cache_size: &str, cache_nfiles: u32) -> SipiServer {
    let test_data = test_data_dir();
    let cache_id = CACHE_DIR_COUNTER.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
    let cache_dir_name = format!("cache_test_{}", cache_id);
    let cache_dir = test_data.join(&cache_dir_name);

    // Create and clean the isolated cache directory
    std::fs::create_dir_all(&cache_dir).expect("create cache test dir");
    if let Ok(entries) = std::fs::read_dir(&cache_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_file() {
                let _ = std::fs::remove_file(&path);
            }
        }
    }

    // Write a custom config file with the isolated cache directory
    let config_content = format!(
        r#"sipi = {{
    port = 1024,
    nthreads = 4,
    jpeg_quality = 60,
    scaling_quality = {{ jpeg = "medium", tiff = "high", png = "high", j2k = "high" }},
    keep_alive = 5,
    max_post_size = '300M',
    imgroot = './images',
    prefix_as_path = true,
    subdir_levels = 0,
    subdir_excludes = {{ "tmp", "thumb" }},
    initscript = './config/sipi.init-knora.lua',
    cache_dir = './{cache_dir_name}',
    cache_size = '{cache_size}',
    cache_nfiles = {cache_nfiles},
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

admin = {{
    user = 'admin',
    password = 'Sipi-Admin'
}}

fileserver = {{
    docroot = './server',
    wwwroute = '/server'
}}

routes = {{
    {{
        method = 'DELETE',
        route = '/api/cache',
        script = 'cache.lua'
    }},
    {{
        method = 'GET',
        route = '/api/cache',
        script = 'cache.lua'
    }},
    {{
        method = 'POST',
        route = '/api/upload',
        script = 'upload.lua'
    }}
}}
"#
    );

    let config_path = test_data
        .join("config")
        .join(format!("sipi.cache-test-{}.lua", cache_id));
    std::fs::write(&config_path, &config_content).expect("write custom config");

    SipiServer::start(
        &format!("config/sipi.cache-test-{}.lua", cache_id),
        &test_data,
    )
}

// =============================================================================
// Tests using the default shared server (cache_size=20M, cache_nfiles=8)
// =============================================================================

#[test]
fn cache_metrics() {
    let srv = server();

    // Get initial metrics
    let before = get_metrics(&srv.base_url);
    let hits_before = before.get("sipi_cache_hits_total").copied().unwrap_or(0.0);
    let misses_before = before
        .get("sipi_cache_misses_total")
        .copied()
        .unwrap_or(0.0);

    // Make a request that should be a cache miss (first request for this path)
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET image failed");
    assert_eq!(resp.status().as_u16(), 200);
    // Consume the body to complete the request
    let _ = resp.bytes();

    // Check metrics changed
    let after = get_metrics(&srv.base_url);
    let hits_after = after.get("sipi_cache_hits_total").copied().unwrap_or(0.0);
    let misses_after = after.get("sipi_cache_misses_total").copied().unwrap_or(0.0);

    // Either hits or misses should have increased
    let total_before = hits_before + misses_before;
    let total_after = hits_after + misses_after;
    assert!(
        total_after > total_before,
        "cache counters should increase after image request: before={}, after={}",
        total_before,
        total_after
    );

    // Verify cache gauge metrics exist
    assert!(
        after.contains_key("sipi_cache_size_bytes"),
        "sipi_cache_size_bytes metric should exist"
    );
    assert!(
        after.contains_key("sipi_cache_files"),
        "sipi_cache_files metric should exist"
    );
}

#[test]
fn cache_api_routes() {
    let srv = server();

    // First make a request to populate the cache
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/pct:50/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET image failed");
    assert_eq!(resp.status().as_u16(), 200);
    let _ = resp.bytes();

    // GET /api/cache — requires basic auth
    let resp = client()
        .get(format!("{}/api/cache", srv.base_url))
        .basic_auth("admin", Some("Sipi-Admin"))
        .send()
        .expect("GET /api/cache failed");
    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    assert!(json.is_array(), "/api/cache should return an array");

    // GET without auth should return 401
    let resp = client()
        .get(format!("{}/api/cache", srv.base_url))
        .send()
        .expect("GET /api/cache no-auth failed");
    assert_eq!(
        resp.status().as_u16(),
        401,
        "/api/cache without auth should return 401"
    );

    // DELETE /api/cache (purge) — with auth
    let resp = client()
        .delete(format!("{}/api/cache", srv.base_url))
        .basic_auth("admin", Some("Sipi-Admin"))
        .send()
        .expect("DELETE /api/cache failed");
    assert_eq!(resp.status().as_u16(), 200);

    let json: serde_json::Value = resp.json().expect("invalid JSON");
    assert_eq!(json["status"], "OK");
}

#[test]
fn cache_hit_avoids_decode() {
    let srv = server();

    // First request — cache miss
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/200,/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET image (miss) failed");
    assert_eq!(resp.status().as_u16(), 200);
    let _ = resp.bytes();

    let after_miss = get_metrics(&srv.base_url);
    let misses = after_miss
        .get("sipi_cache_misses_total")
        .copied()
        .unwrap_or(0.0);

    // Second request — should be a cache hit
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/200,/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET image (hit) failed");
    assert_eq!(resp.status().as_u16(), 200);
    let _ = resp.bytes();

    let after_hit = get_metrics(&srv.base_url);
    let hits = after_hit
        .get("sipi_cache_hits_total")
        .copied()
        .unwrap_or(0.0);
    let misses_after = after_hit
        .get("sipi_cache_misses_total")
        .copied()
        .unwrap_or(0.0);

    // Hits should have increased, misses should not
    assert!(
        hits > 0.0,
        "cache_hits_total should be > 0 after second request"
    );
    assert_eq!(
        misses_after, misses,
        "cache_misses_total should not increase on cache hit"
    );
}

#[test]
fn cache_key_isolation() {
    let srv = server();

    // Request the same image with different IIIF parameters.
    // Use parameters that produce visibly different outputs (lena512 is 512x512 square,
    // so "full" and "square" would be identical).
    let paths = [
        "/unit/lena512.jp2/full/max/0/default.jpg",
        "/unit/lena512.jp2/full/max/90/default.jpg",
        "/unit/lena512.jp2/pct:10,10,50,50/max/0/default.jpg",
    ];

    let mut bodies: Vec<Vec<u8>> = Vec::new();
    for path in &paths {
        let resp = client()
            .get(format!("{}{}", srv.base_url, path))
            .send()
            .unwrap_or_else(|e| panic!("GET {} failed: {}", path, e));
        assert_eq!(resp.status().as_u16(), 200, "failed for {}", path);
        bodies.push(resp.bytes().expect("read body").to_vec());
    }

    // Verify different parameters produce different images
    assert_ne!(
        bodies[0], bodies[1],
        "full/max/0 and full/max/90 should produce different images"
    );
    assert_ne!(
        bodies[0], bodies[2],
        "full/max and cropped region should produce different images"
    );

    // Check cache files metric increased
    let metrics = get_metrics(&srv.base_url);
    let cache_files = metrics.get("sipi_cache_files").copied().unwrap_or(0.0);
    assert!(
        cache_files >= 3.0,
        "cache should contain at least 3 files for 3 different params, got {}",
        cache_files
    );
}

#[test]
fn cache_returns_consistent_results() {
    let srv = server();
    let url = format!(
        "{}/unit/lena512.jp2/pct:5,5,90,90/100,/0/default.jpg",
        srv.base_url
    );

    // First request — cache miss (image conversion happens)
    let resp1 = client()
        .get(&url)
        .send()
        .expect("first GET failed");
    assert_eq!(resp1.status().as_u16(), 200);
    let bytes1 = resp1.bytes().expect("read body 1").to_vec();

    // Second request — cache hit (served from cache)
    let resp2 = client()
        .get(&url)
        .send()
        .expect("second GET failed");
    assert_eq!(resp2.status().as_u16(), 200);
    let bytes2 = resp2.bytes().expect("read body 2").to_vec();

    // Both responses must be identical (no cache corruption)
    assert_eq!(
        bytes1.len(),
        bytes2.len(),
        "cache hit has different length than cache miss"
    );
    assert_eq!(
        bytes1, bytes2,
        "cache hit differs from cache miss — possible cache corruption"
    );
}

#[test]
fn watermark_cache_separation() {
    // This test needs the fake-knora config which has the /test_watermark/ Lua route.
    let srv = common::server();

    // Request without watermark (normal access via "unit" prefix)
    let resp_normal = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET normal image failed");
    assert_eq!(resp_normal.status().as_u16(), 200);
    let normal_bytes = resp_normal.bytes().expect("read normal body");

    // Request with watermark (via "test_watermark" prefix which applies
    // watermark_correct.tif through the Lua pre-flight restrict handler)
    let resp_wm = client()
        .get(format!(
            "{}/test_watermark/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET watermarked image failed");
    assert_eq!(resp_wm.status().as_u16(), 200);
    let wm_bytes = resp_wm.bytes().expect("read watermark body");

    // The watermarked image should differ from the normal one
    assert_ne!(
        normal_bytes, wm_bytes,
        "watermarked image should differ from normal image"
    );
}

// =============================================================================
// Tests requiring custom server configurations
// =============================================================================

#[test]
fn cache_disabled_mode() {
    let srv = start_server_with_cache_config("0", 0);

    // Make a request
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET image failed");
    assert_eq!(resp.status().as_u16(), 200);
    let _ = resp.bytes();

    // Check metrics show cache is disabled (0 cache files)
    let metrics = get_metrics(&srv.base_url);
    let cache_files = metrics.get("sipi_cache_files").copied().unwrap_or(-1.0);
    assert_eq!(
        cache_files, 0.0,
        "cache_files should be 0 when cache is disabled"
    );
}

#[test]
fn cache_lru_purge_correctness() {
    // Start with a very small cache: 1M size, 5 files
    let srv = start_server_with_cache_config("1M", 5);

    // Request more images than cache_nfiles allows to trigger eviction
    let paths = [
        "/unit/lena512.jp2/full/max/0/default.jpg",
        "/unit/lena512.jp2/full/max/0/default.png",
        "/unit/lena512.jp2/full/max/0/default.tif",
        "/unit/lena512.jp2/full/max/90/default.jpg",
        "/unit/lena512.jp2/full/max/180/default.jpg",
        "/unit/lena512.jp2/full/max/270/default.jpg",
        "/unit/lena512.jp2/full/pct:50/0/default.jpg",
        "/unit/lena512.jp2/square/max/0/default.jpg",
    ];

    for path in &paths {
        let resp = client()
            .get(format!("{}{}", srv.base_url, path))
            .send()
            .unwrap_or_else(|e| panic!("GET {} failed: {}", path, e));
        assert_eq!(resp.status().as_u16(), 200, "failed for {}", path);
        let _ = resp.bytes();
        // Small delay to allow cache operations to settle
        thread::sleep(Duration::from_millis(50));
    }

    let metrics = get_metrics(&srv.base_url);
    let cache_files = metrics.get("sipi_cache_files").copied().unwrap_or(0.0);
    let evictions = metrics
        .get("sipi_cache_evictions_total")
        .copied()
        .unwrap_or(0.0);

    // Cache files should be at or below the limit (purge to ~80% = 4)
    assert!(
        cache_files <= 5.0,
        "cache_files ({}) should not exceed cache_nfiles limit (5)",
        cache_files
    );

    // Evictions should have occurred since we requested more than the limit
    assert!(
        evictions > 0.0,
        "cache_evictions_total should be > 0 after exceeding cache limit, got {}",
        evictions
    );
}

#[test]
fn cache_nfiles_limit() {
    // Start with cache_nfiles=3
    let srv = start_server_with_cache_config("20M", 3);

    let paths = [
        "/unit/lena512.jp2/full/max/0/default.jpg",
        "/unit/lena512.jp2/full/max/0/default.png",
        "/unit/lena512.jp2/full/max/90/default.jpg",
        "/unit/lena512.jp2/full/pct:50/0/default.jpg",
        "/unit/lena512.jp2/square/max/0/default.jpg",
    ];

    for path in &paths {
        let resp = client()
            .get(format!("{}{}", srv.base_url, path))
            .send()
            .unwrap_or_else(|e| panic!("GET {} failed: {}", path, e));
        assert_eq!(resp.status().as_u16(), 200, "failed for {}", path);
        let _ = resp.bytes();
        thread::sleep(Duration::from_millis(50));
    }

    let metrics = get_metrics(&srv.base_url);
    let cache_files = metrics.get("sipi_cache_files").copied().unwrap_or(0.0);

    assert!(
        cache_files <= 3.0,
        "cache_files ({}) should never exceed cache_nfiles limit (3)",
        cache_files
    );
}

#[test]
fn cache_eviction_during_read() {
    // Start with very small cache to force eviction
    let srv = start_server_with_cache_config("1M", 3);

    // Populate cache with one image
    let resp = client()
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("GET initial image failed");
    assert_eq!(resp.status().as_u16(), 200);
    let initial_bytes = resp.bytes().expect("read initial body");

    // Now flood with different images to trigger eviction while reading the cached one
    let base_url = srv.base_url.clone();
    let reader_handle = {
        let url = format!("{}/unit/lena512.jp2/full/max/0/default.jpg", base_url);
        thread::spawn(move || {
            let c = http_client();
            let resp = c.get(&url).send().expect("GET cached image failed");
            assert!(
                resp.status().is_success(),
                "cached image request should succeed even during eviction"
            );
            resp.bytes().expect("read body").to_vec()
        })
    };

    // Simultaneously request new images to trigger eviction
    let eviction_paths = [
        "/unit/lena512.jp2/full/max/0/default.png",
        "/unit/lena512.jp2/full/max/90/default.jpg",
        "/unit/lena512.jp2/full/pct:50/0/default.jpg",
        "/unit/lena512.jp2/square/max/0/default.jpg",
    ];

    for path in &eviction_paths {
        let resp = client()
            .get(format!("{}{}", srv.base_url, path))
            .send()
            .unwrap_or_else(|e| panic!("GET {} failed: {}", path, e));
        assert_eq!(resp.status().as_u16(), 200, "failed for {}", path);
        let _ = resp.bytes();
    }

    // The reader should complete successfully (no truncated response)
    let read_bytes = reader_handle.join().expect("reader thread panicked");
    assert_eq!(
        initial_bytes.len(),
        read_bytes.len(),
        "response size should be consistent even during eviction"
    );
}
