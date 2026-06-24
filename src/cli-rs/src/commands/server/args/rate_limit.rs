//! Rate-limiting flags (the "Rate limiting" `--help` heading).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Rate limiting")]
pub struct RateLimitArgs {
    /// Max output pixels per client per window (0 = disabled).
    #[arg(long, env = "SIPI_RATE_LIMIT_MAX_PIXELS", value_name = "PIXELS")]
    pub rate_limit_max_pixels: Option<u64>,
    /// Sliding window in seconds.
    #[arg(long, env = "SIPI_RATE_LIMIT_WINDOW", value_name = "SECS")]
    pub rate_limit_window: Option<u32>,
    /// Rate-limit mode: off, monitor, enforce (engine validates).
    #[arg(long, env = "SIPI_RATE_LIMIT_MODE", value_name = "MODE")]
    pub rate_limit_mode: Option<String>,
    /// Requests below this pixel count are free.
    #[arg(long, env = "SIPI_RATE_LIMIT_PIXEL_THRESHOLD", value_name = "PIXELS")]
    pub rate_limit_pixel_threshold: Option<u64>,
}
