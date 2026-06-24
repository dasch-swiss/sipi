//! Filesystem path + path-resolution flags (the "Paths" `--help` heading).
//!
//! `pathprefix` and `subdirlevels` are deprecated and have no effect on the
//! Rust serve path (plan 02 §3 P3); they parse for oracle parity. `pathprefix`
//! is flag-shaped on the C++ side (a bare `--pathprefix` means "true"), so it
//! takes an optional value here (`--pathprefix` → true, `--pathprefix=false` →
//! false, absent → fall through to the config).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Paths")]
pub struct PathArgs {
    /// Root directory containing the images.
    #[arg(long, env = "SIPI_IMGROOT", value_name = "DIR")]
    pub imgroot: Option<String>,
    /// Document root for the static fileserver.
    #[arg(long, env = "SIPI_DOCROOT", value_name = "DIR")]
    pub docroot: Option<String>,
    /// URL route for the static fileserver.
    #[arg(long, env = "SIPI_WWWROUTE", value_name = "ROUTE")]
    pub wwwroute: Option<String>,
    /// Directory containing the Lua route scripts.
    #[arg(long, env = "SIPI_SCRIPTDIR", value_name = "DIR")]
    pub scriptdir: Option<String>,
    /// Temporary directory (uploads etc.).
    #[arg(long, env = "SIPI_TMPDIR", value_name = "DIR")]
    pub tmpdir: Option<String>,
    /// Max age in seconds of temp files before deletion.
    #[arg(long, env = "SIPI_MAXTMPAGE", value_name = "SECS")]
    pub maxtmpage: Option<i32>,
    /// Path to the Lua init script.
    #[arg(long, env = "SIPI_INITSCRIPT", value_name = "FILE")]
    pub initscript: Option<String>,
    /// IIIF prefix is part of the image path (deprecated; no effect on the Rust
    /// path — §3 P3).
    #[arg(
        long,
        env = "SIPI_PATHPREFIX",
        num_args = 0..=1,
        default_missing_value = "true",
        value_name = "BOOL"
    )]
    pub pathprefix: Option<bool>,
    /// Number of subdir levels (deprecated; no effect on the Rust path).
    #[arg(long, env = "SIPI_SUBDIRLEVELS", value_name = "N")]
    pub subdirlevels: Option<i32>,
    /// Directories excluded from subdir calculations.
    #[arg(long, env = "SIPI_SUBDIREXCLUDES", value_name = "DIR", value_delimiter = ',')]
    pub subdirexcludes: Option<Vec<String>>,
}
