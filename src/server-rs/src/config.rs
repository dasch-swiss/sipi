//! CLI/env override surface for the `server` verb.
//!
//! [`ServerOverrides`] is the Rust-native bag of `server`-verb flags that layer
//! over the loaded Lua config. `cli-rs` builds it from the parsed clap args and
//! hands it to [`crate::run`]; the library never parses argv itself (decision #9
//! — the library is reusable, the binary owns the CLI).
//!
//! Today it carries only the listen port — the one flag the shell consumes. It
//! grows one `Option` per forwarded flag as the full CLI-override path lands
//! (plan 02 §7.5, M3/M4), at which point it is converted to the `#[repr(C)]`
//! `SipiServerConfig` and forwarded through `sipi_init`. When that `#[repr(C)]`
//! struct is added here, its layout must match `sipi_ffi.h`'s `SipiServerConfig`
//! exactly; guard it with a `size_of`/offset test against the header (not
//! bindgen) and pair it with the C++ `static_assert` — the two definitions are
//! lock-step. `--drain-timeout` is
//! deliberately *not* here: it is a Rust-owned serve knob, not a config
//! override, so it stays a direct [`crate::run`] argument.

/// CLI/env flag overrides layered over the loaded Lua config.
#[derive(Debug, Default, Clone)]
pub struct ServerOverrides {
    /// HTTP listen port (`--serverport` / `SIPI_SERVERPORT`). `None` → fall back
    /// to `SIPI_RS_PORT` (dev/test) then the default port.
    pub serverport: Option<u16>,
}
