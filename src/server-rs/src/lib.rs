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
use std::net::SocketAddr;
use std::process::ExitCode;

/// Default listen port for the additive Rust shell. The real port comes from
/// the SIPI config once `sipi_init` lands (T4); until then it is overridable via
/// `SIPI_RS_PORT` so the parallel shell never collides with the C++ server.
const DEFAULT_PORT: u16 = 1024;

/// Run the SIPI server. Blocks until shutdown. Returns the process exit code.
pub fn run() -> ExitCode {
    init_tracing();

    // Prove the C++ engine links into this binary and the seam round-trips
    // before we accept traffic (Open Question #1: cc_library → rust_binary under
    // hermetic LLVM/libc++). 404 is the expected status for a path that cannot
    // exist.
    let status = ffi::link_self_check();
    tracing::info!(seam_status = status, "FFI link self-check: sipi_serve_file(bogus) → status");
    debug_assert_eq!(status, 404, "FFI seam self-check should report 404 for a missing file");

    let rt = match tokio::runtime::Runtime::new() {
        Ok(rt) => rt,
        Err(e) => {
            tracing::error!(error = %e, "failed to build tokio runtime");
            return ExitCode::FAILURE;
        }
    };

    match rt.block_on(serve()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            tracing::error!(error = %e, "server terminated with error");
            ExitCode::FAILURE
        }
    }
}

/// Build the axum application. Routes grow across T5–T8; T1 ships the
/// Rust-native endpoints (`/health`, `/favicon.ico`) that never touch the FFI.
fn app() -> Router {
    Router::new()
        .route("/health", get(health))
        .route("/favicon.ico", get(favicon))
}

async fn serve() -> std::io::Result<()> {
    let port = std::env::var("SIPI_RS_PORT")
        .ok()
        .and_then(|p| p.parse().ok())
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
