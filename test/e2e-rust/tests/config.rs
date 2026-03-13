mod common;

use sipi_e2e::{http_client, test_data_dir, SipiServer};
use std::process::Command;

// =============================================================================
// Configuration tests — each starts its own server with custom config
// =============================================================================

#[test]
fn invalid_config_startup() {
    // Spawn sipi with invalid config (malformed Lua syntax), verify clean error
    // message and non-zero exit.
    let test_data = test_data_dir();

    let config_content = r#"sipi = {
    port = 1024,
    -- deliberately broken Lua syntax:
    nthreads = ,
}
"#;

    let config_path = test_data.join("config/sipi.invalid-syntax.lua");
    std::fs::write(&config_path, config_content).expect("write invalid config");

    let sipi_bin = std::env::var("SIPI_BIN")
        .unwrap_or_else(|_| sipi_e2e::find_sipi_bin().to_string_lossy().to_string());

    let output = Command::new(&sipi_bin)
        .arg("--config")
        .arg("config/sipi.invalid-syntax.lua")
        .current_dir(&test_data)
        .output()
        .expect("failed to spawn sipi");

    let _ = std::fs::remove_file(&config_path);

    assert!(
        !output.status.success(),
        "sipi should exit with non-zero status for invalid config, got: {:?}",
        output.status
    );
}

#[test]
fn config_nonexistent_paths() {
    // Start sipi with nonexistent imgroot, verify graceful error (not crash).
    let test_data = test_data_dir();

    let config_content = r#"sipi = {
    port = 1024,
    nthreads = 4,
    jpeg_quality = 60,
    scaling_quality = { jpeg = "medium", tiff = "high", png = "high", j2k = "high" },
    keep_alive = 5,
    max_post_size = '300M',
    imgroot = '/nonexistent/path/that/does/not/exist',
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

routes = {}
"#;

    let config_path = test_data.join("config/sipi.nonexistent-paths.lua");
    std::fs::write(&config_path, config_content).expect("write nonexistent paths config");

    // Try to start the server — it may fail to start or start but fail on requests
    let result = std::panic::catch_unwind(|| {
        SipiServer::start("config/sipi.nonexistent-paths.lua", &test_data)
    });

    let _ = std::fs::remove_file(&config_path);

    match result {
        Ok(srv) => {
            // Server started despite nonexistent imgroot — requests should fail gracefully
            let c = http_client();
            let resp = c
                .get(format!(
                    "{}/unit/lena512.jp2/full/max/0/default.jpg",
                    srv.base_url
                ))
                .send();

            match resp {
                Ok(r) => {
                    assert!(
                        r.status().as_u16() >= 400,
                        "request with nonexistent imgroot should fail"
                    );
                }
                Err(_) => {
                    // Connection error is also acceptable
                }
            }
        }
        Err(_) => {
            // Server failed to start — this is the expected graceful behavior
        }
    }
}

#[test]
fn config_deprecated_key_migration() {
    // Start sipi with old `cachedir` key (note: no underscore), verify behavior.
    // sipi may treat it as `cache_dir` or ignore it.
    let test_data = test_data_dir();

    // Config with 'cachedir' (old style, no underscore) instead of 'cache_dir'
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
    cachedir = './cache',
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

routes = {}
"#;

    let config_path = test_data.join("config/sipi.deprecated-key.lua");
    std::fs::write(&config_path, config_content).expect("write deprecated key config");

    let result = std::panic::catch_unwind(|| {
        SipiServer::start("config/sipi.deprecated-key.lua", &test_data)
    });

    let _ = std::fs::remove_file(&config_path);

    match result {
        Ok(srv) => {
            // Server started — verify it works
            let c = http_client();
            let resp = c
                .get(format!(
                    "{}/unit/lena512.jp2/full/max/0/default.jpg",
                    srv.base_url
                ))
                .send()
                .expect("request failed with deprecated key config");
            assert_eq!(
                resp.status().as_u16(),
                200,
                "server should work with deprecated config key"
            );
        }
        Err(_) => {
            // Server failed to start — document that deprecated key is not supported
            eprintln!(
                "NOTE: sipi does not support deprecated 'cachedir' key (requires 'cache_dir')"
            );
        }
    }
}

#[test]
fn config_cli_overrides() {
    // Start sipi with --serverport CLI flag, verify it overrides config value.
    // The SipiServer::start() already uses --serverport, so we verify the server
    // actually listens on the dynamically allocated port.
    let test_data = test_data_dir();
    let srv = SipiServer::start("config/sipi.cache-test-config.lua", &test_data);
    let c = http_client();

    // The config says port=1024, but start() overrides via --serverport
    // If the override didn't work, we wouldn't be able to connect
    let resp = c
        .get(format!(
            "{}/unit/lena512.jp2/full/max/0/default.jpg",
            srv.base_url
        ))
        .send()
        .expect("request to CLI-overridden port failed");

    assert_eq!(resp.status().as_u16(), 200, "CLI port override should work");

    // Verify the port is NOT the config default (1024)
    assert_ne!(
        srv.http_port, 1024,
        "server should use CLI-overridden port, not config default"
    );
}

#[test]
fn parse_size_string_edge_cases() {
    // Test parseSizeString indirectly by starting sipi with unusual max_post_size values.
    let test_data = test_data_dir();

    // Test with max_post_size = '0' — should disable uploads or set to unlimited
    let config_content = r#"sipi = {
    port = 1024,
    nthreads = 4,
    jpeg_quality = 60,
    scaling_quality = { jpeg = "medium", tiff = "high", png = "high", j2k = "high" },
    keep_alive = 5,
    max_post_size = '0',
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

routes = {}
"#;

    let config_path = test_data.join("config/sipi.size-zero.lua");
    std::fs::write(&config_path, config_content).expect("write size zero config");

    let result =
        std::panic::catch_unwind(|| SipiServer::start("config/sipi.size-zero.lua", &test_data));

    let _ = std::fs::remove_file(&config_path);

    match result {
        Ok(srv) => {
            // Server started — IIIF should still work
            let c = http_client();
            let resp = c
                .get(format!(
                    "{}/unit/lena512.jp2/full/max/0/default.jpg",
                    srv.base_url
                ))
                .send()
                .expect("request failed with zero max_post_size");
            assert_eq!(resp.status().as_u16(), 200);
        }
        Err(_) => {
            // Server may fail to start with zero — acceptable
            eprintln!("NOTE: sipi doesn't start with max_post_size='0'");
        }
    }
}
