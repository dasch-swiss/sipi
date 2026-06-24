//! Network listener flags (the "Network" `--help` heading).
//!
//! `sslport`, `hostname`, and `keepalive` are parse-only on the Rust shell —
//! they reach the engine on no path post-cutover (TLS terminates at Traefik,
//! tokio is async so the keep-alive knob is unread, and the external host is
//! derived from `X-Forwarded-Host`). They parse for CLI/oracle parity but are
//! not forwarded into `ServerOverrides` (plan 02 §7.5 forward/parse-only split).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Network")]
pub struct NetworkArgs {
    /// HTTP listen port.
    #[arg(long, env = "SIPI_SERVERPORT", value_name = "PORT")]
    pub serverport: Option<u16>,
    /// TLS port (parse-only: SIPI serves plain HTTP behind Traefik — §5 #3).
    #[arg(long, env = "SIPI_SSLPORT", value_name = "PORT")]
    pub sslport: Option<u16>,
    /// Server hostname (parse-only: the shell derives the external host from
    /// `X-Forwarded-Host`).
    #[arg(long, env = "SIPI_HOSTNAME", value_name = "HOST")]
    pub hostname: Option<String>,
    /// HTTP/1.1 keep-alive timeout in seconds (parse-only: tokio is async, so
    /// the knob is unread — §3 P2).
    #[arg(long, env = "SIPI_KEEPALIVE", value_name = "SECS")]
    pub keepalive: Option<i32>,
}
