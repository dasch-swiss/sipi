//! Thin default entry point for the SIPI Rust shell (decision #9).
//!
//! `cli-rs` owns `main` and the verb dispatch; all server behaviour lives in the
//! `sipi` library (`//src/server-rs`). The `server` verb runs the axum shell;
//! `health` is a Rust-native loopback probe (no FFI); every other argv (offline
//! subcommands, `--version`, `--help`) is handed to the C++ CLI (`sipi_cli_main`)
//! verbatim. A downstream crate can replace this binary with its own `main`
//! while reusing the `sipi` library.

// Fast unsafe check (CI `lint` gate): every `unsafe {}` block must carry a
// `// SAFETY:` comment. `allow`-by-default (clippy `restriction` group), so it
// is enabled here explicitly; CI's `-Dwarnings` promotes it to a hard error.
#![warn(clippy::undocumented_unsafe_blocks)]

mod commands;
mod ffi;

use std::ffi::CString;
use std::os::raw::{c_char, c_int};
use std::process::ExitCode;

fn main() -> ExitCode {
    // The `rustls-no-provider` feature on `sentry` (below) pulls in TLS support
    // without forcing a crypto provider — install `ring` explicitly. `sentry`'s
    // plain `rustls` feature would otherwise resolve to reqwest's aws-lc-rs
    // path, and aws-lc-sys's cmake build script panics inside a Bazel action
    // (banned outright in MODULE.bazel); this repo's other rustls consumers
    // (tonic/hyper-rustls, for OTLP) already resolve against `ring`.
    rustls::crypto::ring::default_provider()
        .install_default()
        .expect("failed to install the rustls ring crypto provider");

    // `sentry_rust_minidump::init` re-spawns/reconnects via a
    // `--crash-reporter-server=<socket>` argv entry (hardcoded by the
    // `minidumper-child` crate it wraps — verified against the exact resolved
    // version, 0.3.0: `is_crash_reporter_process` checks
    // `env::args().any(|a| a.starts_with("--crash-reporter-server"))`, and the
    // parent spawns the reporter as `current_exe()` with *only* that one arg;
    // not configurable through this wrapper). Handle it here, first,
    // unconditionally, and return without ever reaching verb dispatch:
    // `minidumper-child`'s own "force exit so app code doesn't run after this"
    // contract only fires on ITS success path (a bind/setup failure returns an
    // `Err` instead) — without this early return, a reporter that fails to
    // start would fall through to `run_cli(&argv)` with the
    // `--crash-reporter-server=...` flag as its sole argument, which
    // `sipi_cli_main` doesn't recognize as any option or subcommand.
    if std::env::args().any(|a| a.starts_with("--crash-reporter-server")) {
        let _sentry_guard = init_sentry();
        if let Some(client) = sentry::Hub::current().client() {
            let _ = sentry_rust_minidump::init(&client);
        }
        return ExitCode::SUCCESS;
    }

    let argv: Vec<String> = std::env::args().collect();
    // The verb is the first non-flag token after argv[0].
    let verb_idx = argv
        .iter()
        .enumerate()
        .skip(1)
        .find(|(_, a)| !a.starts_with('-'))
        .map(|(i, _)| i);
    let verb = verb_idx.map(|idx| argv[idx].as_str());

    // Sentry client — panics + handled events, uniformly for every verb
    // (`server`, `health`, and the offline verbs behind `sipi_cli_main`) once
    // a DSN is configured (`None` otherwise — see `init_sentry`). Held for
    // the life of `main`: `ClientInitGuard`'s `Drop` blocks to flush any
    // pending events before the process exits.
    let _sentry_guard = init_sentry();

    // Native-crash (minidump) reporting — `server` only (DEV-6659). `sipi
    // health` runs on a short interval in prod and must never fork a fresh
    // reporter child of its own; CLI `convert` crash coverage is deliberately
    // out of scope here (it never reaches Rust code — see the module doc).
    // `Hub::current().client()` is `Some` only when `init_sentry` actually
    // called `sentry::init` (a valid DSN was configured) — a DSN-less run
    // never forks a reporter child or installs crash-signal handlers.
    let _minidump_guard = if verb == Some("server") {
        sentry::Hub::current()
            .client()
            .and_then(|client| sentry_rust_minidump::init(&client).ok())
    } else {
        None
    };

    match verb_idx {
        // `server` → the Rust shell. Pass the slice from the verb onward; clap
        // treats argv[idx] ("server"/"health") as the binary name and skips it.
        Some(idx) if argv[idx] == "server" => commands::server::run(&argv[idx..]),
        // `health` → the Rust-native loopback probe (no FFI, no engine).
        Some(idx) if argv[idx] == "health" => commands::health::run(&argv[idx..]),
        // Everything else → the C++ CLI, verbatim.
        _ => run_cli(&argv),
    }
}

/// `SIPI_SENTRY_DSN` empty/unset/unparseable ⇒ `None`, and `sentry::init` is
/// never called at all — mirrors the C++ oracle's `init_sentry` early-return
/// on an empty DSN, but goes one step further: constructing a disabled client
/// still exercises `sentry`'s init-time machinery (integrations, its
/// `reqwest`-backed transport factory) for no observable benefit, since a
/// disabled client's panic hook already just chains through to the previous
/// (default) hook. `release`/`environment` share their source with the OTel
/// resource attributes (`sipi::telemetry`), so both observability backends
/// agree. `traces_sample_rate: 0.0` — Sentry owns crashes/panics/errors here,
/// OTLP owns traces; a nonzero rate would duplicate transactions across both
/// backends.
fn init_sentry() -> Option<sentry::ClientInitGuard> {
    let dsn = std::env::var("SIPI_SENTRY_DSN")
        .ok()
        .filter(|s| !s.is_empty())
        .and_then(|raw| match raw.parse() {
            Ok(dsn) => Some(dsn),
            Err(e) => {
                eprintln!("sipi: SIPI_SENTRY_DSN is not a valid Sentry DSN ({e}); Sentry disabled");
                None
            }
        })?;
    Some(sentry::init(sentry::ClientOptions {
        dsn: Some(dsn),
        release: sipi::telemetry::service_version().map(Into::into),
        environment: Some(sipi::telemetry::deployment_environment().into()),
        attach_stacktrace: true,
        traces_sample_rate: 0.0,
        ..Default::default()
    }))
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
            eprintln!("sipi: argument contains an interior NUL byte");
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
