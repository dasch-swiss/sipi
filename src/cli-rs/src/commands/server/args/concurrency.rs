//! Concurrency flags (the "Concurrency" `--help` heading).
//!
//! All three configure the engine-work pool directly from CLI/env
//! (`SIPI_NTHREADS`/`SIPI_MAX_WAITING`/`SIPI_QUEUE_TIMEOUT`), handed to
//! `sipi::run`; the Lua config does not set them. `nthreads` sizes the pool
//! (0 or unset = auto-detect from CPU cores). `max_waiting` bounds the wait queue
//! in front of it (default 2×nthreads) and `queue_timeout` bounds how long each
//! request waits before a 503 (default 5s). See `server-rs/routes.rs`'s
//! `AppState::load` and `acquire_or_shed`.

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Concurrency")]
pub struct ConcurrencyArgs {
    /// Worker thread count that sizes the engine-work pool (0 = auto-detect from
    /// CPU cores). Also accepts the `-t` short form.
    #[arg(long, short = 't', env = "SIPI_NTHREADS", value_name = "N")]
    pub nthreads: Option<u32>,
    /// Max requests queued for a worker before 503 (0 = no queue, shed
    /// immediately; default 2×nthreads).
    #[arg(long, env = "SIPI_MAX_WAITING", value_name = "N")]
    pub max_waiting: Option<u64>,
    /// Max seconds a queued request waits for a worker before 503 (default 5).
    #[arg(long, env = "SIPI_QUEUE_TIMEOUT", value_name = "SECS")]
    pub queue_timeout: Option<u32>,
}
