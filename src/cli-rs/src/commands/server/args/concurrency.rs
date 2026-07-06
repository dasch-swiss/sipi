//! Concurrency flags (the "Concurrency" `--help` heading).
//!
//! `nthreads`, `max_waiting`, and `queue_timeout` are all parse-only (plan 02
//! §7.5 forward/parse-only split, M7-resolved): the Rust shell bounds
//! concurrent engine work with a tokio semaphore (shed-load → 503) sized from
//! the Lua/TOML config's `nthreads` key (`server-rs/routes.rs`'s
//! `ffi::nthreads()`), not from a CLI/env override — thread count is one of
//! the "transport knobs the shell does not own", grouped with TLS/hostname/
//! keep-alive/logfile in the M5 TOML schema (`server-rs/config_file.rs`), and
//! the CLI stays consistent with that. `max_waiting`/`queue_timeout` are
//! unread for a different reason: the semaphore model has no queue to size
//! (§5 #4).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Concurrency")]
pub struct ConcurrencyArgs {
    /// Worker thread count (0 = auto-detect from CPU cores; parse-only — sizes
    /// the engine-work semaphore only from the Lua/TOML config, not the CLI).
    /// Also accepts the C++ oracle's `-t` short form.
    #[arg(long, short = 't', env = "SIPI_NTHREADS", value_name = "N")]
    pub nthreads: Option<u32>,
    /// Max waiting connections before 503 (parse-only: semaphore model).
    #[arg(long, env = "SIPI_MAX_WAITING", value_name = "N")]
    pub max_waiting: Option<u64>,
    /// Max seconds a request waits in queue before 503 (parse-only).
    #[arg(long, env = "SIPI_QUEUE_TIMEOUT", value_name = "SECS")]
    pub queue_timeout: Option<u32>,
}
