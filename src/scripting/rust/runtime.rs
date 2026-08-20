//! The VM factory: hardened profile, bytecode cache, binding chokepoint.
//!
//! Two VM kinds exist and stay distinct:
//! - [`ScriptRuntime::request_vm`] — the per-request VM: stdlib whitelist,
//!   base scrub, `os` shim, script-dir-restricted `require`, memory cap,
//!   deadline hook (re-armed at every instruction on expiry), and the
//!   checked-entry chokepoint every binding registers through.
//! - [`config_vm`] — the startup config-parse VM: same whitelist and scrub,
//!   plain `os` shim, no request limits (trusted startup path).
//!
//! The whitelist invariants scripts depend on: the string metatable stays
//! linked to the `string` table, `table` stays mutable, `_G` is writable and
//! shared between the init script and the route/hook chunk within one VM —
//! isolation is *across* requests only (a fresh VM per request).

use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::CStr;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::SystemTime;

use mlua::chunk::ChunkMode;
use mlua::{
    Error, Function, HookTriggers, Lua, LuaOptions, MultiValue, StdLib, Table, Value, VmState,
};

use crate::limits::{kill_stats, Deadline, Kill, KillReason, LimitConfig, RuntimeError};

/// The stdlib whitelist: `io` and `debug` are never loaded; `os` is replaced
/// by the Rust shim. mlua's safe mode stubs `package.loadlib` to an erroring
/// function and disables the C-library searchers; the base scrub removes
/// `loadlib` outright so the surface is absent, not just disabled.
fn whitelist() -> StdLib {
    StdLib::STRING | StdLib::TABLE | StdLib::MATH | StdLib::UTF8 | StdLib::PACKAGE
}

/// Base-library escape hatches scrubbed from every VM. The `load` scrub also
/// closes the attacker-supplied-bytecode hole the bytecode cache would
/// otherwise open (binary chunks stay loadable by the host, not by scripts).
const BASE_SCRUB: [&str; 4] = ["dofile", "loadfile", "load", "collectgarbage"];

fn setup_err(e: impl std::fmt::Display) -> RuntimeError {
    RuntimeError::Setup(e.to_string())
}

/// Builds a whitelisted, scrubbed VM with no limits attached yet.
fn base_vm() -> mlua::Result<Lua> {
    let lua = Lua::new_with(whitelist(), LuaOptions::new().catch_rust_panics(false))?;
    {
        let globals = lua.globals();
        for name in BASE_SCRUB {
            globals.set(name, Value::Nil)?;
        }
        let package: Table = globals.get("package")?;
        package.set("loadlib", Value::Nil)?;
        package.set("searchpath", Value::Nil)?;
        package.set("path", "")?;
        package.set("cpath", "")?;
    }
    Ok(lua)
}

/// The startup config-parse VM: whitelist + scrub + plain `os` shim
/// (config files legitimately read deployment env via `os.getenv`), but no
/// request limits — the config file is the trusted startup path.
pub fn config_vm() -> Result<Lua, RuntimeError> {
    let lua = base_vm().map_err(setup_err)?;
    let os = lua.create_table().map_err(setup_err)?;
    os.set("getenv", lua.create_function(os_getenv).map_err(setup_err)?)
        .map_err(setup_err)?;
    os.set("clock", lua.create_function(os_clock).map_err(setup_err)?)
        .map_err(setup_err)?;
    os.set("date", lua.create_function(os_date).map_err(setup_err)?)
        .map_err(setup_err)?;
    lua.globals().set("os", os).map_err(setup_err)?;
    Ok(lua)
}

/// Compiled-chunk cache: init script, route scripts, and `require`d modules
/// compile once and load per VM as binary chunks (`Function::dump` with debug
/// info kept, for error-message-shape parity). Invalidated by mtime + size,
/// so a script edit takes effect on the next request.
#[derive(Default)]
pub struct BytecodeCache {
    chunks: Mutex<HashMap<PathBuf, Arc<CachedChunk>>>,
    hits: AtomicU64,
    misses: AtomicU64,
}

struct CachedChunk {
    mtime: SystemTime,
    len: u64,
    bytecode: Vec<u8>,
}

/// Why a chunk failed to load. `NotFound` stays distinct so a routed script
/// missing on disk can map to a request-time 404, never a boot failure.
#[derive(Debug)]
pub enum LoadError {
    NotFound(PathBuf),
    Io(PathBuf, String),
    Compile(String),
}

impl std::fmt::Display for LoadError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            LoadError::NotFound(p) => write!(f, "script not found: {}", p.display()),
            LoadError::Io(p, e) => write!(f, "cannot read script {}: {e}", p.display()),
            LoadError::Compile(e) => write!(f, "{e}"),
        }
    }
}

impl std::error::Error for LoadError {}

impl BytecodeCache {
    pub fn new() -> Self {
        Self::default()
    }

    /// Loads `path` into `lua` as a callable chunk, compiling on first use or
    /// after an mtime/size change and loading the cached bytecode otherwise.
    pub fn load_function(&self, lua: &Lua, path: &Path) -> Result<Function, LoadError> {
        let meta = std::fs::metadata(path).map_err(|e| {
            if e.kind() == std::io::ErrorKind::NotFound {
                LoadError::NotFound(path.to_path_buf())
            } else {
                LoadError::Io(path.to_path_buf(), e.to_string())
            }
        })?;
        let mtime = meta
            .modified()
            .map_err(|e| LoadError::Io(path.to_path_buf(), e.to_string()))?;
        let len = meta.len();
        let name = format!("@{}", path.display());

        let cached = {
            let chunks = self.chunks.lock().expect("bytecode cache poisoned");
            chunks.get(path).cloned()
        };
        if let Some(chunk) = cached {
            if chunk.mtime == mtime && chunk.len == len {
                self.hits.fetch_add(1, Ordering::Relaxed);
                return lua
                    .load(chunk.bytecode.as_slice())
                    .set_name(name.as_str())
                    .set_mode(ChunkMode::Binary)
                    .into_function()
                    .map_err(|e| LoadError::Compile(e.to_string()));
            }
        }

        self.misses.fetch_add(1, Ordering::Relaxed);
        let mut source =
            std::fs::read(path).map_err(|e| LoadError::Io(path.to_path_buf(), e.to_string()))?;
        // Like luaL_loadfilex: a leading `#!` line is skipped.
        if source.starts_with(b"#") {
            let end = source
                .iter()
                .position(|&b| b == b'\n')
                .map_or(source.len(), |i| i);
            source.drain(..end);
        }
        let func = lua
            .load(source.as_slice())
            .set_name(name.as_str())
            .set_mode(ChunkMode::Text)
            .into_function()
            .map_err(|e| LoadError::Compile(e.to_string()))?;
        let bytecode = func.dump(false);
        self.chunks.lock().expect("bytecode cache poisoned").insert(
            path.to_path_buf(),
            Arc::new(CachedChunk {
                mtime,
                len,
                bytecode,
            }),
        );
        Ok(func)
    }

    pub fn hits(&self) -> u64 {
        self.hits.load(Ordering::Relaxed)
    }

    pub fn misses(&self) -> u64 {
        self.misses.load(Ordering::Relaxed)
    }
}

/// The long-lived runtime: limits, script dir, and the shared bytecode cache.
/// Builds one hardened [`RequestVm`] per request.
pub struct ScriptRuntime {
    script_dir: PathBuf,
    limits: LimitConfig,
    cache: Arc<BytecodeCache>,
}

impl ScriptRuntime {
    pub fn new(script_dir: PathBuf, limits: LimitConfig) -> Self {
        Self {
            script_dir,
            limits,
            cache: Arc::new(BytecodeCache::new()),
        }
    }

    pub fn cache(&self) -> &Arc<BytecodeCache> {
        &self.cache
    }

    pub fn script_dir(&self) -> &Path {
        &self.script_dir
    }

    pub fn limits(&self) -> LimitConfig {
        self.limits
    }

    /// Builds a fresh hardened request VM with the deadline armed. The
    /// deadline covers the whole VM lifetime: init execution, the hook or
    /// route chunk, and every binding call.
    pub fn request_vm(&self) -> Result<RequestVm, RuntimeError> {
        let lua = base_vm().map_err(setup_err)?;
        lua.set_memory_limit(self.limits.memory_limit)
            .map_err(setup_err)?;
        let deadline = Deadline::starting_now(self.limits.timeout);
        arm_deadline_hook(&lua, deadline.clone(), self.limits.hook_period).map_err(setup_err)?;
        let vm = RequestVm {
            lua,
            deadline,
            registry: RefCell::new(Vec::new()),
        };
        vm.install_os_shim().map_err(setup_err)?;
        vm.install_require(self.script_dir.clone(), Arc::clone(&self.cache))
            .map_err(setup_err)?;
        Ok(vm)
    }
}

/// Arms the instruction-count hook. On expiry the hook re-arms itself at
/// every instruction before raising the kill, so a `pcall`-trapped script
/// cannot make useful progress: any instruction executed in an unprotected
/// frame re-raises, which terminates every trap loop.
fn arm_deadline_hook(lua: &Lua, deadline: Deadline, period: u32) -> mlua::Result<()> {
    lua.set_hook(
        HookTriggers::new().every_nth_instruction(period.max(1)),
        move |lua, _| {
            if deadline.poll_expired() {
                lua.set_hook(HookTriggers::new().every_nth_instruction(1), |_, _| {
                    Err(Error::external(Kill(KillReason::Timeout)))
                })?;
                return Err(Error::external(Kill(KillReason::Timeout)));
            }
            Ok(VmState::Continue)
        },
    )
}

/// One per-request VM: the hardened `Lua`, its deadline, and the registry of
/// bindings that entered through the chokepoint.
pub struct RequestVm {
    lua: Lua,
    deadline: Deadline,
    registry: RefCell<Vec<(String, String)>>,
}

impl RequestVm {
    pub fn lua(&self) -> &Lua {
        &self.lua
    }

    pub fn deadline(&self) -> &Deadline {
        &self.deadline
    }

    /// The checked-entry chokepoint: every binding registers through here,
    /// so its first action is the deadline check — a trapped timeout error
    /// can never reach binding I/O. The registration is recorded;
    /// [`Self::verify_bindings_checked`] enumerates binding tables against
    /// the record.
    pub fn register_binding<F, A, R>(
        &self,
        table_path: &str,
        table: &Table,
        name: &str,
        f: F,
    ) -> mlua::Result<()>
    where
        F: Fn(&Lua, A) -> mlua::Result<R> + mlua::MaybeSend + 'static,
        A: mlua::FromLuaMulti,
        R: mlua::IntoLuaMulti,
    {
        let deadline = self.deadline.clone();
        let wrapped = self.lua.create_function(move |lua, args: A| {
            deadline.check()?;
            f(lua, args)
        })?;
        table.set(name, wrapped)?;
        self.registry
            .borrow_mut()
            .push((table_path.to_string(), name.to_string()));
        Ok(())
    }

    /// Asserts every function in the given binding tables entered through
    /// [`Self::register_binding`]. A binding installed any other way is a
    /// structural error — the deadline chokepoint would be bypassable.
    pub fn verify_bindings_checked(&self, table_paths: &[&str]) -> Result<(), String> {
        let registry = self.registry.borrow();
        for path in table_paths {
            let table = self.resolve_table(path)?;
            for pair in table.pairs::<Value, Value>() {
                let (key, value) = pair.map_err(|e| e.to_string())?;
                if !matches!(value, Value::Function(_)) {
                    continue;
                }
                let Value::String(key) = key else {
                    return Err(format!("non-string function key in {path}"));
                };
                let name = key.to_string_lossy().to_string();
                if !registry
                    .iter()
                    .any(|(p, n)| p == path && n.as_str() == name)
                {
                    return Err(format!(
                        "binding {path}.{name} bypasses the checked-entry chokepoint"
                    ));
                }
            }
        }
        Ok(())
    }

    fn resolve_table(&self, dotted: &str) -> Result<Table, String> {
        let mut table: Table = self.lua.globals();
        for part in dotted.split('.') {
            table = table
                .get::<Table>(part)
                .map_err(|_| format!("binding table {dotted} not found at {part}"))?;
        }
        Ok(table)
    }

    /// Runs VM work and classifies the outcome into the `Send`-able domain
    /// error: an expired deadline is a timeout kill even if the script
    /// trapped the error and returned normally; a memory error anywhere in
    /// the cause chain is a memory kill. Kills are counted and logged here.
    pub fn run<T>(&self, work: impl FnOnce(&Lua) -> mlua::Result<T>) -> Result<T, RuntimeError> {
        let started = std::time::Instant::now();
        let result = work(&self.lua);
        let kill = |reason: KillReason| {
            kill_stats().record(reason);
            tracing::warn!(
                reason = reason.as_str(),
                elapsed_ms = started.elapsed().as_millis() as u64,
                lua_memory_bytes = self.lua.used_memory() as u64,
                "lua script killed"
            );
            RuntimeError::Killed(reason)
        };
        match result {
            Ok(value) => {
                if self.deadline.poll_expired() {
                    return Err(kill(KillReason::Timeout));
                }
                Ok(value)
            }
            Err(e) => {
                if self.deadline.poll_expired() {
                    return Err(kill(KillReason::Timeout));
                }
                if is_memory_error(&e) {
                    return Err(kill(KillReason::Memory));
                }
                Err(RuntimeError::Script(e.to_string()))
            }
        }
    }

    fn install_os_shim(&self) -> mlua::Result<()> {
        let os = self.lua.create_table()?;
        self.register_binding("os", &os, "getenv", os_getenv)?;
        self.register_binding("os", &os, "clock", os_clock)?;
        self.register_binding("os", &os, "date", os_date)?;
        self.lua.globals().set("os", os)
    }

    /// Replaces `package.searchers` with a single loader resolving
    /// `[A-Za-z0-9_]+` module names against the script dir only, loading
    /// through the bytecode cache.
    fn install_require(&self, script_dir: PathBuf, cache: Arc<BytecodeCache>) -> mlua::Result<()> {
        let deadline = self.deadline.clone();
        let searcher = self.lua.create_function(move |lua, name: String| {
            deadline.check()?;
            if !is_valid_module_name(&name) {
                let msg =
                    format!("\n\tmodule name '{name}' not permitted (script-dir modules only)");
                return Ok(MultiValue::from_iter([Value::String(
                    lua.create_string(&msg)?,
                )]));
            }
            let path = script_dir.join(format!("{name}.lua"));
            match cache.load_function(lua, &path) {
                Ok(loader) => Ok(MultiValue::from_iter([
                    Value::Function(loader),
                    Value::String(lua.create_string(path.display().to_string())?),
                ])),
                Err(LoadError::NotFound(p)) => {
                    let msg = format!("\n\tno file '{}'", p.display());
                    Ok(MultiValue::from_iter([Value::String(
                        lua.create_string(&msg)?,
                    )]))
                }
                Err(e) => Err(Error::runtime(e.to_string())),
            }
        })?;
        let searchers = self.lua.create_table()?;
        searchers.set(1, searcher)?;
        let package: Table = self.lua.globals().get("package")?;
        package.set("searchers", searchers)
    }
}

fn is_valid_module_name(name: &str) -> bool {
    !name.is_empty() && name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
}

fn is_memory_error(e: &Error) -> bool {
    match e {
        Error::MemoryError(_) => true,
        Error::CallbackError { cause, .. } => is_memory_error(cause),
        Error::WithContext { cause, .. } => is_memory_error(cause),
        _ => false,
    }
}

// ── The `os` shim ───────────────────────────────────────────────────────────
// The frozen audited surface: `getenv` + `clock` + `date`, reimplemented in
// Rust. `getenv` is deliberately unrestricted — scripts legitimately read
// deployment env. `date` is a strftime subset formatted in Rust (never a
// passthrough to C strftime); only the civil-time breakdown uses libc.

fn os_getenv(lua: &Lua, name: String) -> mlua::Result<Value> {
    match std::env::var_os(&name) {
        Some(value) => Ok(Value::String(
            lua.create_string(value.to_string_lossy().as_bytes())?,
        )),
        None => Ok(Value::Nil),
    }
}

#[allow(clippy::unnecessary_wraps)]
fn os_clock(_: &Lua, (): ()) -> mlua::Result<f64> {
    // Process CPU time, like C `clock()/CLOCKS_PER_SEC`.
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    // SAFETY: ts is a valid timespec for the duration of the call.
    let rc = unsafe { libc::clock_gettime(libc::CLOCK_PROCESS_CPUTIME_ID, &mut ts) };
    if rc != 0 {
        return Err(Error::runtime("clock_gettime failed"));
    }
    Ok(ts.tv_sec as f64 + ts.tv_nsec as f64 / 1_000_000_000.0)
}

fn os_date(lua: &Lua, (fmt, time): (Option<String>, Option<i64>)) -> mlua::Result<Value> {
    let fmt = fmt.unwrap_or_else(|| "%c".to_string());
    let t = time.unwrap_or_else(|| {
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .map_or(0, |d| d.as_secs() as i64)
    });
    let (utc, spec) = match fmt.strip_prefix('!') {
        Some(rest) => (true, rest),
        None => (false, fmt.as_str()),
    };
    let tm = civil_breakdown(t, utc)
        .ok_or_else(|| Error::runtime("time value cannot be represented"))?;
    if spec == "*t" {
        let out = lua.create_table()?;
        out.set("year", tm.tm_year as i64 + 1900)?;
        out.set("month", tm.tm_mon + 1)?;
        out.set("day", tm.tm_mday)?;
        out.set("hour", tm.tm_hour)?;
        out.set("min", tm.tm_min)?;
        out.set("sec", tm.tm_sec)?;
        out.set("wday", tm.tm_wday + 1)?;
        out.set("yday", tm.tm_yday + 1)?;
        out.set("isdst", tm.tm_isdst > 0)?;
        return Ok(Value::Table(out));
    }
    let rendered = format_tm(spec, &tm).map_err(Error::runtime)?;
    Ok(Value::String(lua.create_string(&rendered)?))
}

fn civil_breakdown(t: i64, utc: bool) -> Option<libc::tm> {
    extern "C" {
        // POSIX tzset (not re-exported by the libc crate for unix targets).
        fn tzset();
    }
    static TZSET: OnceLock<()> = OnceLock::new();
    TZSET.get_or_init(|| {
        // SAFETY: tzset has no preconditions; called once before localtime_r.
        unsafe { tzset() }
    });
    let tt = t as libc::time_t;
    // SAFETY: zeroed libc::tm is a valid initial value for the _r functions,
    // which fully overwrite it on success.
    let mut tm: libc::tm = unsafe { std::mem::zeroed() };
    // SAFETY: tt and tm are valid for the duration of the call; the _r
    // variants are thread-safe and write only through the provided pointer.
    let ok = unsafe {
        if utc {
            libc::gmtime_r(&tt, &mut tm)
        } else {
            libc::localtime_r(&tt, &mut tm)
        }
    };
    (!ok.is_null()).then_some(tm)
}

const WDAY_ABBR: [&str; 7] = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const WDAY_FULL: [&str; 7] = [
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
];
const MON_ABBR: [&str; 12] = [
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
];
const MON_FULL: [&str; 12] = [
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December",
];

/// The strftime subset, C-locale shaped. An unsupported specifier is a Lua
/// error (fail loud), matching Lua 5.3's own specifier validation.
fn format_tm(fmt: &str, tm: &libc::tm) -> Result<String, String> {
    let mut out = String::with_capacity(fmt.len() * 2);
    let mut chars = fmt.chars();
    while let Some(c) = chars.next() {
        if c != '%' {
            out.push(c);
            continue;
        }
        let Some(spec) = chars.next() else {
            return Err("invalid conversion specifier '%'".to_string());
        };
        let wday = tm.tm_wday.rem_euclid(7) as usize;
        let mon = tm.tm_mon.rem_euclid(12) as usize;
        match spec {
            'Y' => out.push_str(&(tm.tm_year as i64 + 1900).to_string()),
            'y' => out.push_str(&format!(
                "{:02}",
                (tm.tm_year as i64 + 1900).rem_euclid(100)
            )),
            'm' => out.push_str(&format!("{:02}", tm.tm_mon + 1)),
            'd' => out.push_str(&format!("{:02}", tm.tm_mday)),
            'e' => out.push_str(&format!("{:2}", tm.tm_mday)),
            'H' => out.push_str(&format!("{:02}", tm.tm_hour)),
            'I' => {
                let h = tm.tm_hour % 12;
                out.push_str(&format!("{:02}", if h == 0 { 12 } else { h }));
            }
            'M' => out.push_str(&format!("{:02}", tm.tm_min)),
            'S' => out.push_str(&format!("{:02}", tm.tm_sec)),
            'p' => out.push_str(if tm.tm_hour < 12 { "AM" } else { "PM" }),
            'a' => out.push_str(WDAY_ABBR[wday]),
            'A' => out.push_str(WDAY_FULL[wday]),
            'b' => out.push_str(MON_ABBR[mon]),
            'B' => out.push_str(MON_FULL[mon]),
            'j' => out.push_str(&format!("{:03}", tm.tm_yday + 1)),
            'w' => out.push_str(&tm.tm_wday.to_string()),
            'c' => out.push_str(&format_tm("%a %b %e %H:%M:%S %Y", tm)?),
            'x' => out.push_str(&format_tm("%m/%d/%y", tm)?),
            'X' => out.push_str(&format_tm("%H:%M:%S", tm)?),
            'Z' => {
                if !tm.tm_zone.is_null() {
                    // SAFETY: tm_zone, when non-null, points at a NUL-terminated
                    // timezone abbreviation owned by the C runtime.
                    let zone = unsafe { CStr::from_ptr(tm.tm_zone as *const libc::c_char) };
                    out.push_str(&zone.to_string_lossy());
                }
            }
            '%' => out.push('%'),
            other => return Err(format!("invalid conversion specifier '%{other}'")),
        }
    }
    Ok(out)
}
