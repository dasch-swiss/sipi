//! Cache flags (the "Cache" `--help` heading).
//!
//! `cache_size` is a sized string ("-1" unlimited, "0" disabled, or e.g.
//! "200M") parsed engine-side. The deprecated `--cachedir` / `--cachesize` /
//! `--cachenfiles` aliases mirror the C++ oracle's separate CLI11 options
//! (`cli_app.cpp:1776/1780/1784`, each with its own `->envname` binding onto
//! the same underlying variable as the canonical flag) — declared here as
//! hidden fields (a `visible_alias` doesn't apply: they differ in hyphen shape
//! from the canonical long names) and collapsed onto the canonical field in
//! `commands/server/mod.rs`'s `From<&ServerArgs>` (canonical wins if both are
//! set). `--cachehysteresis` is intentionally never declared — an unknown flag
//! rejection is the Rust-side equivalent of the C++ no-op.

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
    /// Max number of cached files (0 = no limit). Unsigned: a negative is
    /// rejected (matches the C++ CLI; no signed→unsigned wrap).
    #[arg(long, env = "SIPI_CACHE_NFILES", value_name = "N")]
    pub cache_nfiles: Option<u32>,
    /// DEPRECATED: use `--cache-dir`.
    #[arg(
        long = "cachedir",
        hide = true,
        env = "SIPI_CACHEDIR",
        value_name = "DIR"
    )]
    pub cachedir: Option<String>,
    /// DEPRECATED: use `--cache-size`.
    #[arg(
        long = "cachesize",
        hide = true,
        env = "SIPI_CACHESIZE",
        value_name = "SIZE"
    )]
    pub cachesize: Option<String>,
    /// DEPRECATED: use `--cache-nfiles`.
    #[arg(
        long = "cachenfiles",
        hide = true,
        env = "SIPI_CACHENFILES",
        value_name = "N"
    )]
    pub cachenfiles: Option<u32>,
}
