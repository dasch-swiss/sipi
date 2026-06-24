//! The `server` verb's argument set, assembled from per-domain flatten groups.
//!
//! [`ServerArgs`] is the clap `Parser`; each `#[derive(Args)]` group is
//! flattened in so `--help` renders sectioned and a reader finds every flag by
//! domain. Group structs are crate-internal — only `ServerArgs` is the public
//! surface (decision #9: the binary owns the CLI, the `sipi` library takes a
//! Rust-native `ServerOverrides`).
//!
//! The full ~40-flag server surface parses here (so e.g. `server --imgroot X`
//! no longer exits 2), with each overridable flag an `Option<T>` carrying its
//! `SIPI_*` env var — NO `default_value`, so an unset flag stays `None` and
//! falls through to the loaded Lua config (the Rust analog of the C++
//! `user_set` gate; precedence `config < env < CLI`, plan 02 §7.5). The flags
//! are **unwired**: only `serverport` is forwarded into `ServerOverrides`
//! today; the override channel into the engine (the `repr(C)` struct + the
//! `sipi_init` apply block) lands in M4.
//!
//! Two fields stay top-level rather than in a group:
//! - `config` is bootstrap — it *selects* the base Lua config the overrides
//!   layer onto; it is not itself an override.
//! - `drain_timeout` is a Rust-owned serve knob (the graceful-drain deadline),
//!   not a config override, so it is handed straight to `sipi::run`.

mod cache;
mod concurrency;
mod knora;
mod limits;
mod logging;
mod network;
mod paths;
mod rate_limit;
mod tls_auth;

pub use cache::CacheArgs;
pub use concurrency::ConcurrencyArgs;
pub use knora::KnoraArgs;
pub use limits::LimitsArgs;
pub use logging::LoggingArgs;
pub use network::NetworkArgs;
pub use paths::PathArgs;
pub use rate_limit::RateLimitArgs;
pub use tls_auth::TlsAuthArgs;

use clap::Parser;

#[derive(Parser, Debug)]
#[command(name = "sipi server")]
pub struct ServerArgs {
    /// Path to the SIPI Lua config (installed by `sipi_init` before serving).
    /// Also accepts the C++ oracle's `-c` short form.
    #[arg(long, short = 'c', env = "SIPI_CONFIGFILE", value_name = "FILE")]
    pub config: Option<String>,

    #[command(flatten)]
    pub network: NetworkArgs,
    #[command(flatten)]
    pub concurrency: ConcurrencyArgs,
    #[command(flatten)]
    pub limits: LimitsArgs,
    #[command(flatten)]
    pub paths: PathArgs,
    #[command(flatten)]
    pub cache: CacheArgs,
    #[command(flatten)]
    pub rate_limit: RateLimitArgs,
    #[command(flatten)]
    pub tls_auth: TlsAuthArgs,
    #[command(flatten)]
    pub knora: KnoraArgs,
    #[command(flatten)]
    pub logging: LoggingArgs,

    /// Graceful-drain deadline in seconds (default 30): on SIGTERM/Ctrl-C,
    /// in-flight requests get this long to finish before a forced shutdown.
    #[arg(long, env = "SIPI_DRAIN_TIMEOUT", value_name = "SECS")]
    pub drain_timeout: Option<u64>,
}

#[cfg(test)]
mod tests {
    use super::ServerArgs;
    use clap::Parser;

    /// Every override-bearing field must parse to `None` when neither CLI nor
    /// env provides it, so it falls through to the loaded Lua config (plan 02
    /// §7.5). A `default_value` on any overridable field would silently clobber
    /// the config value — this pins one representative field per group against
    /// that regression, plus the two non-trivial parse configs (`pathprefix`'s
    /// optional-value flag and `subdirexcludes`'s delimited list), where the
    /// `default_value`-vs-`default_missing_value` distinction is easiest to get
    /// wrong. The argv starts at `"server"` because that is what
    /// `commands::server::run` hands `try_parse_from` after the verb dispatch.
    #[test]
    fn bare_server_invocation_sets_no_overrides() {
        let args = ServerArgs::try_parse_from(["server"]).unwrap();
        assert_eq!(args.config, None);
        assert_eq!(args.drain_timeout, None);
        assert_eq!(args.network.serverport, None);
        assert_eq!(args.network.sslport, None);
        assert_eq!(args.concurrency.nthreads, None);
        assert_eq!(args.limits.maxpost, None);
        assert_eq!(args.limits.decode_memory_mode, None);
        assert_eq!(args.paths.imgroot, None);
        assert_eq!(args.paths.pathprefix, None);
        assert_eq!(args.paths.subdirexcludes, None);
        assert_eq!(args.cache.cache_dir, None);
        assert_eq!(args.rate_limit.rate_limit_mode, None);
        assert_eq!(args.tls_auth.jwtkey, None);
        assert_eq!(args.knora.knorapath, None);
        assert_eq!(args.logging.loglevel, None);
    }

    /// The flags route into their groups and parse to the expected types — a
    /// canary that the flatten wiring and the long-flag names hold across all
    /// nine groups, that the top-level `config`/`--nthreads` `-c`/`-t` short
    /// forms mirror the oracle, and that `--pathprefix` (an optional-value flag)
    /// resolves to `Some(true)` when given without a value.
    #[test]
    fn server_flags_parse_into_their_groups() {
        let args = ServerArgs::try_parse_from([
            "server",
            "-c",
            "/etc/sipi.lua",
            "--serverport",
            "1024",
            "-t",
            "8",
            "--maxpost",
            "300M",
            "--imgroot",
            "/img",
            "--max-pixel-limit",
            "1000",
            "--cache-dir",
            "/c",
            "--rate-limit-mode",
            "enforce",
            "--jwtkey",
            "secret",
            "--knoraport",
            "3434",
            "--loglevel",
            "INFO",
            // An optional-value flag, placed last so no following token can be
            // read as its value: it must resolve to `Some(true)`.
            "--pathprefix",
        ])
        .unwrap();
        assert_eq!(args.config.as_deref(), Some("/etc/sipi.lua"));
        assert_eq!(args.network.serverport, Some(1024));
        assert_eq!(args.concurrency.nthreads, Some(8));
        assert_eq!(args.limits.maxpost.as_deref(), Some("300M"));
        assert_eq!(args.limits.max_pixel_limit, Some(1000));
        assert_eq!(args.paths.imgroot.as_deref(), Some("/img"));
        assert_eq!(args.cache.cache_dir.as_deref(), Some("/c"));
        assert_eq!(args.rate_limit.rate_limit_mode.as_deref(), Some("enforce"));
        assert_eq!(args.tls_auth.jwtkey.as_deref(), Some("secret"));
        assert_eq!(args.knora.knoraport.as_deref(), Some("3434"));
        assert_eq!(args.logging.loglevel.as_deref(), Some("INFO"));
        assert_eq!(args.paths.pathprefix, Some(true));
    }
}
