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

use std::cell::RefCell;
use std::path::{Path, PathBuf};
use std::rc::Rc;

use mlua::{Lua, MultiValue, Table, Value};

use crate::bindings::{self, BindingCtx, ConfigValues, RequestData, ResponseWriter};
use crate::limits::{KillReason, LimitConfig, RuntimeError};
use crate::runtime::{config_vm, RequestVm, ScriptRuntime};

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

// ── The request-serving environment ─────────────────────────────────────────

/// The long-lived Lua environment the shell owns: the hardened
/// [`ScriptRuntime`], the configured init script, the JWT secret, and the
/// `config`-table values. Every entry point builds a fresh request VM
/// (isolation is across requests), installs the bindings, executes the init
/// script from the bytecode cache, and runs the hook or route chunk.
pub struct LuaEnv {
    runtime: ScriptRuntime,
    init_script: Option<PathBuf>,
    jwt_secret: String,
    config: ConfigValues,
}

/// Which hooks the init script defines — probed once at boot and frozen
/// (`AppState`); a hook added post-boot takes effect only on restart.
#[derive(Debug, Clone, Copy)]
pub struct HookProbes {
    pub pre_flight: bool,
    pub file_pre_flight: bool,
}

/// A preflight hook's answer.
pub enum PreflightReply {
    /// The validated permission string + the open kv channel (`infile`,
    /// `watermark`, `size`, auth-service keys …).
    Decision {
        permission: String,
        kv: Vec<(String, String)>,
    },
    /// The hook answered the request itself (`sendStatus` was called) —
    /// render this verbatim instead of dispatching on a permission.
    Direct {
        status: u16,
        headers: Vec<(String, String)>,
        body: Vec<u8>,
    },
}

/// Why a preflight call produced no reply. `Killed` maps to the kill path
/// (500, never cached); `Error` is logged here and maps to a bare 500.
#[derive(Debug)]
pub enum PreflightFailure {
    Killed(KillReason),
    Error(String),
}

impl LuaEnv {
    pub fn new(
        script_dir: PathBuf,
        init_script: Option<PathBuf>,
        jwt_secret: String,
        limits: LimitConfig,
        config: ConfigValues,
    ) -> Self {
        Self {
            runtime: ScriptRuntime::new(script_dir, limits),
            init_script,
            jwt_secret,
            config,
        }
    }

    pub fn config(&self) -> &ConfigValues {
        &self.config
    }

    /// Builds a request VM with the bindings installed and the init script
    /// executed (through the bytecode cache).
    fn vm_with_bindings(
        &self,
        mut req: RequestData,
        writer: ResponseWriter,
    ) -> Result<(RequestVm, Rc<RefCell<ResponseWriter>>), RuntimeError> {
        req.jwt_secret = self.jwt_secret.clone();
        let vm = self.runtime.request_vm()?;
        let response = Rc::new(RefCell::new(writer));
        let ctx = BindingCtx {
            request: Rc::new(req),
            response: Rc::clone(&response),
            config: Rc::new(self.config.clone()),
        };
        bindings::install(&vm, &ctx).map_err(|e| RuntimeError::Setup(e.to_string()))?;
        if let Some(init) = &self.init_script {
            let chunk = self
                .runtime
                .cache()
                .load_function(vm.lua(), init)
                .map_err(|e| RuntimeError::Setup(e.to_string()))?;
            vm.run(|_| chunk.call::<()>(()))?;
        }
        Ok((vm, response))
    }

    /// A throwaway writer for VMs whose output nobody reads (probes).
    fn null_writer() -> ResponseWriter {
        ResponseWriter::new(Box::new(|_, _| {}), Box::new(|_| Ok(())))
    }

    /// Boot-time probe: runs the init script under the request-VM limit
    /// profile (an honest canary) and reports which hooks it defines. An
    /// init-script *error* is fatal — the caller refuses startup (fail
    /// closed); a hook genuinely not defined is the legitimate no-preflight
    /// mode.
    /// The limit profile request VMs run under — the host derives its own
    /// budgets (e.g. the body-channel write bound) from the same clock.
    pub fn limits(&self) -> crate::limits::LimitConfig {
        self.runtime.limits()
    }

    pub fn probe_hooks(&self) -> Result<HookProbes, RuntimeError> {
        let (vm, _response) = self.vm_with_bindings(RequestData::default(), Self::null_writer())?;
        let is_function = |name: &str| -> bool {
            matches!(
                vm.lua().globals().get::<Value>(name),
                Ok(Value::Function(_))
            )
        };
        Ok(HookProbes {
            pre_flight: is_function("pre_flight"),
            file_pre_flight: is_function("file_pre_flight"),
        })
    }

    /// The IIIF `pre_flight` hook: `pre_flight(prefix, identifier, cookie)`.
    pub fn preflight(
        &self,
        req: RequestData,
        prefix: &str,
        identifier: &str,
    ) -> Result<PreflightReply, PreflightFailure> {
        let cookie = raw_cookie_header(&req);
        self.run_hook(
            req,
            "pre_flight",
            (prefix.to_string(), identifier.to_string(), cookie),
            /*extended=*/ true,
        )
    }

    /// The `/file` `file_pre_flight` hook: `file_pre_flight(filepath, cookie)`
    /// (narrower permission set — no clickthrough/kiosk/external).
    pub fn file_preflight(
        &self,
        req: RequestData,
        filepath: &str,
    ) -> Result<PreflightReply, PreflightFailure> {
        let cookie = raw_cookie_header(&req);
        self.run_hook(
            req,
            "file_pre_flight",
            (filepath.to_string(), cookie),
            /*extended=*/ false,
        )
    }

    fn run_hook(
        &self,
        req: RequestData,
        funcname: &str,
        args: impl mlua::IntoLuaMulti,
        extended: bool,
    ) -> Result<PreflightReply, PreflightFailure> {
        // The hook's own response, buffered (a preflight direct response is a
        // script's error/redirect page, never a streamed body).
        let head: Head = Rc::default();
        let body: Rc<RefCell<Vec<u8>>> = Rc::default();
        let commit_head = Rc::clone(&head);
        let write_body = Rc::clone(&body);
        let writer = ResponseWriter::new(
            Box::new(move |status, headers| {
                *commit_head.borrow_mut() = Some((status, headers));
            }),
            Box::new(move |data| {
                write_body.borrow_mut().extend_from_slice(data);
                Ok(())
            }),
        );

        let (vm, response) = self.vm_with_bindings(req, writer).map_err(|e| match e {
            RuntimeError::Killed(k) => PreflightFailure::Killed(k),
            other => PreflightFailure::Error(other.to_string()),
        })?;

        let result = vm.run(|lua| {
            // Fetching the hook inside the run: a hook that vanished after the
            // boot-frozen probe is a request-time error (fail closed).
            let hook: mlua::Function = lua.globals().get(funcname)?;
            hook.call::<MultiValue>(args)
        });

        let rvals = match result {
            Err(RuntimeError::Killed(k)) => return Err(PreflightFailure::Killed(k)),
            Err(other) => {
                // A hook that failed but already answered the request itself
                // still resolves to its direct response (the historical
                // `return false` after `sendStatus`).
                if response.borrow().status_was_set() {
                    return Ok(direct_reply(&response, &head, &body));
                }
                tracing::error!(hook = funcname, error = %other, "Lua hook failed");
                return Err(PreflightFailure::Error(other.to_string()));
            }
            Ok(rvals) => rvals,
        };

        // A written response wins over the returned values.
        if response.borrow().status_was_set() {
            return Ok(direct_reply(&response, &head, &body));
        }

        match parse_preflight_values(&rvals, funcname, extended) {
            Ok((permission, kv)) => Ok(PreflightReply::Decision { permission, kv }),
            Err(msg) => {
                tracing::error!(hook = funcname, "{msg}");
                Err(PreflightFailure::Error(msg))
            }
        }
    }
}

fn raw_cookie_header(req: &RequestData) -> String {
    req.headers
        .iter()
        .find(|(name, _)| name == "cookie")
        .map(|(_, value)| value.clone())
        .unwrap_or_default()
}

type Head = Rc<RefCell<Option<(u16, Vec<(String, String)>)>>>;

fn direct_reply(
    response: &Rc<RefCell<ResponseWriter>>,
    head: &Head,
    body: &Rc<RefCell<Vec<u8>>>,
) -> PreflightReply {
    // Committed (the hook printed a body): the captured head + body.
    // Uncommitted (sendStatus/sendHeader only): the writer's snapshot.
    let (status, headers) = head
        .borrow()
        .clone()
        .unwrap_or_else(|| response.borrow().head_snapshot());
    PreflightReply::Direct {
        status,
        headers,
        body: std::mem::take(&mut body.borrow_mut()),
    }
}

/// The permission vocabulary: base set + the IIIF-only extensions.
fn valid_permission(s: &str, extended: bool) -> bool {
    matches!(s, "allow" | "login" | "restrict" | "deny")
        || (extended && matches!(s, "clickthrough" | "kiosk" | "external"))
}

/// Port of the preflight return-shape parsing: the permission is a bare
/// string or a table carrying `type` plus extra string keys; every non-deny
/// permission requires an infile string as the second value.
fn parse_preflight_values(
    rvals: &MultiValue,
    funcname: &str,
    extended: bool,
) -> Result<(String, Vec<(String, String)>), String> {
    let mut kv: Vec<(String, String)> = Vec::new();
    let Some(perm_val) = rvals.iter().next() else {
        return Err(format!(
            "Lua function {funcname} must return at least one value"
        ));
    };
    let permission = match perm_val {
        Value::String(s) => s.to_string_lossy(),
        Value::Table(t) => {
            let type_val: Value = t.get("type").map_err(|e| e.to_string())?;
            let Value::String(type_str) = type_val else {
                return Err(if matches!(type_val, Value::Nil) {
                    format!("The permission value returned by Lua function {funcname} has no type field!")
                } else {
                    format!("The permission 'type' returned by Lua function {funcname} must be a string")
                });
            };
            for pair in t.pairs::<Value, Value>() {
                let (key, value) = pair.map_err(|e| e.to_string())?;
                let Value::String(key) = key else { continue };
                let key = key.to_string_lossy();
                if key == "type" {
                    continue;
                }
                let Value::String(value) = value else {
                    return Err(format!(
                        "The '{key}' value returned by Lua function {funcname} must be a string"
                    ));
                };
                kv.push((key, value.to_string_lossy()));
            }
            type_str.to_string_lossy()
        }
        _ => {
            return Err(format!(
                "The permission value returned by Lua function {funcname} was not valid"
            ));
        }
    };

    if !valid_permission(&permission, extended) {
        return Err(format!(
            "The permission returned by Lua function {funcname} is not valid: {permission}"
        ));
    }

    if permission == "deny" {
        kv.push(("infile".to_string(), String::new()));
    } else {
        let Some(infile_val) = rvals.iter().nth(1) else {
            return Err(format!(
                "Lua function {funcname} returned other permission than 'deny', but it did not return a file path"
            ));
        };
        let Value::String(infile) = infile_val else {
            return Err(format!(
                "The file path returned by Lua function {funcname} was not a string"
            ));
        };
        kv.push(("infile".to_string(), infile.to_string_lossy()));
    }

    Ok((permission, kv))
}

/// Why a route run produced no (complete) response.
#[derive(Debug)]
pub enum RouteFailure {
    /// The routed script is missing on disk — a request-time 404, never a
    /// boot failure (production configs may route deleted scripts).
    NotFound,
    Killed(KillReason),
    /// Logged here; the caller maps it to a bare 500 (pre-commit) or a
    /// stream truncation (post-commit).
    Error(String),
}

/// The result of one route/docroot script run. `committed` is the
/// pre-/post-commit kill boundary; `head` is the buffered head for a
/// body-less completion (status defaults to 200, as the transport did).
pub struct RouteOutcome {
    pub result: Result<(), RouteFailure>,
    pub committed: bool,
    pub head: (u16, Vec<(String, String)>),
}

impl LuaEnv {
    /// Runs a configured route or docroot script (`.lua` whole-chunk or
    /// `.elua` HTML/Lua interleave) against the given response closures.
    /// `.lua` chunks load through the bytecode cache; `.elua` sources are
    /// read per request (mixed HTML is not a cacheable chunk).
    pub fn run_route(
        &self,
        req: RequestData,
        script: &Path,
        commit: bindings::CommitFn,
        write: bindings::WriteFn,
    ) -> RouteOutcome {
        let writer = ResponseWriter::new(commit, write);
        let (vm, response) = match self.vm_with_bindings(req, writer) {
            Ok(pair) => pair,
            Err(RuntimeError::Killed(k)) => {
                return RouteOutcome {
                    result: Err(RouteFailure::Killed(k)),
                    committed: false,
                    head: (500, Vec::new()),
                };
            }
            Err(other) => {
                tracing::error!(error = %other, "route VM setup failed");
                return RouteOutcome {
                    result: Err(RouteFailure::Error(other.to_string())),
                    committed: false,
                    head: (500, Vec::new()),
                };
            }
        };

        let ext = script
            .extension()
            .and_then(|e| e.to_str())
            .unwrap_or_default()
            .to_ascii_lowercase();
        let result = match ext.as_str() {
            "lua" => match self.runtime.cache().load_function(vm.lua(), script) {
                Err(crate::runtime::LoadError::NotFound(_)) => Err(RouteFailure::NotFound),
                Err(e) => {
                    tracing::error!(error = %e, "route script failed to load");
                    Err(RouteFailure::Error(e.to_string()))
                }
                Ok(chunk) => vm.run(|_| chunk.call::<()>(())).map_err(route_failure),
            },
            "elua" => run_elua(&vm, &response, script),
            other => {
                tracing::error!(extension = other, "route script has no valid extension");
                Err(RouteFailure::Error(format!(
                    "script has no valid extension '{other}'"
                )))
            }
        };

        // Drop the VM first: the binding closures hold the response Rc, so the
        // writer is only unwrappable once the Lua state is gone.
        drop(vm);
        let Ok(cell) = Rc::try_unwrap(response) else {
            unreachable!("the dropped VM held the only other response references");
        };
        let writer = cell.into_inner();
        RouteOutcome {
            committed: writer.committed(),
            head: writer.head_snapshot(),
            result,
        }
    }
}

fn route_failure(e: RuntimeError) -> RouteFailure {
    match e {
        RuntimeError::Killed(k) => RouteFailure::Killed(k),
        other => {
            tracing::error!(error = %other, "route script failed");
            RouteFailure::Error(other.to_string())
        }
    }
}

/// The `.elua` interleave: raw HTML alternates with `<lua>…</lua>` chunks
/// (literal, case-sensitive delimiters). HTML segments write to the response
/// (committing the head on the first byte); every chunk runs in the same VM,
/// so state set by one chunk is visible to later ones. A chunk error aborts
/// the whole route. An unterminated `<lua>` executes the remainder as the
/// final chunk and then fails the route (the historical behavior errored
/// there too, via an out-of-range substr).
fn run_elua(
    vm: &RequestVm,
    response: &Rc<RefCell<ResponseWriter>>,
    script: &Path,
) -> Result<(), RouteFailure> {
    let code = match std::fs::read(script) {
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
            return Err(RouteFailure::NotFound);
        }
        Err(e) => return Err(RouteFailure::Error(e.to_string())),
        Ok(bytes) => bytes,
    };
    let name = format!("@{}", script.display());
    let mut cursor = 0usize;
    loop {
        let Some(open) = find(&code, b"<lua>", cursor) else {
            break;
        };
        let html = &code[cursor..open];
        if !html.is_empty() && response.borrow_mut().write(html).is_err() {
            return Err(RouteFailure::Error("client gone".into()));
        }
        let chunk_start = open + 5;
        let (chunk, terminated) = match find(&code, b"</lua>", chunk_start) {
            Some(close) => {
                cursor = close + 6;
                (&code[chunk_start..close], true)
            }
            None => (&code[chunk_start..], false),
        };
        vm.run(|lua| {
            lua.load(chunk)
                .set_name(name.as_str())
                .set_mode(mlua::chunk::ChunkMode::Text)
                .exec()
        })
        .map_err(route_failure)?;
        if !terminated {
            return Err(RouteFailure::Error("unterminated <lua> chunk".into()));
        }
    }
    let tail = &code[cursor..];
    if !tail.is_empty() && response.borrow_mut().write(tail).is_err() {
        return Err(RouteFailure::Error("client gone".into()));
    }
    Ok(())
}

fn find(haystack: &[u8], needle: &[u8], from: usize) -> Option<usize> {
    haystack
        .get(from..)?
        .windows(needle.len())
        .position(|w| w == needle)
        .map(|i| from + i)
}
