//! Request/decode limit + admission flags (the "Limits" `--help` heading).
//!
//! `maxpost` and `memory_limit` are sized strings (e.g. "300M", "8G") — they
//! carry the raw string across the seam and the engine parses the suffix (don't
//! pre-parse Rust-side, the size grammar lives in C++).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Limits")]
pub struct LimitsArgs {
    /// Max POST body size, e.g. "300M" (engine parses the suffix).
    #[arg(long, env = "SIPI_MAXPOSTSIZE", value_name = "SIZE")]
    pub maxpost: Option<String>,
    /// Total RAM envelope, e.g. "8G" (0 = auto-detect available RAM; engine
    /// parses the suffix). The full lane's byte cap is derived from this and
    /// `--tiles-memory-ratio`.
    #[arg(long, env = "SIPI_MEMORY_LIMIT", value_name = "SIZE")]
    pub memory_limit: Option<String>,
    /// Admission mode: basic (thread cap only; default) or advanced (also the memory + two-lane caps). Unknown values fall back to basic.
    #[arg(long, env = "SIPI_ADMISSION_MODE", value_name = "MODE")]
    pub admission_mode: Option<String>,
    /// Fraction of the envelope reserved for tiles (0..1); the full lane gets
    /// envelope × (1 − ratio). Defaults to 0.25.
    #[arg(long, env = "SIPI_TILES_MEMORY_RATIO", value_name = "RATIO")]
    pub tiles_memory_ratio: Option<f64>,
    /// Estimated peak-memory threshold in bytes at/above which a decode is a
    /// full-lane decode charged against the budget; below it is a tile decode
    /// that bypasses the budget. Defaults to 32 MiB.
    #[arg(long, env = "SIPI_LARGE_DECODE_THRESHOLD_BYTES", value_name = "BYTES")]
    pub large_decode_threshold_bytes: Option<u64>,
    /// Thumbnail size used within Lua, e.g. "!128,128".
    #[arg(long, env = "SIPI_THUMBSIZE", value_name = "SIZE")]
    pub thumbsize: Option<String>,
}
