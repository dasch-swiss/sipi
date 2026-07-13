//! The `health` verb — a Rust-native loopback liveness probe (no FFI, no engine).
//!
//! Mirrors the C++ `cmd_health` (`src/cli/commands/health.cpp`): probe
//! `http://127.0.0.1:<port>/health` and exit 0 iff the response is `200`. This
//! is the container HEALTHCHECK (`sipi health --port 1024`), so it must never
//! hang — hence the bounded connect + end-to-end timeouts. The response body is
//! never read; only the status matters.

use clap::Parser;
use std::process::ExitCode;
use std::time::Duration;
use ureq::Agent;

#[derive(Parser, Debug)]
#[command(name = "sipi health")]
struct HealthArgs {
    /// Port of the local SIPI server to probe (matches the C++ `cmd_health`
    /// default).
    #[arg(long, default_value_t = 1024)]
    port: u16,
}

/// Parse the `health` flags (argv from the "health" token onward) and probe the
/// local server. Exit 0 if `/health` returns HTTP 200, else 1 (or 2 on a bad
/// flag).
pub fn run(health_argv: &[String]) -> ExitCode {
    let args = match HealthArgs::try_parse_from(health_argv) {
        Ok(a) => a,
        Err(e) => {
            let _ = e.print();
            return ExitCode::from(2);
        }
    };

    let healthy = probe(args.port);

    // Under ASan (this binary statically links the sanitizer runtime via the
    // C++ engine, so its pthread interceptors apply process-wide), tearing
    // down `ureq`'s connection machinery at process exit trips the same
    // "Joining already joined thread" false-positive abort documented in
    // `sipi::server_main` (server-rs/src/lib.rs) and `SipiIOJ2k.cpp`'s Kakadu
    // fix — only on the healthy path, which does a full HTTP round-trip
    // (the no-server path fails instantly on connection-refused, before
    // `ureq` sets up anything to join). `_exit` skips that teardown
    // entirely; `probe` has already produced its result, so nothing is lost.
    // `ASAN_OPTIONS` is set only by the asan-instrumented test/CI config,
    // never in dev or production.
    if std::env::var_os("ASAN_OPTIONS").is_some() {
        // SAFETY: `_exit` is async-signal-safe and always valid to call.
        unsafe { libc::_exit(i32::from(!healthy)) };
    }

    if healthy {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

/// True iff `GET /health` on the local server returns `200`. A non-200 status (a
/// draining server's 503), a refused connection, or a timeout all mean
/// "unhealthy" → false. Bounded by a 1s connect timeout and a 3s end-to-end
/// ceiling so the probe never hangs (mirrors `cmd_health`'s 1s-connect/2s-read).
fn probe(port: u16) -> bool {
    let agent: Agent = Agent::config_builder()
        .timeout_connect(Some(Duration::from_secs(1)))
        .timeout_global(Some(Duration::from_secs(3)))
        // Read the status ourselves — a 503 from a draining server is
        // "unhealthy", not a transport error worth surfacing.
        .http_status_as_error(false)
        .build()
        .into();

    let url = format!("http://127.0.0.1:{port}/health");
    match agent.get(&url).call() {
        Ok(resp) => resp.status().as_u16() == 200,
        Err(_) => false,
    }
}
