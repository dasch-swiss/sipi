//! The `server` verb: parse the flags and run the `sipi` library's axum shell.
//!
//! Only the flags the shell consumes today are declared. The full ~42-flag CLI
//! surface + the CLI→config override wiring land in M2–M4 (plan 02 §7.5); until
//! then an unrecognised flag is a clap usage error (exit 2). The flag set
//! matches what the e2e harness passes (`server --config … --serverport …
//! --sslport … --drain-timeout …`).

use clap::Parser;
use sipi::ServerOverrides;
use std::process::ExitCode;

#[derive(Parser, Debug)]
#[command(name = "sipi server")]
struct ServerArgs {
    /// Path to the SIPI Lua config (installed by `sipi_init` before serving).
    #[arg(long)]
    config: Option<String>,
    /// HTTP listen port.
    #[arg(long)]
    serverport: Option<u16>,
    /// TLS port (accepted for harness/CLI parity; SIPI serves plain HTTP behind
    /// Traefik, so this is unused).
    #[arg(long)]
    sslport: Option<u16>,
    /// Graceful-drain deadline in seconds (default 30): on SIGTERM/Ctrl-C,
    /// in-flight requests get this long to finish before a forced shutdown.
    #[arg(long = "drain-timeout")]
    drain_timeout: Option<u64>,
}

/// Parse the `server` flags (argv from the "server" token onward) and run the
/// axum server. Blocks until shutdown; returns the process exit code.
pub fn run(server_argv: &[String]) -> ExitCode {
    let args = match ServerArgs::try_parse_from(server_argv) {
        Ok(args) => args,
        Err(e) => {
            // clap renders the usage/error; exit code 2 is the conventional
            // usage-error code (we never call clap's process-exiting `.exit()`).
            let _ = e.print();
            return ExitCode::from(2);
        }
    };

    // `sslport` is parsed for CLI/harness parity but unused (TLS terminates at
    // Traefik). `--drain-timeout` is a Rust-owned serve knob, not a config
    // override, so it stays a direct argument; `serverport` is the one override
    // the shell consumes today.
    sipi::run(
        args.config,
        ServerOverrides { serverport: args.serverport },
        args.drain_timeout,
    )
}
