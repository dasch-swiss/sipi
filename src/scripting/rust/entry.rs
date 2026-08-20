//! Runtime entry points. Currently the Lua config parse: [`parse_config_file`]
//! evaluates a `sipi.config.lua` in the unlimited config VM
//! ([`crate::runtime::config_vm`]) and reads the `sipi` / `admin` /
//! `fileserver` / `routes` globals into a [`LuaConfigFile`], applying the same
//! defaults, key deprecations, and type checks the config contract defines:
//! strings coerce from numbers, integers and booleans are strict, an absent or
//! non-table section means "all defaults", and the `routes` global must exist
//! as a table.
//!
//! Error strings never echo config source text (the file carries
//! `jwt_secret = '…'` literally): Lua messages are cut at their `near '…'`
//! source echo, keeping chunk name + line + reason.

use std::path::Path;

use mlua::{Lua, Table, Value};

use crate::runtime::config_vm;

/// One `routes` table row (`{ method = …, route = …, script = … }`); `script`
/// is as written in the config — the caller composes it against `script_dir`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LuaRouteSpec {
    pub method: String,
    pub route: String,
    pub script: String,
}

/// Per-codec scaling quality as written in the config; `None` = key absent
/// (the consumer's defaults apply: jpeg medium, tiff/png/j2k high).
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct LuaScalingQuality {
    pub jpeg: Option<String>,
    pub tiff: Option<String>,
    pub png: Option<String>,
    pub j2k: Option<String>,
}

/// The resolved contents of a Lua config file, defaults applied. Raw size
/// strings (`cache_size`, `max_post_size`) stay raw — the engine parses the
/// suffix — but are validated here so a malformed size fails startup.
#[derive(Debug, Clone)]
pub struct LuaConfigFile {
    pub hostname: String,
    pub port: i64,
    pub ssl_port: i64,
    pub img_root: String,
    pub max_temp_file_age: i64,
    pub prefix_as_path: bool,
    pub jpeg_quality: i64,
    pub scaling_quality: LuaScalingQuality,
    pub init_script: String,
    pub cache_dir: String,
    pub cache_size: String,
    pub cache_nfiles: i64,
    pub thumb_size: String,
    pub max_post_size: String,
    pub tmp_dir: String,
    pub script_dir: String,
    pub jwt_secret: String,
    pub knora_path: String,
    pub knora_port: String,
    pub admin_user: String,
    pub admin_password: String,
    pub docroot: String,
    pub wwwroute: String,
    /// Read and type-checked for schema parity; the shell's own
    /// `--drain-timeout` knob governs draining, so this value is not consumed.
    pub drain_timeout: i64,
    pub routes: Vec<LuaRouteSpec>,
}

/// The method vocabulary a `routes` row may use (the request-VM dispatch set).
const ROUTE_METHODS: [&str; 8] = [
    "GET", "PUT", "POST", "DELETE", "OPTIONS", "CONNECT", "HEAD", "OTHER",
];

/// Mirrors the engine's size-string grammar: `"-1"` = unlimited sentinel,
/// `M`/`m` and `G`/`g` suffixes, else plain bytes. Unlike C `stoll`, trailing
/// garbage is an error, not silently truncated.
pub fn parse_size_string(s: &str) -> Result<i64, String> {
    if s.is_empty() {
        return Ok(0);
    }
    if s == "-1" {
        return Ok(-1);
    }
    let (digits, mult) = match s.as_bytes()[s.len() - 1] {
        b'M' | b'm' => (&s[..s.len() - 1], 1024 * 1024),
        b'G' | b'g' => (&s[..s.len() - 1], 1024 * 1024 * 1024),
        _ => (s, 1),
    };
    digits
        .trim()
        .parse::<i64>()
        .ok()
        .and_then(|v| v.checked_mul(mult))
        .ok_or_else(|| format!("invalid size value '{s}'"))
}

/// Evaluates the Lua config file and reads it into a [`LuaConfigFile`].
pub fn parse_config_file(path: &Path) -> Result<LuaConfigFile, String> {
    let source = std::fs::read(path)
        .map_err(|e| format!("cannot read Lua config {}: {e}", path.display()))?;
    let lua = config_vm().map_err(|e| e.to_string())?;
    lua.load(source.as_slice())
        .set_name(format!("@{}", path.display()).as_str())
        .exec()
        .map_err(|e| sanitize_lua_error(&e))?;
    read_config(&lua)
}

/// Cuts a Lua error message at its `near '…'` source echo so config source
/// text (which may carry secrets) never reaches logs or Sentry. The chunk
/// name, line number, and reason survive.
fn sanitize_lua_error(e: &mlua::Error) -> String {
    let msg = e.to_string();
    msg.split(" near ").next().unwrap_or(&msg).to_string()
}

fn read_config(lua: &Lua) -> Result<LuaConfigFile, String> {
    let sipi = section(lua, "sipi")?;
    let admin = section(lua, "admin")?;
    let fileserver = section(lua, "fileserver")?;

    // cache_dir: new key first, then the deprecated key.
    let cache_dir_new = cfg_string(&sipi, "sipi", "cache_dir", "")?;
    let cache_dir_old = cfg_string(&sipi, "sipi", "cachedir", "")?;
    let cache_dir = match (cache_dir_new.is_empty(), cache_dir_old.is_empty()) {
        (false, false) => {
            return Err(
                "Both 'cachedir' and 'cache_dir' specified. Remove the deprecated 'cachedir' key."
                    .to_string(),
            );
        }
        (false, true) => cache_dir_new,
        (true, false) => {
            tracing::warn!("Config key 'cachedir' is deprecated. Use 'cache_dir' instead.");
            cache_dir_old
        }
        (true, true) => "./cache".to_string(),
    };

    // cache_size: same new-then-deprecated dance, kept raw but validated.
    let cache_size_new = cfg_string(&sipi, "sipi", "cache_size", "")?;
    let cache_size_old = cfg_string(&sipi, "sipi", "cachesize", "")?;
    let cache_size = match (cache_size_new.is_empty(), cache_size_old.is_empty()) {
        (false, false) => {
            return Err(
                "Both 'cachesize' and 'cache_size' specified. Remove the deprecated 'cachesize' key."
                    .to_string(),
            );
        }
        (false, true) => cache_size_new,
        (true, false) => {
            tracing::warn!("Config key 'cachesize' is deprecated. Use 'cache_size' instead.");
            cache_size_old
        }
        (true, true) => "200M".to_string(),
    };
    if parse_size_string(&cache_size)? < -1 {
        return Err(format!(
            "Invalid cache_size value '{cache_size}'. Use '-1' (unlimited), '0' (disabled), or a positive value like '200M'."
        ));
    }

    // cache_hysteresis: no longer supported; warn when explicitly set.
    if cfg_float(&sipi, "sipi", "cache_hysteresis", -1.0)? >= 0.0 {
        tracing::warn!(
            "Config key 'cache_hysteresis' is no longer supported (replaced by the built-in 80% low-water mark). Remove it from your config."
        );
    }

    let max_post_size = cfg_string(&sipi, "sipi", "max_post_size", "0")?;
    parse_size_string(&max_post_size)?;

    let scaling = cfg_string_table(&sipi, "sipi", "scaling_quality")?;
    let scaling_quality = match scaling {
        Some(map) => LuaScalingQuality {
            jpeg: map.get("jpeg").cloned(),
            tiff: map.get("tiff").cloned(),
            png: map.get("png").cloned(),
            j2k: map.get("j2k").cloned(),
        },
        None => LuaScalingQuality::default(),
    };

    Ok(LuaConfigFile {
        hostname: cfg_string(&sipi, "sipi", "hostname", "localhost")?,
        port: cfg_integer(&sipi, "sipi", "port", 3333)?,
        ssl_port: cfg_integer(&sipi, "sipi", "ssl_port", -1)?,
        img_root: cfg_string(&sipi, "sipi", "imgroot", ".")?,
        max_temp_file_age: cfg_integer(&sipi, "sipi", "max_temp_file_age", 86400)?,
        prefix_as_path: cfg_boolean(&sipi, "sipi", "prefix_as_path", true)?,
        jpeg_quality: cfg_integer(&sipi, "sipi", "jpeg_quality", 80)?,
        scaling_quality,
        init_script: cfg_string(&sipi, "sipi", "initscript", ".")?,
        cache_dir,
        cache_size,
        cache_nfiles: cfg_integer(&sipi, "sipi", "cache_nfiles", 200)?.max(0),
        thumb_size: cfg_string(&sipi, "sipi", "thumb_size", "!128,128")?,
        max_post_size,
        tmp_dir: cfg_string(&sipi, "sipi", "tmpdir", "/tmp")?,
        script_dir: cfg_string(&sipi, "sipi", "scriptdir", "./scripts")?,
        jwt_secret: cfg_string(&sipi, "sipi", "jwt_secret", "")?,
        knora_path: cfg_string(&sipi, "sipi", "knora_path", "localhost")?,
        knora_port: cfg_string(&sipi, "sipi", "knora_port", "3333")?,
        admin_user: cfg_string(&admin, "admin", "user", "")?,
        admin_password: cfg_string(&admin, "admin", "password", "")?,
        docroot: cfg_string(&fileserver, "fileserver", "docroot", "")?,
        wwwroute: cfg_string(&fileserver, "fileserver", "wwwroute", "")?,
        drain_timeout: {
            let v = cfg_integer(&sipi, "sipi", "drain_timeout", 30)?;
            if v < 1 {
                30
            } else {
                v
            }
        },
        routes: read_routes(lua)?,
    })
}

/// A config section global. A non-table value (including absent) means "all
/// defaults for this section".
fn section(lua: &Lua, name: &str) -> Result<Option<Table>, String> {
    match lua.globals().get::<Value>(name) {
        Ok(Value::Table(t)) => Ok(Some(t)),
        Ok(_) => Ok(None),
        Err(e) => Err(e.to_string()),
    }
}

fn field(sec: &Option<Table>, key: &str) -> Result<Value, String> {
    match sec {
        Some(t) => t.get::<Value>(key).map_err(|e| e.to_string()),
        None => Ok(Value::Nil),
    }
}

/// String value; numbers coerce (the Lua C API's `lua_tostring` semantics).
fn cfg_string(
    sec: &Option<Table>,
    sec_name: &str,
    key: &str,
    default: &str,
) -> Result<String, String> {
    match field(sec, key)? {
        Value::Nil => Ok(default.to_string()),
        Value::String(s) => Ok(s.to_string_lossy()),
        Value::Integer(i) => Ok(i.to_string()),
        Value::Number(n) => Ok(format_lua_number(n)),
        _ => Err(format!("String expected for {sec_name}.{key}")),
    }
}

/// Strict integer (a float — even `3.0` — is a type error, matching
/// `lua_isinteger`).
fn cfg_integer(
    sec: &Option<Table>,
    sec_name: &str,
    key: &str,
    default: i64,
) -> Result<i64, String> {
    match field(sec, key)? {
        Value::Nil => Ok(default),
        Value::Integer(i) => Ok(i),
        _ => Err(format!("Integer expected for {sec_name}.{key}")),
    }
}

fn cfg_boolean(
    sec: &Option<Table>,
    sec_name: &str,
    key: &str,
    default: bool,
) -> Result<bool, String> {
    match field(sec, key)? {
        Value::Nil => Ok(default),
        Value::Boolean(b) => Ok(b),
        _ => Err(format!("Boolean expected for {sec_name}.{key}")),
    }
}

fn cfg_float(sec: &Option<Table>, sec_name: &str, key: &str, default: f64) -> Result<f64, String> {
    match field(sec, key)? {
        Value::Nil => Ok(default),
        Value::Integer(i) => Ok(i as f64),
        Value::Number(n) => Ok(n),
        _ => Err(format!("Number expected for {sec_name}.{key}")),
    }
}

/// A string→string sub-table; `None` when the key is absent.
fn cfg_string_table(
    sec: &Option<Table>,
    sec_name: &str,
    key: &str,
) -> Result<Option<std::collections::HashMap<String, String>>, String> {
    match field(sec, key)? {
        Value::Nil => Ok(None),
        Value::Table(t) => {
            let mut map = std::collections::HashMap::new();
            for pair in t.pairs::<Value, Value>() {
                let (k, v) = pair.map_err(|e| e.to_string())?;
                let (Value::String(k), Value::String(v)) = (k, v) else {
                    continue; // non-string entries are skipped, as in the C API walk
                };
                map.insert(k.to_string_lossy(), v.to_string_lossy());
            }
            Ok(Some(map))
        }
        _ => Err(format!(
            "Value '{key}' in config file must be a table (in {sec_name})"
        )),
    }
}

/// The `routes` global: required, a table, read as a 1-based sequence of
/// `{ method, route, script }` string triples with the method validated
/// against the dispatch vocabulary.
fn read_routes(lua: &Lua) -> Result<Vec<LuaRouteSpec>, String> {
    let routes: Table = match lua.globals().get::<Value>("routes") {
        Ok(Value::Table(t)) => t,
        Ok(_) => return Err("Value 'routes' in config file must be a table".to_string()),
        Err(e) => return Err(e.to_string()),
    };
    let mut out = Vec::new();
    for i in 1.. {
        let row: Value = routes.raw_get(i).map_err(|e| e.to_string())?;
        let row = match row {
            Value::Nil => break,
            Value::Table(t) => t,
            _ => return Err(format!("routes[{i}] must be a table")),
        };
        let get_str = |key: &str| -> Result<String, String> {
            match row.get::<Value>(key).map_err(|e| e.to_string())? {
                Value::String(s) => Ok(s.to_string_lossy()),
                _ => Err(format!("routes[{i}].{key} must be a string")),
            }
        };
        let method = get_str("method")?;
        if !ROUTE_METHODS.contains(&method.as_str()) {
            return Err(format!("Unknown HTTP method {method}"));
        }
        out.push(LuaRouteSpec {
            method,
            route: get_str("route")?,
            script: get_str("script")?,
        });
    }
    Ok(out)
}

/// Float-to-string coercion for a string-typed config key given a number.
/// Rust's shortest-roundtrip formatting; an integral float renders with a
/// trailing `.0` trimmed (config keys are paths/sizes/hosts — a float here is
/// already an authoring oddity).
fn format_lua_number(n: f64) -> String {
    let s = format!("{n}");
    s.strip_suffix(".0").map_or_else(|| s.clone(), String::from)
}
