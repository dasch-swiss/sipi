//! Concurrency flags (the "Concurrency" `--help` heading).
//!
//! All are Rust-owned serve knobs handed to `sipi::run` (the Lua config does not
//! set them). The first three configure the engine-work pool from CLI/env
//! (`SIPI_NTHREADS`/`SIPI_MAX_WAITING`/`SIPI_QUEUE_TIMEOUT`): `nthreads` sizes the
//! pool (0 or unset = auto-detect from CPU cores), `max_waiting` bounds the wait
//! queue in front of it (default 2×nthreads), `queue_timeout` bounds how long each
//! request waits before a 503 (default 5s). The last two configure the shell's
//! opt-in preflight access-cache (`SIPI_PREFLIGHT_CACHE_TTL`/
//! `SIPI_PREFLIGHT_CACHE_SLOTS`), disabled unless a TTL is set. See
//! `server-rs/routes.rs`'s `AppState::load` / `acquire_or_shed` and
//! `server-rs/preflight_cache.rs`.

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
    /// Seconds a `pre_flight` access decision is cached per (image, credential),
    /// coalescing repeat auth calls for one image (default 0 = disabled; set >0,
    /// e.g. 2, only if the hook decides purely on prefix/identifier/Cookie/Authorization).
    #[arg(long, env = "SIPI_PREFLIGHT_CACHE_TTL", value_name = "SECS")]
    pub preflight_cache_ttl: Option<u32>,
    /// Slot count for the preflight access-cache (default 4096).
    #[arg(long, env = "SIPI_PREFLIGHT_CACHE_SLOTS", value_name = "N")]
    pub preflight_cache_slots: Option<u64>,
}
