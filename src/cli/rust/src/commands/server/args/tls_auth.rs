//! TLS + auth flags (the "TLS & Auth" `--help` heading).
//!
//! `sslcert` / `sslkey` are parse-only: TLS terminates at Traefik and the shell
//! serves plain HTTP.

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "TLS & Auth")]
pub struct TlsAuthArgs {
    /// Path to the SSL certificate (parse-only: TLS at Traefik).
    #[arg(long, env = "SIPI_SSLCERTIFICATE", value_name = "FILE")]
    pub sslcert: Option<String>,
    /// Path to the SSL key (parse-only: TLS at Traefik).
    #[arg(long, env = "SIPI_SSLKEY", value_name = "FILE")]
    pub sslkey: Option<String>,
    /// Secret for generating JWTs (required for any Lua-configured deployment;
    /// at least 32 bytes; the shipped default is rejected).
    #[arg(long, env = "SIPI_JWTKEY", value_name = "SECRET")]
    pub jwtkey: Option<String>,
}
