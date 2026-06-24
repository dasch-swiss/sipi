//! The `server` verb: parse the flags and run the `sipi` library's axum shell.
//!
//! The clap surface lives in [`args`] (assembled from per-domain flatten
//! groups); this module owns the verb handler and the
//! `From<&ServerArgs> for ServerOverrides` mapping — the binary knows the CLI
//! shape, the `sipi` library takes the Rust-native overrides bag (decision #9).
//!
//! Only the listen port is wired into `ServerOverrides` today; the full
//! CLI→config override channel lands in M3–M4 (plan 02 §7.5). Until then an
//! unrecognised flag is a clap usage error (exit 2).

mod args;

use args::ServerArgs;
use clap::Parser;
use sipi::ServerOverrides;
use std::process::ExitCode;

impl From<&ServerArgs> for ServerOverrides {
    fn from(args: &ServerArgs) -> Self {
        // `sslport` parses for CLI/harness parity but is inert (TLS at Traefik),
        // so it is not an override. `serverport` is the one override the shell
        // consumes today.
        ServerOverrides {
            serverport: args.network.serverport,
        }
    }
}

/// Parse the `server` flags (argv from the "server" token onward) and run the
/// axum server. Blocks until shutdown; returns the process exit code.
pub fn run(server_argv: &[String]) -> ExitCode {
    let args = match ServerArgs::try_parse_from(server_argv) {
        Ok(args) => args,
        Err(e) => {
            // clap renders help/version to stdout and usage errors to stderr;
            // mirror its own exit codes — 0 for `--help`/`--version`, 2 for a
            // usage error — instead of forcing 2 (the C++ oracle exits 0 on
            // `server --help`). We never call clap's process-exiting `.exit()`.
            let _ = e.print();
            return ExitCode::from(e.exit_code() as u8);
        }
    };

    // `--drain-timeout` is a Rust-owned serve knob, not a config override, so it
    // is handed straight to `sipi::run`.
    let overrides = ServerOverrides::from(&args);
    sipi::run(args.config, overrides, args.drain_timeout)
}
