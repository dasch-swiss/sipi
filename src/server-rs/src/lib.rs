//! SIPI Rust HTTP shell (strangler-fig Phase C; ADR-0013).
//!
//! This crate is the `sipi` **library**: it owns the axum + tokio server,
//! routing, the FFI wiring to the C++ image engine, config, and observability,
//! and it statically links the engine via `//src/ffi:sipi_ffi`. The default
//! `sipi_server` binary (`main.rs`) is a thin entry point that calls
//! [`run`]. Shipping as lib + thin bin is what lets SIPI be consumed as a
//! dependency (decision #9): a downstream crate can own `main`, depend on
//! `sipi`, and inject its own behaviour.
//!
//! Phase C is built additively — this shell runs in parallel with the existing
//! C++ server, which keeps the production socket until the step-5 cutover.

pub mod ffi;

use axum::{http::StatusCode, routing::get, Router};
use clap::Parser;
use std::ffi::CString;
use std::net::SocketAddr;
use std::os::raw::{c_char, c_int};
use std::process::ExitCode;

/// Default listen port for the additive Rust shell. The real port comes from
/// the SIPI config once `sipi_init` lands (T4); until then it is overridable via
/// `--serverport` or `SIPI_RS_PORT` so the parallel shell never collides with
/// the C++ server.
const DEFAULT_PORT: u16 = 1024;

/// Server-mode flags. `server` is the only subcommand the Rust shell owns;
/// every other argv is handed to the C++ CLI (`sipi_cli_main`) verbatim. Only
/// the flags the shell consumes today are declared; `--config` is parsed and
/// will be threaded into `sipi_init` (T4). The flag set matches what the e2e
/// harness passes (`server --config … --serverport … --sslport … --drain-timeout …`).
#[derive(Parser, Debug)]
#[command(name = "sipi server")]
struct ServerArgs {
    /// Path to the SIPI Lua config (consumed by sipi_init — T4).
    #[arg(long)]
    config: Option<String>,
    /// HTTP listen port.
    #[arg(long)]
    serverport: Option<u16>,
    /// TLS port (accepted for harness/CLI parity; SIPI serves plain HTTP behind
    /// Traefik, so this is unused — DEV-6035).
    #[arg(long)]
    sslport: Option<u16>,
    /// Graceful-drain timeout in seconds (wired in T9).
    #[arg(long = "drain-timeout")]
    drain_timeout: Option<u64>,
}

/// Run SIPI. Rust owns `main`: the `server` verb runs the axum shell; every
/// other argv (offline subcommands, `--version`, `--help`) is handed to the C++
/// CLI via [`ffi::sipi_cli_main`]. Returns the process exit code.
pub fn run() -> ExitCode {
    init_tracing();

    let argv: Vec<String> = std::env::args().collect();
    // The verb is the first non-flag token after argv[0].
    let verb_idx = argv
        .iter()
        .enumerate()
        .skip(1)
        .find(|(_, a)| !a.starts_with('-'))
        .map(|(i, _)| i);

    match verb_idx {
        // `server` → the Rust shell. Pass the slice from "server" onward; clap
        // treats argv[idx] ("server") as the binary name and skips it.
        Some(idx) if argv[idx] == "server" => run_server(&argv[idx..]),
        // Everything else → the C++ CLI, verbatim.
        _ => run_cli(&argv),
    }
}

/// Parse the `server` flags and run the axum server. Blocks until shutdown.
fn run_server(server_argv: &[String]) -> ExitCode {
    let args = match ServerArgs::try_parse_from(server_argv) {
        Ok(args) => args,
        Err(e) => {
            // clap renders the usage/error; exit code 2 is the conventional
            // usage-error code (we never call clap's process-exiting `.exit()`).
            let _ = e.print();
            return ExitCode::from(2);
        }
    };

    // Prove the C++ engine links into this binary and the seam round-trips
    // before we accept traffic (Open Question #1: cc_library → rust_binary under
    // hermetic LLVM/libc++). 404 is the expected status for a missing file.
    let status = ffi::link_self_check();
    tracing::info!(seam_status = status, "FFI link self-check: sipi_serve_file(bogus) → status");
    debug_assert_eq!(status, 404, "FFI seam self-check should report 404 for a missing file");

    // Install the engine + Lua config from the Lua config file before serving.
    // engine_context() hard-fails on any serve call until this runs, so without
    // --config only the engine-free routes (/health, /favicon.ico) work.
    match &args.config {
        Some(cfg) => match ffi::init(cfg) {
            Ok(()) => tracing::info!(config = %cfg, "engine + Lua config installed"),
            Err(code) => {
                tracing::error!(config = %cfg, code, "sipi_init failed");
                return ExitCode::FAILURE;
            }
        },
        None => tracing::warn!("no --config: engine uninitialised; only /health and /favicon.ico will serve"),
    }

    let rt = match tokio::runtime::Runtime::new() {
        Ok(rt) => rt,
        Err(e) => {
            tracing::error!(error = %e, "failed to build tokio runtime");
            return ExitCode::FAILURE;
        }
    };

    match rt.block_on(serve(args.serverport)) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            tracing::error!(error = %e, "server terminated with error");
            ExitCode::FAILURE
        }
    }
}

/// Hand the full argv to the C++ CLI (`sipi_cli_main`) and return its exit code.
fn run_cli(argv: &[String]) -> ExitCode {
    // Marshal argv into a C `char**`. `sipi_cli_main` is synchronous and does
    // not retain the pointers, so the CStrings only need to outlive the call.
    let c_args: Vec<CString> = match argv
        .iter()
        .map(|a| CString::new(a.as_str()))
        .collect::<Result<_, _>>()
    {
        Ok(v) => v,
        Err(_) => {
            tracing::error!("argument contains an interior NUL byte");
            return ExitCode::FAILURE;
        }
    };
    let mut c_ptrs: Vec<*mut c_char> = c_args.iter().map(|c| c.as_ptr() as *mut c_char).collect();

    // SAFETY: `c_ptrs` is a valid argv-shaped array of `argc` NUL-terminated
    // strings that outlive the synchronous call; the seam guarantees no C++
    // exception unwinds across the boundary (it returns a status code).
    let code = unsafe { ffi::sipi_cli_main(c_ptrs.len() as c_int, c_ptrs.as_mut_ptr()) };
    // Process exit codes are a single byte; CLI11/command codes (0/1/105/106)
    // all fit.
    ExitCode::from(code as u8)
}

/// Build the axum application. Routes grow across T5–T8; T1 ships the
/// Rust-native endpoints (`/health`, `/favicon.ico`) that never touch the FFI.
fn app() -> Router {
    Router::new()
        .route("/health", get(health))
        .route("/favicon.ico", get(favicon))
}

async fn serve(port: Option<u16>) -> std::io::Result<()> {
    let port = port
        .or_else(|| std::env::var("SIPI_RS_PORT").ok().and_then(|p| p.parse().ok()))
        .unwrap_or(DEFAULT_PORT);
    let addr = SocketAddr::from(([0, 0, 0, 0], port));
    let listener = tokio::net::TcpListener::bind(addr).await?;
    tracing::info!(%addr, "SIPI Rust shell listening");
    axum::serve(listener, app()).await
}

/// Rust-native liveness probe — bypasses the engine entirely (DEV-6101).
async fn health() -> &'static str {
    "OK\n"
}

/// Rust-native favicon — 204 No Content (SIPI ships no favicon).
async fn favicon() -> StatusCode {
    StatusCode::NO_CONTENT
}

/// Structured logging. Honours `RUST_LOG`; defaults to `info`. JSON formatting
/// and OTLP export land in T10.
fn init_tracing() {
    use tracing_subscriber::{fmt, EnvFilter};
    let filter = EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info"));
    // `try_init` so a second call (e.g. in tests) is a no-op rather than a panic.
    let _ = fmt().with_env_filter(filter).try_init();
}
