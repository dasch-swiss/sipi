//! The `server` verb's argument set, assembled from per-domain flatten groups.
//!
//! [`ServerArgs`] is the clap `Parser`; each `#[derive(Args)]` group is
//! flattened in so `--help` renders sectioned and a reader finds every flag by
//! domain. Group structs are crate-internal — only `ServerArgs` is the public
//! surface (decision #9: the binary owns the CLI, the `sipi` library takes a
//! Rust-native `ServerOverrides`). The full ~42-flag surface lands across the
//! remaining groups in M3 (plan 02 §7.5); this step only homes the flags the
//! shell already parses.
//!
//! Two fields stay top-level rather than in a group:
//! - `config` is bootstrap — it *selects* the base Lua config the overrides
//!   layer onto; it is not itself an override.
//! - `drain_timeout` is a Rust-owned serve knob (the graceful-drain deadline),
//!   not a config override, so it is handed straight to `sipi::run`.

mod network;

pub use network::NetworkArgs;

use clap::Parser;

#[derive(Parser, Debug)]
#[command(name = "sipi server")]
pub struct ServerArgs {
    /// Path to the SIPI Lua config (installed by `sipi_init` before serving).
    #[arg(long)]
    pub config: Option<String>,

    #[command(flatten)]
    pub network: NetworkArgs,

    /// Graceful-drain deadline in seconds (default 30): on SIGTERM/Ctrl-C,
    /// in-flight requests get this long to finish before a forced shutdown.
    #[arg(long = "drain-timeout")]
    pub drain_timeout: Option<u64>,
}

#[cfg(test)]
mod tests {
    use super::ServerArgs;
    use clap::Parser;

    /// Every override-bearing field must parse to `None` when neither CLI nor
    /// env provides it, so it falls through to the loaded Lua config (plan 02
    /// §7.5). A `default_value` on any overridable field would silently clobber
    /// the config value — this pins against that regression as the group grows.
    #[test]
    fn bare_server_invocation_sets_no_overrides() {
        let args = ServerArgs::try_parse_from(["sipi server"]).unwrap();
        assert_eq!(args.config, None);
        assert_eq!(args.network.serverport, None);
        assert_eq!(args.network.sslport, None);
        assert_eq!(args.drain_timeout, None);
    }
}
