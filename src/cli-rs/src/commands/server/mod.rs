//! The `server` verb: parse the flags and run the `sipi` library's axum shell.
//!
//! The clap surface lives in [`args`] (assembled from per-domain flatten
//! groups); this module owns the verb handler and the
//! `From<&ServerArgs> for ServerOverrides` mapping — the binary knows the CLI
//! shape, the `sipi` library takes the Rust-native overrides bag.
//!
//! Every forwarded `server` flag maps into `ServerOverrides`; the override
//! channel into the engine (the `repr(C)` struct + the `sipi_init` apply block)
//! lives in `server-rs/config.rs`.

mod args;

use args::ServerArgs;
use clap::Parser;
use sipi::ServerOverrides;
use std::process::ExitCode;

impl From<&ServerArgs> for ServerOverrides {
    fn from(args: &ServerArgs) -> Self {
        // Engine-behaviour flags forward into this overrides bag; flags the shell
        // does not act on (sslport/sslcert/sslkey, keepalive, hostname, logfile)
        // are accepted but not forwarded. `--nthreads`, `--max-waiting`, and
        // `--queue-timeout` are handled separately: Rust-owned serve knobs handed
        // straight to `sipi::run` below (like `--drain-timeout`), not layered onto
        // the engine config. The
        // deprecated
        // cache aliases (--cachedir/--cachesize/--cachenfiles) collapse onto
        // their canonical field here — canonical wins if both are somehow set
        // (matches the C++ oracle's last-write-wins on the shared `optCache*`
        // variable closely enough: neither binary's precedence between the two
        // spellings is a contract anyone can rely on).
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
            cache_dir: args
                .cache
                .cache_dir
                .clone()
                .or_else(|| args.cache.cachedir.clone()),
            cache_size: args
                .cache
                .cache_size
                .clone()
                .or_else(|| args.cache.cachesize.clone()),
            cache_nfiles: args.cache.cache_nfiles.or(args.cache.cachenfiles),
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
            // jpeg_quality + scaling_quality are TOML-config-only (no CLI flag —
            // the oracle has none either), so the clap path never sets them.
            jpeg_quality: None,
            scaling_quality: Default::default(),
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

    // `--drain-timeout` and the concurrency knobs (`--nthreads`, `--max-waiting`,
    // `--queue-timeout`, `--preflight-cache-ttl`, `--preflight-cache-slots`) are
    // Rust-owned serve knobs, not config overrides, so they are handed straight to
    // `sipi::run` rather than layered onto the engine config.
    let overrides = ServerOverrides::from(&args);
    sipi::run(
        args.config,
        overrides,
        args.drain_timeout,
        args.concurrency.nthreads,
        args.concurrency.max_waiting,
        args.concurrency.queue_timeout,
        args.concurrency.preflight_cache_ttl,
        args.concurrency.preflight_cache_slots,
    )
}
