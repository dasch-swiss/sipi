//! Limit knobs, the per-VM execution deadline, and kill accounting.
//!
//! A request VM gets a [`LimitConfig`]-sized Lua-heap cap and wall-clock
//! budget. The [`Deadline`] is shared between the instruction-count hook and
//! every binding's checked entry ([`crate::runtime::RequestVm::register_binding`]),
//! so a `pcall`-trapped timeout error still cannot reach I/O. Kills are
//! counted process-wide in [`KillStats`] (the OTLP `sipi.lua.kills`
//! source, rendered `sipi_lua_kills_total{reason}`) and logged as one structured line at the kill site.

use std::error::Error as StdError;
use std::fmt;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::sync::OnceLock;
use std::time::{Duration, Instant};

/// Default per-VM Lua-heap cap. Sized to cover the dsp-api init-script
/// closure with generous headroom (the closure allocates well under 1 MiB;
/// the cap must comfortably cover init execution plus script working set).
pub const DEFAULT_MEMORY_LIMIT_BYTES: usize = 64 * 1024 * 1024;

/// Default wall-clock budget for one request VM (init execution + hook or
/// route script + bindings).
pub const DEFAULT_TIMEOUT: Duration = Duration::from_secs(5);

/// Default instruction count between deadline checks. Low enough that a
/// tight loop is killed within microseconds of the deadline, high enough
/// that the hook is amortized to noise on the preflight hot path.
pub const DEFAULT_HOOK_PERIOD: u32 = 1_000;

/// The env knobs. Invalid values are startup errors, never silent defaults.
pub const ENV_MEMORY_LIMIT: &str = "SIPI_LUA_MEMORY_LIMIT";
pub const ENV_TIMEOUT_MS: &str = "SIPI_LUA_TIMEOUT_MS";
pub const ENV_HOOK_PERIOD: &str = "SIPI_LUA_HOOK_PERIOD";

/// Per-request-VM resource limits.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LimitConfig {
    /// Lua-heap allocation cap in bytes (`Lua::set_memory_limit`). Counts
    /// Lua-side allocations only; Rust-side allocations (http bodies, json
    /// values, sqlite rows) live outside it.
    pub memory_limit: usize,
    /// Wall-clock budget for the whole VM lifetime (init + script + bindings).
    pub timeout: Duration,
    /// Instructions between deadline checks; re-armed at 1 on expiry.
    pub hook_period: u32,
}

impl Default for LimitConfig {
    fn default() -> Self {
        Self {
            memory_limit: DEFAULT_MEMORY_LIMIT_BYTES,
            timeout: DEFAULT_TIMEOUT,
            hook_period: DEFAULT_HOOK_PERIOD,
        }
    }
}

impl LimitConfig {
    /// Resolves the limits from the `SIPI_LUA_*` env knobs, defaulting each
    /// absent knob. An unparsable value is an error (fail-closed startup).
    pub fn from_env() -> Result<Self, String> {
        fn knob<T: std::str::FromStr>(name: &str) -> Result<Option<T>, String> {
            match std::env::var(name) {
                Ok(raw) => raw
                    .parse::<T>()
                    .map(Some)
                    .map_err(|_| format!("{name}: invalid value {raw:?}")),
                Err(std::env::VarError::NotPresent) => Ok(None),
                Err(e) => Err(format!("{name}: {e}")),
            }
        }
        let mut limits = Self::default();
        if let Some(bytes) = knob::<usize>(ENV_MEMORY_LIMIT)? {
            limits.memory_limit = bytes;
        }
        if let Some(ms) = knob::<u64>(ENV_TIMEOUT_MS)? {
            limits.timeout = Duration::from_millis(ms);
        }
        if let Some(period) = knob::<u32>(ENV_HOOK_PERIOD)? {
            limits.hook_period = period.max(1);
        }
        Ok(limits)
    }
}

/// Why a VM was killed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KillReason {
    /// The wall-clock deadline expired (instruction hook or binding entry).
    Timeout,
    /// The Lua-heap cap was exceeded (`mlua::Error::MemoryError`).
    Memory,
}

impl KillReason {
    /// The metric label value (the `sipi.lua.kills` `reason` attribute).
    pub fn as_str(self) -> &'static str {
        match self {
            KillReason::Timeout => "timeout",
            KillReason::Memory => "memory",
        }
    }
}

impl fmt::Display for KillReason {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

/// The error payload the deadline machinery raises into Lua. Scripts can
/// `pcall`-trap it, but the hook re-arms at every instruction on expiry and
/// every binding entry re-raises it, so no useful progress is possible.
#[derive(Debug)]
pub struct Kill(pub KillReason);

impl fmt::Display for Kill {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "lua script killed: {}", self.0)
    }
}

impl StdError for Kill {}

/// The wall-clock deadline of one request VM, shared between the instruction
/// hook, the binding chokepoint, and the host-side kill classification.
#[derive(Clone)]
pub struct Deadline {
    inner: Arc<DeadlineInner>,
}

struct DeadlineInner {
    at: Instant,
    expired: AtomicBool,
}

impl Deadline {
    /// Arms a deadline `budget` from now.
    pub fn starting_now(budget: Duration) -> Self {
        Self {
            inner: Arc::new(DeadlineInner {
                at: Instant::now() + budget,
                expired: AtomicBool::new(false),
            }),
        }
    }

    /// Checks the wall clock, latching the expired flag. Returns whether the
    /// deadline has passed.
    pub fn poll_expired(&self) -> bool {
        if self.inner.expired.load(Ordering::Relaxed) {
            return true;
        }
        if Instant::now() >= self.inner.at {
            self.inner.expired.store(true, Ordering::Relaxed);
            return true;
        }
        false
    }

    /// Whether the expired flag has latched (no clock read).
    pub fn is_expired(&self) -> bool {
        self.inner.expired.load(Ordering::Relaxed)
    }

    /// The checked-entry chokepoint: raises the timeout kill if expired.
    /// Every binding funnels through this before doing any work.
    pub fn check(&self) -> mlua::Result<()> {
        if self.poll_expired() {
            Err(mlua::Error::external(Kill(KillReason::Timeout)))
        } else {
            Ok(())
        }
    }

    /// Budget left for a blocking binding call (`server.http` total-request
    /// timeout, sqlite busy budget). Zero when expired.
    pub fn remaining(&self) -> Duration {
        self.inner.at.saturating_duration_since(Instant::now())
    }
}

/// Process-wide kill counters, exported as `sipi.lua.kills` (rendered
/// `sipi_lua_kills_total{reason}`).
#[derive(Debug, Default)]
pub struct KillStats {
    timeout: AtomicU64,
    memory: AtomicU64,
}

impl KillStats {
    pub fn record(&self, reason: KillReason) {
        match reason {
            KillReason::Timeout => self.timeout.fetch_add(1, Ordering::Relaxed),
            KillReason::Memory => self.memory.fetch_add(1, Ordering::Relaxed),
        };
    }

    pub fn timeout(&self) -> u64 {
        self.timeout.load(Ordering::Relaxed)
    }

    pub fn memory(&self) -> u64 {
        self.memory.load(Ordering::Relaxed)
    }
}

/// The process-wide [`KillStats`] instance the OTLP bridge reads.
pub fn kill_stats() -> &'static KillStats {
    static STATS: OnceLock<KillStats> = OnceLock::new();
    STATS.get_or_init(KillStats::default)
}

/// Per-sample duration observations the host subscribes to (the OTel
/// histograms live host-side; this crate stays OTel-free). Both closures get
/// the entry-point label (`probe` / `pre_flight` / `file_pre_flight` /
/// `route` / `elua`) and the elapsed seconds.
pub struct DurationRecorder {
    /// One request-VM build: hardened VM + bindings + init script.
    pub vm_build: Box<dyn Fn(&'static str, f64) + Send + Sync>,
    /// One hook/route chunk run (the script itself, after the VM stands).
    pub script: Box<dyn Fn(&'static str, f64) + Send + Sync>,
}

static DURATION_RECORDER: OnceLock<DurationRecorder> = OnceLock::new();

/// Installs the process-wide duration recorder. First call wins; later calls
/// are ignored (set once at boot).
pub fn set_duration_recorder(recorder: DurationRecorder) {
    let _ = DURATION_RECORDER.set(recorder);
}

pub(crate) fn record_vm_build(entry: &'static str, elapsed: std::time::Duration) {
    if let Some(r) = DURATION_RECORDER.get() {
        (r.vm_build)(entry, elapsed.as_secs_f64());
    }
}

pub(crate) fn record_script(entry: &'static str, elapsed: std::time::Duration) {
    if let Some(r) = DURATION_RECORDER.get() {
        (r.script)(entry, elapsed.as_secs_f64());
    }
}

/// The `Send`-able domain error the runtime hands the async shell.
/// `mlua::Error` is `!Send`, so it never crosses a channel; the runtime
/// classifies it into this owned form at the blocking-thread boundary.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RuntimeError {
    /// The VM was killed (timeout or memory); the request maps to the kill
    /// path (pre-commit 500 / post-commit stream abort).
    Killed(KillReason),
    /// The script itself errored (message includes the chunk name).
    Script(String),
    /// VM construction or chunk loading failed before script code ran.
    Setup(String),
}

impl fmt::Display for RuntimeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RuntimeError::Killed(reason) => write!(f, "lua script killed: {reason}"),
            RuntimeError::Script(msg) => write!(f, "lua script error: {msg}"),
            RuntimeError::Setup(msg) => write!(f, "lua runtime setup error: {msg}"),
        }
    }
}

impl StdError for RuntimeError {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn limit_defaults() {
        let limits = LimitConfig::default();
        assert_eq!(limits.memory_limit, DEFAULT_MEMORY_LIMIT_BYTES);
        assert_eq!(limits.timeout, DEFAULT_TIMEOUT);
        assert_eq!(limits.hook_period, DEFAULT_HOOK_PERIOD);
    }

    #[test]
    fn deadline_expires_and_latches() {
        let d = Deadline::starting_now(Duration::ZERO);
        assert!(d.poll_expired());
        assert!(d.is_expired());
        assert!(d.check().is_err());
        assert_eq!(d.remaining(), Duration::ZERO);
    }

    #[test]
    fn deadline_open_before_budget() {
        let d = Deadline::starting_now(Duration::from_secs(3600));
        assert!(!d.poll_expired());
        assert!(d.check().is_ok());
        assert!(d.remaining() > Duration::from_secs(3500));
    }

    #[test]
    fn kill_stats_count_by_reason() {
        let stats = KillStats::default();
        stats.record(KillReason::Timeout);
        stats.record(KillReason::Timeout);
        stats.record(KillReason::Memory);
        assert_eq!(stats.timeout(), 2);
        assert_eq!(stats.memory(), 1);
    }
}
