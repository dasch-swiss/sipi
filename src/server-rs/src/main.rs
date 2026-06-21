//! Thin default entry point for the SIPI Rust shell (decision #9).
//!
//! All behaviour lives in the `sipi` library so a downstream crate can own
//! `main`, depend on `sipi`, and inject its own behaviour. The thin `clap`
//! subcommand split (`server` → `sipi::run`, everything else → the C++
//! `sipi_cli_main`) lands in T3; today this binary just runs the server.

fn main() -> std::process::ExitCode {
    sipi::run()
}
