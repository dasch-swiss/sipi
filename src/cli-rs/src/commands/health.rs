//! The `health` verb — a Rust-native loopback liveness probe (no FFI, no engine).
//!
//! Mirrors the C++ `cmd_health` (`src/cli/commands/health.cpp`): probe
//! `http://127.0.0.1:<port>/health` and exit 0 iff the response is `200`. This
//! is the container HEALTHCHECK (`sipi health --port 1024`), so it must never
//! hang — hence the bounded connect + read timeouts. The response body is
//! never read; only the status line matters.

use clap::Parser;
use std::io::{Read, Write};
use std::net::{SocketAddr, TcpStream};
use std::process::ExitCode;
use std::time::Duration;

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

    if probe(args.port) {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

/// True iff `GET /health` on the local server returns `200`. A non-200 status (a
/// draining server's 503), a refused connection, or a timeout all mean
/// "unhealthy" → false. Bounded by a 1s connect timeout and a 2s read timeout
/// so the probe never hangs (mirrors `cmd_health`'s 1s-connect/2s-read).
///
/// A raw TCP request, not an HTTP client crate: the target is always a
/// literal loopback IP, never a hostname needing resolution, but `ureq`'s
/// resolver spawns a background thread to bound DNS lookup whenever any
/// timeout is configured (`ureq::unversioned::resolver::resolve_async`) —
/// under ASan, dropping that thread's `JoinHandle` trips a sanitizer-runtime
/// `CHECK failed` in `sanitizer_thread_arg_retval.cpp`. A plain socket read
/// of the status line avoids spawning that thread at all.
fn probe(port: u16) -> bool {
    let addr: SocketAddr = match format!("127.0.0.1:{port}").parse() {
        Ok(a) => a,
        Err(_) => return false,
    };
    let Ok(mut stream) = TcpStream::connect_timeout(&addr, Duration::from_secs(1)) else {
        return false;
    };
    if stream
        .set_read_timeout(Some(Duration::from_secs(2)))
        .is_err()
    {
        return false;
    }
    if stream
        .write_all(b"GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
        .is_err()
    {
        return false;
    }
    // Read until the status line's terminating CRLF shows up (a single read()
    // isn't guaranteed to return the whole line), bounded by the read
    // timeout above on each call and a small size cap overall.
    let mut buf = Vec::with_capacity(64);
    let mut chunk = [0u8; 64];
    loop {
        match stream.read(&mut chunk) {
            Ok(0) => break,
            Ok(n) => {
                buf.extend_from_slice(&chunk[..n]);
                if buf.windows(2).any(|w| w == b"\r\n") || buf.len() > 512 {
                    break;
                }
            }
            Err(_) => break,
        }
    }
    // Status line is "HTTP/1.1 200 ...\r\n" — a plain substring check is
    // enough; the body is never read or needed.
    std::str::from_utf8(&buf)
        .map(|s| s.contains(" 200 "))
        .unwrap_or(false)
}
