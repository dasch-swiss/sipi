//! `--config` accepts a `.toml` file as well as `.lua`. This proves a TOML
//! config serves identically to the equivalent Lua config: the same info.json,
//! the same image response, and a configured `[[routes]]` entry dispatching like
//! a Lua-config route. The matched fixtures are
//! `config/sipi.toml-parity-test.{lua,toml}` under `test/_test_data`; they set
//! the same imgroot, scriptdir, init script, and the same `GET /parity` route.

use sipi_e2e::{http_client, test_data_dir, SipiServer};

/// Fetch `path` from `server` as text, replacing the server's own base URL (which
/// embeds the ephemeral per-server port) with a placeholder so two servers on
/// different ports compare equal — info.json's `id` is the only host-bearing
/// field.
fn fetch_normalized(client: &reqwest::blocking::Client, server: &SipiServer, path: &str) -> String {
    let url = format!("{}{}", server.base_url, path);
    let body = client
        .get(&url)
        .send()
        .expect("request failed")
        .text()
        .expect("response body");
    body.replace(&server.base_url, "{BASE}")
}

#[test]
fn toml_config_serves_identically_to_lua() {
    let dir = test_data_dir();
    // Two servers on distinct ports: one driven by the Lua config, one by the
    // equivalent TOML config (Lua-less engine init). SipiServer kills each on drop.
    let lua = SipiServer::start("config/sipi.toml-parity-test.lua", &dir);
    let toml = SipiServer::start("config/sipi.toml-parity-test.toml", &dir);
    let client = http_client();

    // 1. info.json is byte-identical once the port is normalised — same imgroot,
    //    same engine assembly, just sourced from a TOML rather than a Lua config.
    let info = "/unit/lena512.tif/info.json";
    assert_eq!(
        fetch_normalized(&client, &lua, info),
        fetch_normalized(&client, &toml, info),
        "info.json differs between the .lua and .toml configs"
    );

    // 2. an IIIF image request succeeds with the same status on both.
    let img = "/unit/lena512.tif/full/max/0/default.jpg";
    let lua_img = client
        .get(format!("{}{}", lua.base_url, img))
        .send()
        .expect("lua image request");
    let toml_img = client
        .get(format!("{}{}", toml.base_url, img))
        .send()
        .expect("toml image request");
    assert!(
        lua_img.status().is_success(),
        "lua image request failed: {}",
        lua_img.status()
    );
    assert_eq!(
        lua_img.status(),
        toml_img.status(),
        "image response status differs between configs"
    );

    // 3. the configured [[routes]] entry dispatches identically — proving a TOML
    //    route reaches the engine's Lua runtime via the same sipi_run_lua_route
    //    path as a Lua-config route.
    let lua_route = client
        .get(format!("{}/parity", lua.base_url))
        .send()
        .expect("lua route request");
    let toml_route = client
        .get(format!("{}/parity", toml.base_url))
        .send()
        .expect("toml route request");
    let lua_status = lua_route.status();
    let toml_status = toml_route.status();
    assert!(
        toml_status.is_success(),
        "TOML-configured route did not dispatch: {}",
        toml_status
    );
    assert_eq!(
        lua_status, toml_status,
        "route status differs between configs"
    );
    assert_eq!(
        lua_route.text().expect("lua route body"),
        toml_route.text().expect("toml route body"),
        "route body differs between configs"
    );
}
