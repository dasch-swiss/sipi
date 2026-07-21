//! Network listener flags (the "Network" `--help` heading).
//!
//! `sslport`, `hostname`, and `keepalive` are parse-only on the Rust shell —
//! they reach the engine on no path post-cutover (TLS terminates at Traefik,
//! tokio is async so the keep-alive knob is unread, and the external host is
//! derived from `X-Forwarded-Host`). They parse for CLI/oracle parity but are
//! not forwarded into `ServerOverrides` (the forward/parse-only split).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Network")]
pub struct NetworkArgs {
    /// HTTP listen port (1–65535).
    #[arg(long, env = "SIPI_SERVERPORT", value_name = "PORT", value_parser = clap::value_parser!(u16).range(1..=65535))]
    pub serverport: Option<u16>,
    /// TLS port 1–65535 (parse-only: SIPI serves plain HTTP behind Traefik).
    #[arg(long, env = "SIPI_SSLPORT", value_name = "PORT", value_parser = clap::value_parser!(u16).range(1..=65535))]
    pub sslport: Option<u16>,
    /// Server hostname (parse-only: the shell derives the external host from
    /// `X-Forwarded-Host`).
    #[arg(long, env = "SIPI_HOSTNAME", value_name = "HOST")]
    pub hostname: Option<String>,
    /// HTTP/1.1 keep-alive timeout in seconds (parse-only: tokio is async, so
    /// the knob is unread).
    #[arg(long, env = "SIPI_KEEPALIVE", value_name = "SECS")]
    pub keepalive: Option<i32>,
}
