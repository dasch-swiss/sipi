//! Cache flags (the "Cache" `--help` heading).
//!
//! `cache_size` is a sized string ("-1" unlimited, "0" disabled, or e.g.
//! "200M") parsed engine-side. The deprecated `--cachedir` / `--cachesize` /
//! `--cachenfiles` aliases (and the no-op `--cachehysteresis`) are NOT declared
//! here: the aliases land in M5 (hidden, collapsed in the override mapping) and
//! `--cachehysteresis` is intentionally hard-rejected on the Rust shell
//! (plan 02 §3 P3 / §7.5 M5).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Cache")]
pub struct CacheArgs {
    /// Cache directory.
    #[arg(long, env = "SIPI_CACHE_DIR", value_name = "DIR")]
    pub cache_dir: Option<String>,
    /// Cache size: "-1" (unlimited), "0" (disabled), or e.g. "200M" (engine
    /// parses the suffix).
    #[arg(long, env = "SIPI_CACHE_SIZE", value_name = "SIZE")]
    pub cache_size: Option<String>,
    /// Max number of cached files (0 = no limit).
    #[arg(long, env = "SIPI_CACHE_NFILES", value_name = "N")]
    pub cache_nfiles: Option<i32>,
}
