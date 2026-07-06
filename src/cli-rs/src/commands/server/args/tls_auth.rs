//! TLS + auth flags (the "TLS & Auth" `--help` heading).
//!
//! `sslcert` / `sslkey` are parse-only: TLS terminates at Traefik and the shell
//! serves plain HTTP (plan 02 §5 #3). `--adminuser` binds the correct
//! `SIPI_ADMINUSER` env var; the C++ oracle binds the misspelled
//! `SIPI_ADMIINUSER` (`cli_app.cpp:1822`) — a latent typo nobody can
//! intentionally rely on. The documented typo-divergence is pinned by
//! `test/e2e/tests/differential.rs::adminuser_env_name_documented_divergence`
//! (plan 02 §7.5 M6): a parse-level `--help` grep, since no behavioural probe
//! exists.

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
    /// Secret for generating JWTs (exactly 42 characters).
    #[arg(long, env = "SIPI_JWTKEY", value_name = "SECRET")]
    pub jwtkey: Option<String>,
    /// SIPI admin username.
    #[arg(long, env = "SIPI_ADMINUSER", value_name = "USER")]
    pub adminuser: Option<String>,
    /// Admin password.
    #[arg(long, env = "SIPI_ADMINPASSWD", value_name = "PASSWD")]
    pub adminpasswd: Option<String>,
}
