//! Network listener flags for the `server` verb (the "Network" `--help`
//! section).

use clap::Args;

/// `--serverport` / `--sslport`.
///
/// `sslport` is parsed for CLI/harness parity but inert: TLS terminates at
/// Traefik and the shell serves plain HTTP (plan 02 §5 #3).
#[derive(Args, Debug)]
#[command(next_help_heading = "Network")]
pub struct NetworkArgs {
    /// HTTP listen port.
    #[arg(long)]
    pub serverport: Option<u16>,
    /// TLS port (accepted for CLI/harness parity; SIPI serves plain HTTP behind
    /// Traefik, so this is inert).
    #[arg(long)]
    pub sslport: Option<u16>,
}
