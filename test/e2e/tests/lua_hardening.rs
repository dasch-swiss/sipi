//! End-to-end proof of the hardened Lua runtime contract (ADR-0023):
//! the sandbox (stdlib whitelist, os shim, restricted require, dropped
//! bindings), the deadline/memory kills with their pre-/post-commit
//! semantics, the lowercase-header invariant, the request-time 404 for a
//! missing route script, fail-closed startup on a broken init script, the
//! request-time 500 after a post-boot init-script edit, and the
//! killed-preflight-is-never-cached property.
//!
//! The route probes share one dedicated server started from
//! `config/sipi.lua-hardening-config.lua` with a 1s deadline so the kill
//! tests stay fast; the preflight tests write their own config + init pairs
//! (unique filenames — tests in this binary may run concurrently).

use reqwest::blocking::Client;
use sipi_e2e::{allocate_ports, http_client, sipi_bin_path, test_data_dir, SipiServer};
use std::process::Command;
use std::sync::OnceLock;
use std::time::Duration;

static SERVER: OnceLock<SipiServer> = OnceLock::new();

/// The shared hardening server: no init script (no preflight), the hardening
/// routes, and a 1s Lua deadline.
fn server() -> &'static SipiServer {
    SERVER.get_or_init(|| {
        SipiServer::start_env(
            "config/sipi.lua-hardening-config.lua",
            &test_data_dir(),
            &[],
            &[("SIPI_LUA_TIMEOUT_MS", "1000")],
        )
    })
}

fn get_text(url: &str) -> (u16, String) {
    let resp = http_client().get(url).send().expect("GET failed");
    let status = resp.status().as_u16();
    let body = resp.text().expect("read body");
    (status, body)
}

// ── Sandbox ──────────────────────────────────────────────────────────────────

/// DEV-5925: `io`/`debug` never load; `dofile`/`loadfile`/`load`/
/// `collectgarbage`/`package.loadlib`/`package.searchpath` are scrubbed; the
/// os shim is exactly `getenv`/`clock`/`date`; `server.shutdown`,
/// `server.fs.chdir`, `config.password`, and `config.adminuser` are gone.
/// The script prints the first failing probe, so a regression names itself.
#[test]
fn sandbox_profile_is_enforced() {
    let (status, body) = get_text(&format!("{}/hardening/sandbox", server().base_url));
    assert_eq!(status, 200, "sandbox probe route failed: {body}");
    assert_eq!(body.trim(), "SANDBOX_OK", "sandbox probe found: {body}");
}

/// A plain `[A-Za-z0-9_]+` module name in the script dir loads; separators,
/// traversal, and extensions are rejected.
#[test]
fn require_is_restricted_to_the_script_dir() {
    let (status, body) = get_text(&format!("{}/hardening/require", server().base_url));
    assert_eq!(status, 200, "require probe route failed: {body}");
    assert_eq!(body.trim(), "REQUIRE_OK", "require probe found: {body}");
}

// ── Kills (DEV-6070) ─────────────────────────────────────────────────────────

/// An infinite loop (inside `pcall`) before any body byte: the deadline kill
/// is a pre-commit 500, and the trapped script makes no progress after the
/// timeout (the body never carries the script's post-pcall output).
#[test]
fn timeout_kill_before_commit_is_a_500() {
    let (status, body) = get_text(&format!("{}/hardening/loop", server().base_url));
    assert_eq!(status, 500, "deadline kill must be a 500, body: {body}");
    assert!(
        !body.contains("SURVIVED_THE_KILL"),
        "a pcall-trapped script kept running after the kill"
    );
}

/// The head is already committed (first body byte streamed) when the loop
/// starts: the kill must abort the stream so the client never sees a clean
/// EOF — reading the body errors instead of returning a short success.
#[test]
fn timeout_kill_after_commit_aborts_the_stream() {
    let resp = http_client()
        .get(format!("{}/hardening/loop_committed", server().base_url))
        .send()
        .expect("GET failed");
    assert_eq!(resp.status().as_u16(), 200, "head commits before the kill");
    assert!(
        resp.bytes().is_err(),
        "a post-commit kill must reset the connection, not end the body cleanly"
    );
}

/// An untrapped memory bomb hits the Lua allocator cap: a pre-commit 500
/// (a memory kill), never an engine OOM.
#[test]
fn memory_kill_is_a_500() {
    let (status, body) = get_text(&format!("{}/hardening/memory", server().base_url));
    assert_eq!(status, 500, "memory kill must be a 500, body: {body}");
}

/// The head is already committed when the script raises an ordinary uncaught
/// Lua error: the stream must be aborted — a dropped sender would read as a
/// clean EOF, indistinguishable from a complete 200 body.
#[test]
fn script_error_after_commit_aborts_the_stream() {
    let resp = http_client()
        .get(format!("{}/hardening/error_committed", server().base_url))
        .send()
        .expect("GET failed");
    assert_eq!(resp.status().as_u16(), 200, "head commits before the error");
    assert!(
        resp.bytes().is_err(),
        "a post-commit script error must abort the stream, not end the body cleanly"
    );
}

/// A slow-reading client must not pin the blocking thread past the Lua
/// deadline: the route streams 16 MiB, the client stalls for 3.5s (deadline
/// 1s), then drains. The bounded body-channel write must have failed the
/// script at the deadline, so the drained stream is far short of the full
/// body (an unbounded `blocking_send` would resume and deliver all 16 MiB).
#[test]
fn stalled_reader_cannot_pin_the_worker_past_the_deadline() {
    use std::io::{Read, Write};

    let full_size = 64 * 256 * 1024;
    let port = server().http_port;
    let mut sock = std::net::TcpStream::connect(("127.0.0.1", port)).expect("connect");
    write!(
        sock,
        "GET /hardening/stream HTTP/1.1
Host: 127.0.0.1:{port}
Connection: close

"
    )
    .expect("send request");

    // Stall: read nothing while the server's channel + socket buffers fill.
    std::thread::sleep(Duration::from_millis(3500));

    // Drain whatever the server managed to buffer before the deadline fix
    // failed the script's writes.
    sock.set_read_timeout(Some(Duration::from_secs(10)))
        .expect("timeout");
    let mut drained = Vec::new();
    let _ = sock.read_to_end(&mut drained);
    assert!(
        drained.len() < full_size / 2,
        "the stalled route kept streaming after the deadline: drained {} of {full_size} bytes",
        drained.len()
    );
}

/// Trapping the allocation error with `pcall` is stock Lua semantics, but it
/// must not lift the cap: a follow-up over-cap allocation in the same VM
/// fails the same way.
#[test]
fn memory_cap_survives_a_trapping_pcall() {
    let (status, body) = get_text(&format!("{}/hardening/memory_trapped", server().base_url));
    assert_eq!(status, 200, "trapped-bomb probe route failed: {body}");
    assert_eq!(body.trim(), "CAP_HELD", "found: {body}");
}

// ── Header + routing invariants ──────────────────────────────────────────────

/// `server.header` keys stay lowercase (a pinned invariant production scripts
/// depend on): a mixed-case request header arrives under its lowercase name
/// and no key in the table carries uppercase.
#[test]
fn header_keys_are_lowercase() {
    let resp = http_client()
        .get(format!("{}/hardening/headers", server().base_url))
        .header("X-MiXeD-CaSe", "probe-value")
        .send()
        .expect("GET failed");
    let status = resp.status().as_u16();
    let body = resp.text().expect("read body");
    assert_eq!(status, 200, "header probe route failed: {body}");
    assert_eq!(body.trim(), "HEADERS_OK:probe-value", "found: {body}");
}

/// A configured route whose script is missing on disk is a request-time 404,
/// never a boot failure.
#[test]
fn missing_route_script_is_a_404() {
    let (status, _) = get_text(&format!("{}/hardening/missing", server().base_url));
    assert_eq!(status, 404);
}

// ── Init-script lifecycle ────────────────────────────────────────────────────

/// A minimal config whose `initscript` points at `init`, with the shared
/// fixture paths.
fn preflight_config(init: &str) -> String {
    format!(
        r#"sipi = {{
    port = 1024,
    jpeg_quality = 60,
    scaling_quality = {{ jpeg = "medium", tiff = "high", png = "high", j2k = "high" }},
    max_post_size = '300M',
    imgroot = './images',
    prefix_as_path = true,
    initscript = '{init}',
    cache_dir = './cache',
    cache_size = '20M',
    cache_nfiles = 8,
    scriptdir = './scripts',
    thumb_size = '!128,128',
    tmpdir = '/tmp',
    max_temp_file_age = 86400,
    knora_path = 'localhost',
    knora_port = '3434',
    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',
}}
routes = {{}}
"#
    )
}

/// Removes the written fixture files on drop, so a failing assertion doesn't
/// leave them behind in the shared `test/_test_data` tree.
struct Cleanup(Vec<std::path::PathBuf>);
impl Drop for Cleanup {
    fn drop(&mut self) {
        for p in &self.0 {
            let _ = std::fs::remove_file(p);
        }
    }
}

/// An init script that errors refuses startup (fail closed): the process
/// exits non-zero instead of serving with authorization silently disabled.
#[test]
fn broken_init_script_refuses_startup() {
    let test_data = test_data_dir();
    let init_rel = "config/hardening-bad-init.lua";
    let config_rel = "config/sipi.hardening-bad-init-config.lua";
    let _cleanup = Cleanup(vec![test_data.join(init_rel), test_data.join(config_rel)]);
    std::fs::write(
        test_data.join(init_rel),
        "error('deliberately broken init')",
    )
    .expect("write init");
    std::fs::write(
        test_data.join(config_rel),
        preflight_config(&format!("./{init_rel}")),
    )
    .expect("write config");

    let (http_port, _) = allocate_ports();
    let output = Command::new(sipi_bin_path())
        .arg("server")
        .arg("--config")
        .arg(config_rel)
        .arg("--serverport")
        .arg(http_port.to_string())
        .current_dir(&test_data)
        .output()
        .expect("spawn sipi");

    assert!(
        !output.status.success(),
        "a broken init script must refuse startup, got: {:?}\nstderr:\n{}",
        output.status,
        String::from_utf8_lossy(&output.stderr)
    );
}

/// Hook probes are boot-frozen; a post-boot edit that breaks the init script
/// surfaces as a request-time 500 (fail closed), not a silent allow.
#[test]
fn post_boot_init_edit_fails_closed() {
    let test_data = test_data_dir();
    let init_rel = "config/hardening-editable-init.lua";
    let config_rel = "config/sipi.hardening-editable-init-config.lua";
    let _cleanup = Cleanup(vec![test_data.join(init_rel), test_data.join(config_rel)]);
    std::fs::write(
        test_data.join(init_rel),
        "function pre_flight(prefix, identifier, cookie)\n\
             return 'allow', config.imgroot .. '/' .. prefix .. '/' .. identifier\n\
         end\n",
    )
    .expect("write init");
    std::fs::write(
        test_data.join(config_rel),
        preflight_config(&format!("./{init_rel}")),
    )
    .expect("write config");

    let srv = SipiServer::start(config_rel, &test_data);
    let url = format!("{}/unit/lena512.jp2/full/max/0/default.jpg", srv.base_url);
    let c = http_client();

    let before = c.get(&url).send().expect("GET before edit");
    assert_eq!(before.status().as_u16(), 200, "preflight allow before edit");

    // The bytecode cache invalidates on mtime+size, so the edit takes effect
    // on the next request without a restart.
    std::fs::write(
        test_data.join(init_rel),
        "error('init broken by a post-boot edit')",
    )
    .expect("rewrite init");

    let after = c.get(&url).send().expect("GET after edit");
    assert_eq!(
        after.status().as_u16(),
        500,
        "a broken init script at request time must fail closed"
    );
}

/// DEV-6070 + the cache contract: a killed preflight returns a bare 500 and
/// is never written to the preflight cache. The hook drops a marker directory
/// per invocation before looping forever; two requests for the same key under
/// an enabled cache must run the hook twice (nothing was cached), and both
/// must be 500s.
#[test]
fn killed_preflight_is_a_500_and_never_cached() {
    let test_data = test_data_dir();
    let tmp = std::env::temp_dir().join(format!("sipi-hardening-pf-{}", std::process::id()));
    std::fs::create_dir_all(&tmp).expect("create marker base");
    let init_rel = "config/hardening-killed-preflight-init.lua";
    let config_rel = "config/sipi.hardening-killed-preflight-config.lua";
    let _cleanup = Cleanup(vec![test_data.join(init_rel), test_data.join(config_rel)]);
    std::fs::write(
        test_data.join(init_rel),
        format!(
            "function pre_flight(prefix, identifier, cookie)\n\
                 local base = '{}'\n\
                 -- server.fs.exists returns (ok, present)\n\
                 local _, present = server.fs.exists(base .. '/marker1')\n\
                 if present then\n\
                     server.fs.mkdir(base .. '/marker2', 448)\n\
                 else\n\
                     server.fs.mkdir(base .. '/marker1', 448)\n\
                 end\n\
                 while true do end\n\
             end\n",
            tmp.display()
        ),
    )
    .expect("write init");
    std::fs::write(
        test_data.join(config_rel),
        preflight_config(&format!("./{init_rel}")),
    )
    .expect("write config");

    let srv = SipiServer::start_env(
        config_rel,
        &test_data,
        &["--preflight-cache-ttl", "10"],
        &[("SIPI_LUA_TIMEOUT_MS", "500")],
    );
    let url = format!("{}/unit/lena512.jp2/full/max/0/default.jpg", srv.base_url);
    // The kill takes the full 500ms deadline, so give the client headroom.
    let c = Client::builder()
        .timeout(std::time::Duration::from_secs(30))
        .build()
        .expect("client");

    for attempt in 1..=2 {
        let resp = c.get(&url).send().expect("GET killed preflight");
        assert_eq!(
            resp.status().as_u16(),
            500,
            "killed preflight must be a bare 500 (attempt {attempt})"
        );
    }

    assert!(
        tmp.join("marker1").is_dir(),
        "first hook run left no marker"
    );
    assert!(
        tmp.join("marker2").is_dir(),
        "second request never ran the hook — a killed preflight was served from the cache"
    );
    let _ = std::fs::remove_dir_all(&tmp);
}
