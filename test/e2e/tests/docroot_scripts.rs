//! Docroot `.lua`/`.elua` execution through the `/server` fileserver.
//!
//! The fileserver runs docroot scripts through the existing `sipi_run_lua_route`
//! seam (no new FFI) and injects `server.docroot` into the VM (plan 02 §6 C,
//! Option 2 — the C++ `file_handler` does this at `Server.cpp:310`). The e2e
//! config sets `fileserver.docroot = './server'`, so a script that reads
//! `server.docroot` must see `./server`.

mod common;

use common::{client, server};
use sipi_e2e::test_data_dir;
use std::io::Write;

/// A docroot script written into the fileserver root, cleaned up on drop.
struct DocrootScript {
    path: std::path::PathBuf,
}

impl DocrootScript {
    fn create(name: &str, content: &str) -> Self {
        let server_dir = test_data_dir().join("server");
        std::fs::create_dir_all(&server_dir).expect("create server dir");
        let path = server_dir.join(name);
        let mut f = std::fs::File::create(&path).expect("create script");
        f.write_all(content.as_bytes()).expect("write script");
        DocrootScript { path }
    }
}

impl Drop for DocrootScript {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.path);
    }
}

/// An embedded-Lua (`.elua`) docroot script executes and reads the injected
/// `server.docroot` (Option 2). HTML outside `<lua>…</lua>` is streamed verbatim.
#[test]
fn elua_executes_and_reads_server_docroot() {
    let _script = DocrootScript::create(
        "probe_docroot.elua",
        "<lua>server.print(\"DOCROOT[\" .. tostring(server.docroot) .. \"]\")</lua>",
    );
    let srv = server();

    let resp = client()
        .get(format!("{}/server/probe_docroot.elua", srv.base_url))
        .send()
        .expect("GET failed");

    assert_eq!(resp.status().as_u16(), 200);
    let body = resp.text().expect("read body");
    assert!(
        body.contains("DOCROOT[./server]"),
        "server.docroot must be injected for docroot scripts, got: {body}"
    );
}

/// A pure-Lua (`.lua`) docroot script executes as a single chunk through the seam
/// and likewise sees `server.docroot`.
#[test]
fn lua_executes_and_reads_server_docroot() {
    let _script = DocrootScript::create(
        "probe_docroot.lua",
        "server.print(\"LUA_DOCROOT[\" .. tostring(server.docroot) .. \"]\")",
    );
    let srv = server();

    let resp = client()
        .get(format!("{}/server/probe_docroot.lua", srv.base_url))
        .send()
        .expect("GET failed");

    assert_eq!(resp.status().as_u16(), 200);
    let body = resp.text().expect("read body");
    assert!(
        body.contains("LUA_DOCROOT[./server]"),
        "pure-Lua docroot script must run with server.docroot, got: {body}"
    );
}

/// A docroot script that does not exist → 404 (the seam's `access(R_OK)` check).
#[test]
fn missing_script_is_404() {
    let srv = server();
    let resp = client()
        .get(format!("{}/server/does_not_exist.lua", srv.base_url))
        .send()
        .expect("GET failed");
    assert_eq!(resp.status().as_u16(), 404);
}
