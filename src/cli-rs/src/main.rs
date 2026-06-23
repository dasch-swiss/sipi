//! Thin default entry point for the SIPI Rust shell (decision #9).
//!
//! `cli-rs` owns `main` and the verb dispatch; all server behaviour lives in the
//! `sipi` library (`//src/server-rs`). The `server` verb runs the axum shell;
//! `health` is a Rust-native loopback probe (no FFI); every other argv (offline
//! subcommands, `--version`, `--help`) is handed to the C++ CLI (`sipi_cli_main`)
//! verbatim. A downstream crate can replace this binary with its own `main`
//! while reusing the `sipi` library.

mod commands;
mod ffi;

use std::ffi::CString;
use std::os::raw::{c_char, c_int};
use std::process::ExitCode;

fn main() -> ExitCode {
    let argv: Vec<String> = std::env::args().collect();
    // The verb is the first non-flag token after argv[0].
    let verb_idx = argv
        .iter()
        .enumerate()
        .skip(1)
        .find(|(_, a)| !a.starts_with('-'))
        .map(|(i, _)| i);

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
