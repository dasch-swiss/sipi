//! Request/decode limit flags (the "Limits" `--help` heading).
//!
//! `maxpost` and `max_decode_memory` are sized strings (e.g. "300M", "2G") —
//! they carry the raw string across the seam and the engine parses the suffix
//! (don't pre-parse Rust-side, the size grammar lives in C++).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Limits")]
pub struct LimitsArgs {
    /// Max POST body size, e.g. "300M" (engine parses the suffix).
    #[arg(long, env = "SIPI_MAXPOSTSIZE", value_name = "SIZE")]
    pub maxpost: Option<String>,
    /// Max output pixels (width × height) per IIIF request (0 = unlimited).
    #[arg(long, env = "SIPI_MAX_PIXEL_LIMIT", value_name = "PIXELS")]
    pub max_pixel_limit: Option<u64>,
    /// Max concurrent decode-memory budget, e.g. "2G" (0 = auto; engine parses
    /// the suffix).
    #[arg(long, env = "SIPI_MAX_DECODE_MEMORY", value_name = "SIZE")]
    pub max_decode_memory: Option<String>,
    /// Decode-memory mode: off, monitor, enforce (engine validates).
    #[arg(long, env = "SIPI_DECODE_MEMORY_MODE", value_name = "MODE")]
    pub decode_memory_mode: Option<String>,
    /// Thumbnail size used within Lua, e.g. "!128,128".
    #[arg(long, env = "SIPI_THUMBSIZE", value_name = "SIZE")]
    pub thumbsize: Option<String>,
}
