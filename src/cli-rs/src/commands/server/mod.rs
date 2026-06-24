//! The `server` verb: parse the flags and run the `sipi` library's axum shell.
//!
//! The clap surface lives in [`args`] (assembled from per-domain flatten
//! groups); this module owns the verb handler and the
//! `From<&ServerArgs> for ServerOverrides` mapping — the binary knows the CLI
//! shape, the `sipi` library takes the Rust-native overrides bag (decision #9).
//!
//! Every forwarded `server` flag maps into `ServerOverrides`; the override
//! channel into the engine (the `repr(C)` struct + the `sipi_init` apply block)
//! lands in the remaining M4 slices (plan 02 §7.5).

mod args;

use args::ServerArgs;
use clap::Parser;
use sipi::ServerOverrides;
use std::process::ExitCode;

impl From<&ServerArgs> for ServerOverrides {
    fn from(args: &ServerArgs) -> Self {
        // Engine-behaviour flags forward; transport flags the Rust shell owns
        // (sslport/sslcert/sslkey, keepalive, max-waiting/queue-timeout,
        // hostname, nthreads, logfile) parse for CLI parity but are never
        // forwarded. The deprecated cache aliases (--cachedir/--cachesize/
        // --cachenfiles) land in M5; this maps the canonical names only.
        ServerOverrides {
            serverport: args.network.serverport,
            imgroot: args.paths.imgroot.clone(),
            scriptdir: args.paths.scriptdir.clone(),
            initscript: args.paths.initscript.clone(),
            tmpdir: args.paths.tmpdir.clone(),
            maxtmpage: args.paths.maxtmpage,
            docroot: args.paths.docroot.clone(),
            wwwroute: args.paths.wwwroute.clone(),
            pathprefix: args.paths.pathprefix,
            subdirlevels: args.paths.subdirlevels,
            subdirexcludes: args.paths.subdirexcludes.clone(),
            jwtkey: args.tls_auth.jwtkey.clone(),
            adminuser: args.tls_auth.adminuser.clone(),
            adminpasswd: args.tls_auth.adminpasswd.clone(),
            cache_dir: args.cache.cache_dir.clone(),
            cache_size: args.cache.cache_size.clone(),
            cache_nfiles: args.cache.cache_nfiles,
            rate_limit_max_pixels: args.rate_limit.rate_limit_max_pixels,
            rate_limit_window: args.rate_limit.rate_limit_window,
            rate_limit_mode: args.rate_limit.rate_limit_mode.clone(),
            rate_limit_pixel_threshold: args.rate_limit.rate_limit_pixel_threshold,
            max_decode_memory: args.limits.max_decode_memory.clone(),
            decode_memory_mode: args.limits.decode_memory_mode.clone(),
            max_pixel_limit: args.limits.max_pixel_limit,
            maxpost: args.limits.maxpost.clone(),
            thumbsize: args.limits.thumbsize.clone(),
            knorapath: args.knora.knorapath.clone(),
            knoraport: args.knora.knoraport.clone(),
            loglevel: args.logging.loglevel.clone(),
        }
    }
}

/// Parse the `server` flags (argv from the "server" token onward) and run the
/// axum server. Blocks until shutdown; returns the process exit code.
pub fn run(server_argv: &[String]) -> ExitCode {
    let args = match ServerArgs::try_parse_from(server_argv) {
        Ok(args) => args,
        Err(e) => {
            // clap renders help/version to stdout and usage errors to stderr;
            // mirror its own exit codes — 0 for `--help`/`--version`, 2 for a
            // usage error — instead of forcing 2 (the C++ oracle exits 0 on
            // `server --help`). We never call clap's process-exiting `.exit()`.
            let _ = e.print();
            return ExitCode::from(e.exit_code() as u8);
        }
    };

    // `--drain-timeout` is a Rust-owned serve knob, not a config override, so it
    // is handed straight to `sipi::run`.
    let overrides = ServerOverrides::from(&args);
    sipi::run(args.config, overrides, args.drain_timeout)
}
