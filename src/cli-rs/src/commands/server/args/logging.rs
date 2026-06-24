//! Logging flags (the "Logging" `--help` heading).
//!
//! On the Rust shell, log routing is `RUST_LOG`/EnvFilter-driven (plan 02 §5
//! #2). `loglevel` still forwards to the engine's own logger config; `logfile`
//! parses for oracle parity but is not forwarded (NYI in the engine).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Logging")]
pub struct LoggingArgs {
    /// Logfile name (NYI in the engine).
    #[arg(long, env = "SIPI_LOGFILE", value_name = "NAME")]
    pub logfile: Option<String>,
    /// Logging level: DEBUG, INFO, WARNING, ERR, CRIT, ALERT, EMERG.
    #[arg(long, env = "SIPI_LOGLEVEL", value_name = "LEVEL")]
    pub loglevel: Option<String>,
}
