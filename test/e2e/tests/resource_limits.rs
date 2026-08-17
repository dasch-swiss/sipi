mod common;

use common::{client, server};
use sipi_e2e::{http_client, poll_cache_file_count, test_data_dir};

// =============================================================================
// Resource limits tests — verify server handles heavy load without crashes
// =============================================================================

#[test]
fn sustained_load_memory_growth() {
    // Send 100+ sequential requests for large images and check the cache does not
    // grow unboundedly. Pinned on the on-disk file count under `cache_dir`, per
    // the DEV-6659 repin in `cache.rs`: the shell serves no `/metrics` route, so
    // the original scrape silently yielded no reading and the growth assertion
    // below never ran.
    let srv = server();
    let c = client();
    let cache_dir = test_data_dir().join("cache");

    let initial_cache_files = poll_cache_file_count(&cache_dir, |_| true);

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

    // Cache files should not grow unboundedly — the same two derivatives are
    // requested over and over, so the count stabilises rather than growing by 100.
    let final_cache_files = poll_cache_file_count(&cache_dir, |c| c > 0);
    // Guard the guard: with an empty cache dir the growth check below would pass
    // for the wrong reason, which is how the original `/metrics` version of this
    // test went silently vacuous.
    assert!(
        final_cache_files > 0,
        "100 image requests should have populated the cache at {}",
        cache_dir.display()
    );
    let growth = final_cache_files.saturating_sub(initial_cache_files);
    assert!(
        growth < 20,
        "cache files grew by {} over 100 requests (initial={}, final={}) — possible leak",
        growth,
        initial_cache_files,
        final_cache_files
    );

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

    // Verify server still responsive after all transforms. If the musl
    // static binary hasn't recovered from the heavy transform work yet,
    // this fails and Bazel re-runs the whole test
    // (`--flaky_test_attempts` for this target in `.bazelrc`) — the single
    // retry mechanism for the suite.
    let health_url = format!("{}/unit/lena512.jp2/full/max/0/default.jpg", srv.base_url);
    let resp = c
        .get(&health_url)
        .send()
        .expect("health check request failed");
    assert_eq!(
        resp.status().as_u16(),
        200,
        "server not responsive after transform pipeline"
    );
}
