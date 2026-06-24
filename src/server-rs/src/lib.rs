//! SIPI Rust HTTP shell (strangler-fig rewrite; ADR-0013).
//!
//! This crate is the `sipi` **library**: it owns the axum + tokio server,
//! routing, the FFI wiring to the C++ image engine, config, and observability,
//! and it statically links the engine via `//src/ffi:sipi_ffi`. The default
//! `sipi` binary lives in the sibling `cli-rs` crate (`//src/cli-rs:sipi`): it
//! owns `main`, parses the CLI, and calls [`run`] for the `server` verb.
//! Shipping the server as a library is what lets SIPI be consumed as a
//! dependency (decision #9): a downstream crate can own `main`, depend on
//! `sipi`, and inject its own behaviour.
//!
//! The shell is built additively: it runs in parallel with the existing C++
//! server, which keeps the production socket until the cutover.

pub mod config;
pub mod ffi;
pub mod iiif;
pub mod info;
pub mod path;
pub mod routes;
pub mod sink;
pub mod telemetry;

pub use config::ServerOverrides;

use axum::response::{IntoResponse, Response};
use axum::{http::StatusCode, routing::get, Router};
use std::future::IntoFuture;
use std::net::SocketAddr;
use std::process::ExitCode;
use std::sync::{Arc, OnceLock};
use std::time::{Duration, Instant};

/// Default listen port for the additive Rust shell. The real port will come
/// from the SIPI config; until then it is overridable via `--serverport` or
/// `SIPI_RS_PORT` so the parallel shell never collides with the C++ server.
const DEFAULT_PORT: u16 = 1024;

/// Process start time, for the `/health` uptime field. Set once at server
/// startup; the handler reads `elapsed()`.
static START: OnceLock<Instant> = OnceLock::new();

/// Run the SIPI axum server. The `cli-rs` binary parses the `server` verb's
/// flags and calls this; a downstream crate can call it directly (decision #9).
/// `config` is the bootstrap Lua config path (it selects the base config the
/// `overrides` layer onto); `drain_timeout` is the Rust-owned graceful-drain
/// deadline in seconds (default 30). Blocks until shutdown; returns the process
/// exit code. Telemetry init lives in [`server_main`] (inside the runtime)
/// because the OTLP batch exporter needs a tokio runtime.
pub fn run(
    config: Option<String>,
    overrides: ServerOverrides,
    drain_timeout: Option<u64>,
) -> ExitCode {
    let rt = match tokio::runtime::Runtime::new() {
        Ok(rt) => rt,
        Err(e) => {
            eprintln!("failed to build tokio runtime: {e}");
            return ExitCode::FAILURE;
        }
    };

    rt.block_on(server_main(config, overrides, drain_timeout))
}

/// The async server lifecycle inside the tokio runtime: install telemetry, prove
/// the FFI seam, install the engine + Lua config, serve, then flush telemetry on
/// the way out.
async fn server_main(
    config: Option<String>,
    overrides: ServerOverrides,
    drain_timeout: Option<u64>,
) -> ExitCode {
    // Stamp the process start for /health uptime before anything else.
    let _ = START.set(Instant::now());

    // Telemetry first, so the startup logs + the FFI self-check are captured.
    // Fail-open: no OTEL_EXPORTER_OTLP_ENDPOINT → no exporter, so local dev needs
    // no collector. Inside the runtime because the OTLP batch exporter needs it.
    let otel = telemetry::init();

    // Prove the C++ engine links into this binary and the seam round-trips
    // before we accept traffic (cc_library → rust_binary under hermetic
    // LLVM/libc++). 404 is the expected status for a missing file.
    let status = ffi::link_self_check();
    tracing::info!(
        seam_status = status,
        "FFI link self-check: sipi_serve_file(bogus) → status"
    );
    debug_assert_eq!(
        status, 404,
        "FFI seam self-check should report 404 for a missing file"
    );

    // Install the engine + Lua config from the Lua config file before serving.
    // engine_context() hard-fails on any serve call until this runs, so without
    // --config only the engine-free routes (/health, /favicon.ico) work.
    match &config {
        Some(cfg) => match ffi::init(cfg, &overrides) {
            Ok(()) => tracing::info!(config = %cfg, "engine + Lua config installed"),
            Err(code) => {
                tracing::error!(config = %cfg, code, "sipi_init failed");
                flush_telemetry(otel).await;
                return ExitCode::FAILURE;
            }
        },
        None => tracing::warn!(
            "no --config: engine uninitialised; only /health and /favicon.ico will serve"
        ),
    }

    let drain_deadline = Duration::from_secs(drain_timeout.unwrap_or(30));
    let result = serve(overrides.serverport, drain_deadline).await;

    // Flush pending spans before the guard drops; the OTLP export is blocking
    // I/O, so do it off the async runtime.
    flush_telemetry(otel).await;

    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            tracing::error!(error = %e, "server terminated with error");
            ExitCode::FAILURE
        }
    }
}

/// Flush + shut down the OTel exporter off the async runtime — the flush
/// performs blocking I/O (the documented current-thread-shutdown deadlock is
/// avoided by running it on a blocking thread).
async fn flush_telemetry(otel: telemetry::Telemetry) {
    let _ = tokio::task::spawn_blocking(move || otel.shutdown()).await;
}

/// Build the axum application. `/health` + `/favicon.ico` are Rust-native and
/// never touch the FFI; every other path is the IIIF catch-all, which classifies,
/// validates, and drives the engine seam. The CORS, concurrency, and OTel
/// layers wrap this.
pub fn app(state: Arc<routes::AppState>) -> Router {
    use axum_tracing_opentelemetry::middleware::{OtelAxumLayer, OtelInResponseLayer};
    let mut router = Router::new()
        // The bare root has no `*rest` capture, so register it explicitly; the
        // handler classifies it (→ 400, matching the C++ parser). OPTIONS is the
        // CORS preflight (engine-independent). These routes are traced: a span
        // named by the route template (low cardinality), continuing
        // the W3C traceparent; OtelInResponseLayer echoes it into the response.
        .route(
            "/",
            get(routes::iiif)
                .head(routes::iiif)
                .options(routes::cors_preflight),
        )
        .route(
            "/{*rest}",
            get(routes::iiif)
                .head(routes::iiif)
                .options(routes::cors_preflight),
        );

    // Configured Lua routes (the script_handler analogue), registered before the
    // OTel layer so they are traced too. axum/matchit prioritises a static route
    // over the IIIF `/{*rest}` catch-all; a `<route>/{*rest}` variant reproduces
    // the C++ longest-prefix match (a route at /api/upload also serves subpaths).
    for entry in &state.routes {
        if let Some(method_router) = routes::lua_route_method_router(Arc::clone(&state), entry) {
            router = router.route(&entry.route, method_router.clone());
            if entry.route.as_str() != "/" {
                let prefix = format!("{}/{{*rest}}", entry.route.trim_end_matches('/'));
                router = router.route(&prefix, method_router);
            }
        }
    }

    router
        .layer(OtelInResponseLayer)
        .layer(OtelAxumLayer::default())
        // Registered after the layers so liveness / asset probes never enter the
        // trace pipeline — they also bypass the engine pool.
        .route("/health", get(health))
        .route("/favicon.ico", get(favicon))
        .with_state(state)
}

async fn serve(port: Option<u16>, drain_timeout: Duration) -> std::io::Result<()> {
    // Read the cached engine config once (image root + prefix_as_path); a
    // not-ready state (no --config) leaves the serve routes returning 503.
    let state = Arc::new(routes::AppState::load());
    let port = port
        .or_else(|| {
            std::env::var("SIPI_RS_PORT")
                .ok()
                .and_then(|p| p.parse().ok())
        })
        .unwrap_or(DEFAULT_PORT);
    let addr = SocketAddr::from(([0, 0, 0, 0], port));
    let listener = tokio::net::TcpListener::bind(addr).await?;
    tracing::info!(%addr, "SIPI Rust shell listening");

    // Graceful shutdown: a SIGTERM/Ctrl-C stops accepting new
    // connections and lets in-flight requests finish, bounded by drain_timeout —
    // past the deadline the remaining requests are abandoned and the process
    // exits. A watch latches the signal so axum's drain and the deadline both
    // observe it from the single signal listener (a notify could be missed).
    let (drain_tx, drain_rx) = tokio::sync::watch::channel(false);
    let graceful = {
        let mut rx = drain_rx;
        async move {
            let _ = rx.wait_for(|started| *started).await;
        }
    };
    // `with_graceful_shutdown` yields an `IntoFuture`, not a `Future`; resolve it
    // so the select arm can poll `&mut server`.
    let server = axum::serve(listener, app(state))
        .with_graceful_shutdown(graceful)
        .into_future();
    tokio::pin!(server);

    tokio::select! {
        res = &mut server => res,
        () = async {
            shutdown_signal().await;
            let _ = drain_tx.send(true);// begin draining in-flight requests
            tokio::time::sleep(drain_timeout).await;
        } => {
            tracing::warn!(
                drain_timeout_s = drain_timeout.as_secs(),
                "drain deadline exceeded; abandoning in-flight requests"
            );
            Ok(())
        }
    }
}

/// Resolve when the process is asked to stop: a Unix `SIGTERM` (the container
/// stop signal) or `Ctrl-C` (`SIGINT`). On non-Unix only `Ctrl-C` is wired.
async fn shutdown_signal() {
    let ctrl_c = async {
        let _ = tokio::signal::ctrl_c().await;
    };
    #[cfg(unix)]
    let terminate = async {
        match tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate()) {
            Ok(mut sig) => {
                sig.recv().await;
            }
            Err(e) => {
                tracing::error!(error = %e, "failed to install SIGTERM handler");
                std::future::pending::<()>().await;
            }
        }
    };
    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();

    tokio::select! {
        () = ctrl_c => {},
        () = terminate => {},
    }
    tracing::info!("shutdown signal received; draining");
}

/// Rust-native liveness probe — bypasses the engine entirely. Mirrors the C++
/// health_handler's body: `{status, version, uptime_seconds}` as JSON. The
/// version is the deployed release (`SIPI_SENTRY_RELEASE`, the same source as the
/// OTel `service.version`), falling back to the crate version in local runs.
async fn health() -> Response {
    let uptime = START.get().map_or(0, |s| s.elapsed().as_secs());
    let version = std::env::var("SIPI_SENTRY_RELEASE")
        .ok()
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| env!("CARGO_PKG_VERSION").to_owned());
    let body = serde_json::json!({ "status": "ok", "version": version, "uptime_seconds": uptime })
        .to_string();
    (
        [(axum::http::header::CONTENT_TYPE, "application/json")],
        body,
    )
        .into_response()
}

/// Rust-native favicon — 204 No Content (SIPI ships no favicon).
async fn favicon() -> StatusCode {
    StatusCode::NO_CONTENT
}

#[cfg(test)]
mod app_tests {
    use super::*;
    use axum::body::Body;
    use axum::http::Request;
    use tower::ServiceExt; // `oneshot`

    // No `sipi_init` runs in this test binary, so the engine is uninstalled and
    // AppState::load() reports not-ready. The engine-free routes still serve; the
    // serve routes 503. The full serve path (real images via the FFI) is covered
    // by the manual smoke run and the reqwest e2e suite targeting //src/cli-rs:sipi.
    fn test_app() -> Router {
        app(Arc::new(routes::AppState::load()))
    }

    async fn status_of(uri: &str) -> StatusCode {
        let resp = test_app()
            .oneshot(Request::get(uri).body(Body::empty()).unwrap())
            .await
            .unwrap();
        resp.status()
    }

    #[tokio::test]
    async fn health_is_rust_native() {
        assert_eq!(status_of("/health").await, StatusCode::OK);
    }

    #[tokio::test]
    async fn favicon_is_no_content() {
        assert_eq!(status_of("/favicon.ico").await, StatusCode::NO_CONTENT);
    }

    #[tokio::test]
    async fn serve_routes_503_without_engine() {
        // The catch-all guards on engine readiness before touching the seam.
        assert_eq!(
            status_of("/unit/lena512.jp2/info.json").await,
            StatusCode::SERVICE_UNAVAILABLE
        );
        assert_eq!(
            status_of("/unit/lena512.jp2/full/max/0/default.jpg").await,
            StatusCode::SERVICE_UNAVAILABLE
        );
    }
}
