//! Lua config-parse tests: the shipped `config/sipi.config.lua` resolves to
//! the expected values, and the schema semantics (defaults, deprecated keys,
//! strict types, the required `routes` table, `os.getenv`) hold.

use std::io::Write;
use std::path::PathBuf;

use runfiles::{rlocation, Runfiles};
use scripting::entry::parse_size_string;
use scripting::parse_config_file;

fn shipped_config() -> PathBuf {
    let r = Runfiles::create().expect("runfiles");
    rlocation!(r, "_main/config/sipi.config.lua").expect("config runfile")
}

fn write_config(body: &str) -> (tempfile::TempDir, PathBuf) {
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("cfg.lua");
    let mut f = std::fs::File::create(&path).expect("create");
    f.write_all(body.as_bytes()).expect("write");
    (dir, path)
}

#[test]
fn shipped_config_resolves_expected_values() {
    let cfg = parse_config_file(&shipped_config()).expect("parse shipped config");
    assert_eq!(cfg.port, 1024);
    assert_eq!(cfg.jpeg_quality, 60);
    assert_eq!(
        parse_size_string(&cfg.max_post_size).unwrap(),
        300 * 1024 * 1024
    );
    assert_eq!(cfg.img_root, "./images");
    assert!(cfg.prefix_as_path);
    assert_eq!(cfg.init_script, "./config/sipi.init.lua");
    assert_eq!(cfg.cache_dir, "./cache");
    assert_eq!(
        parse_size_string(&cfg.cache_size).unwrap(),
        20 * 1024 * 1024
    );
    assert_eq!(cfg.cache_nfiles, 8);
    assert_eq!(cfg.script_dir, "./scripts");
    assert_eq!(cfg.thumb_size, "!128,128");
    assert_eq!(cfg.tmp_dir, "/tmp");
    assert!(!cfg.routes.is_empty());
}

#[test]
fn absent_keys_resolve_to_defaults() {
    let (_d, path) = write_config("sipi = {}\nroutes = {}\n");
    let cfg = parse_config_file(&path).expect("parse");
    assert_eq!(cfg.hostname, "localhost");
    assert_eq!(cfg.port, 3333);
    assert_eq!(cfg.ssl_port, -1);
    assert_eq!(cfg.img_root, ".");
    assert_eq!(cfg.max_temp_file_age, 86400);
    assert!(cfg.prefix_as_path);
    assert_eq!(cfg.jpeg_quality, 80);
    assert_eq!(cfg.init_script, ".");
    assert_eq!(cfg.cache_dir, "./cache");
    assert_eq!(cfg.cache_size, "200M");
    assert_eq!(cfg.cache_nfiles, 200);
    assert_eq!(cfg.thumb_size, "!128,128");
    assert_eq!(cfg.max_post_size, "0");
    assert_eq!(cfg.tmp_dir, "/tmp");
    assert_eq!(cfg.script_dir, "./scripts");
    assert_eq!(cfg.jwt_secret, "");
    assert_eq!(cfg.knora_path, "localhost");
    assert_eq!(cfg.knora_port, "3333");
    assert_eq!(cfg.admin_user, "");
    assert_eq!(cfg.admin_password, "");
    assert_eq!(cfg.docroot, "");
    assert_eq!(cfg.wwwroute, "");
    assert!(cfg.routes.is_empty());
    assert_eq!(cfg.scaling_quality, Default::default());
}

#[test]
fn deprecated_cachedir_key_still_reads() {
    let (_d, path) = write_config("sipi = { cachedir = '/old/cache' }\nroutes = {}\n");
    let cfg = parse_config_file(&path).expect("parse");
    assert_eq!(cfg.cache_dir, "/old/cache");
}

#[test]
fn both_cache_dir_keys_is_an_error() {
    let (_d, path) =
        write_config("sipi = { cachedir = '/old', cache_dir = '/new' }\nroutes = {}\n");
    let err = parse_config_file(&path).expect_err("must reject both keys");
    assert!(err.contains("cachedir"), "{err}");
}

#[test]
fn strict_types_are_enforced() {
    for (body, needle) in [
        (
            "sipi = { port = '1024' }\nroutes = {}\n",
            "Integer expected for sipi.port",
        ),
        (
            "sipi = { prefix_as_path = 1 }\nroutes = {}\n",
            "Boolean expected for sipi.prefix_as_path",
        ),
        (
            "sipi = { imgroot = {} }\nroutes = {}\n",
            "String expected for sipi.imgroot",
        ),
        (
            "sipi = { cache_size = '20X' }\nroutes = {}\n",
            "invalid size value",
        ),
    ] {
        let (_d, path) = write_config(body);
        let err = parse_config_file(&path).expect_err(body);
        assert!(err.contains(needle), "{body}: {err}");
    }
}

#[test]
fn routes_global_is_required_and_validated() {
    let (_d, path) = write_config("sipi = {}\n");
    let err = parse_config_file(&path).expect_err("missing routes");
    assert!(err.contains("'routes'"), "{err}");

    let (_d2, path2) = write_config(
        "sipi = {}\nroutes = { { method = 'FETCH', route = '/x', script = 'x.lua' } }\n",
    );
    let err2 = parse_config_file(&path2).expect_err("unknown method");
    assert!(err2.contains("Unknown HTTP method FETCH"), "{err2}");

    let (_d3, path3) = write_config(
        "sipi = {}\nroutes = {\n  { method = 'POST', route = '/api/upload', script = 'upload.lua' },\n  { method = 'GET', route = '/api/x', script = 'x.lua' },\n}\n",
    );
    let cfg = parse_config_file(&path3).expect("parse");
    assert_eq!(cfg.routes.len(), 2);
    assert_eq!(cfg.routes[0].method, "POST");
    assert_eq!(cfg.routes[0].route, "/api/upload");
    assert_eq!(cfg.routes[0].script, "upload.lua");
}

#[test]
fn config_can_read_env() {
    std::env::set_var("SIPI_CONFIG_PARSE_TEST_ROOT", "/env/images");
    let (_d, path) = write_config(
        "sipi = { imgroot = os.getenv('SIPI_CONFIG_PARSE_TEST_ROOT') }\nroutes = {}\n",
    );
    let cfg = parse_config_file(&path).expect("parse");
    assert_eq!(cfg.img_root, "/env/images");
}

#[test]
fn parse_errors_do_not_echo_source_text() {
    // A syntax error adjacent to a secret literal: the `near '…'` echo is cut.
    let (_d, path) = write_config("sipi = { jwt_secret = = 'super-secret' }\nroutes = {}\n");
    let err = parse_config_file(&path).expect_err("syntax error");
    assert!(!err.contains("super-secret"), "{err}");
    assert!(err.contains("cfg.lua"), "{err}");
}

#[test]
fn scaling_quality_partial_table() {
    let (_d, path) = write_config("sipi = { scaling_quality = { jpeg = 'low' } }\nroutes = {}\n");
    let cfg = parse_config_file(&path).expect("parse");
    assert_eq!(cfg.scaling_quality.jpeg.as_deref(), Some("low"));
    assert_eq!(cfg.scaling_quality.tiff, None);
}
