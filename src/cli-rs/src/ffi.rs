//! The one C-ABI symbol `cli-rs` binds directly: `sipi_cli_main`.
//!
//! `cli-rs` does NOT route this through the `sipi` library — the two FFI
//! surfaces are disjoint (the server seam lives in `//src/server-rs`), so each
//! crate declares the symbols it calls. The symbol is provided by the
//! `//src/cli:cli_app` `cc_library` at link time.

use std::os::raw::{c_char, c_int};

extern "C" {
    /// Hands argv verbatim to the C++ CLI11 parser (`//src/cli:cli_app`) and
    /// returns the process exit code — no `exit()`/`abort()` from inside the
    /// FFI, so the Rust caller owns teardown. Drives the offline subcommands
    /// (`convert`/`verify`/`query`/`compare`) the shell does not own, plus
    /// top-level `--version`/`--help`. (`server` and `health` are Rust-native
    /// verbs — they never reach this.)
    pub fn sipi_cli_main(argc: c_int, argv: *mut *mut c_char) -> c_int;
}
