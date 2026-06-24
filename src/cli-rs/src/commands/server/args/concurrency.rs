//! Concurrency flags (the "Concurrency" `--help` heading).
//!
//! `max_waiting` and `queue_timeout` are parse-only: the Rust shell uses a
//! semaphore concurrency model (shed-load → 503) rather than the C++
//! thread-per-connection socket queue, so the queue knobs are unread
//! (plan 02 §5 #4 / §7.5 forward/parse-only split).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Concurrency")]
pub struct ConcurrencyArgs {
    /// Worker thread count (0 = auto-detect from CPU cores). Also accepts the
    /// C++ oracle's `-t` short form.
    #[arg(long, short = 't', env = "SIPI_NTHREADS", value_name = "N")]
    pub nthreads: Option<u32>,
    /// Max waiting connections before 503 (parse-only: semaphore model).
    #[arg(long, env = "SIPI_MAX_WAITING", value_name = "N")]
    pub max_waiting: Option<u64>,
    /// Max seconds a request waits in queue before 503 (parse-only).
    #[arg(long, env = "SIPI_QUEUE_TIMEOUT", value_name = "SECS")]
    pub queue_timeout: Option<u32>,
}
