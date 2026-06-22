//! Thin default entry point for the SIPI Rust shell (decision #9).
//!
//! All behaviour lives in the `sipi` library so a downstream crate can own
//! `main`, depend on `sipi`, and inject its own behaviour. `sipi::run` does the
//! thin `clap` dispatch: the `server` verb runs the axum shell, every other
//! argv is handed to the C++ `sipi_cli_main`.

fn main() -> std::process::ExitCode {
    sipi::run()
}
