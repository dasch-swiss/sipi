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

use std::os::raw::{c_char, c_int};

/// CLI/env flag overrides layered over the loaded Lua config.
#[derive(Debug, Default, Clone)]
pub struct ServerOverrides {
    /// HTTP listen port (`--serverport` / `SIPI_SERVERPORT`). `None` → fall back
    /// to `SIPI_RS_PORT` (dev/test) then the default port.
    pub serverport: Option<u16>,
}

/// The CLI/env override channel passed to `sipi_init` — hand-mirrored from
/// `sipi_ffi.h`'s `SipiServerConfig` (NOT bindgen; see the module docs). The
/// engine layers these over the Lua-parsed config before building its services.
///
/// Presence convention (matches the header):
/// - strings / the string array: a null pointer means "absent".
/// - scalars: a paired `has_*` flag (non-zero = present), because `0` is a valid
///   value for some (e.g. `cache_nfiles` `0` = unlimited).
///
/// Field order, widths, and `has_*` flags are lock-step with the header: the
/// [`layout`] test below and the C++ `static_assert`s in `sipi_ffi.h` pin the
/// layout against drift on either side. Constructed only via [`OverridesHolder`]
/// (a later slice), which owns the backing C strings; the fields are an FFI ABI
/// mirror, hence `#[allow(dead_code)]` until that wiring reads them.
#[repr(C)]
#[allow(dead_code)] // constructed/read once OverridesHolder lands (plan 02 §7.5 M4)
pub(crate) struct SipiServerConfig {
    // 8-byte: path / identity strings (null = absent)
    pub imgroot: *const c_char,
    pub scriptdir: *const c_char,
    pub initscript: *const c_char,
    pub tmpdir: *const c_char,
    pub jwtkey: *const c_char,
    pub adminuser: *const c_char,
    pub adminpasswd: *const c_char,
    pub cache_dir: *const c_char,
    pub cache_size: *const c_char, // raw "200M" — engine parses the suffix
    pub maxpost: *const c_char,    // raw "300M" — engine parses the suffix
    pub max_decode_memory: *const c_char, // raw — engine parses the suffix
    pub decode_memory_mode: *const c_char,
    pub rate_limit_mode: *const c_char,
    pub thumbsize: *const c_char,
    pub knorapath: *const c_char,
    pub knoraport: *const c_char,
    pub docroot: *const c_char,
    pub wwwroute: *const c_char,
    pub loglevel: *const c_char,
    // 8-byte: the subdir-exclude array + its length (null/0 = absent)
    pub subdirexcludes: *const *const c_char,
    pub subdirexcludes_len: usize,
    // 8-byte: 64-bit scalar values (presence via the has_ flags below)
    pub max_pixel_limit: u64,
    pub rate_limit_max_pixels: u64,
    pub rate_limit_pixel_threshold: u64,
    // 4-byte scalar values (presence via the has_ flags below)
    pub serverport: i32,
    pub maxtmpage: i32,
    pub cache_nfiles: i32, // signed: 0 = unlimited, negatives wrap — cli_app parity
    pub subdirlevels: i32,
    pub pathprefix: i32, // prefix_as_path, bool carried as 0/1
    pub rate_limit_window: u32,
    // 4-byte presence flags (non-zero = present)
    pub has_serverport: c_int,
    pub has_maxtmpage: c_int,
    pub has_cache_nfiles: c_int,
    pub has_subdirlevels: c_int,
    pub has_pathprefix: c_int,
    pub has_rate_limit_window: c_int,
    pub has_max_pixel_limit: c_int,
    pub has_rate_limit_max_pixels: c_int,
    pub has_rate_limit_pixel_threshold: c_int,
}

/// Lock-step layout guard — paired with the C++ `static_assert`/`offsetof`
/// checks in `src/ffi/sipi_ffi.h`. Any field reorder or width change on either
/// side breaks one of the two. LP64 on every supported target (darwin-aarch64,
/// linux-x86_64, linux-aarch64).
#[cfg(test)]
mod layout {
    use super::SipiServerConfig;
    use std::mem::{align_of, offset_of, size_of};

    #[test]
    fn repr_c_matches_sipi_ffi_h() {
        assert_eq!(size_of::<usize>(), 8, "layout assumes an LP64 target");
        assert_eq!(align_of::<SipiServerConfig>(), 8);
        assert_eq!(size_of::<SipiServerConfig>(), 256);

        assert_eq!(offset_of!(SipiServerConfig, imgroot), 0);
        assert_eq!(offset_of!(SipiServerConfig, scriptdir), 8);
        assert_eq!(offset_of!(SipiServerConfig, initscript), 16);
        assert_eq!(offset_of!(SipiServerConfig, tmpdir), 24);
        assert_eq!(offset_of!(SipiServerConfig, jwtkey), 32);
        assert_eq!(offset_of!(SipiServerConfig, adminuser), 40);
        assert_eq!(offset_of!(SipiServerConfig, adminpasswd), 48);
        assert_eq!(offset_of!(SipiServerConfig, cache_dir), 56);
        assert_eq!(offset_of!(SipiServerConfig, cache_size), 64);
        assert_eq!(offset_of!(SipiServerConfig, maxpost), 72);
        assert_eq!(offset_of!(SipiServerConfig, max_decode_memory), 80);
        assert_eq!(offset_of!(SipiServerConfig, decode_memory_mode), 88);
        assert_eq!(offset_of!(SipiServerConfig, rate_limit_mode), 96);
        assert_eq!(offset_of!(SipiServerConfig, thumbsize), 104);
        assert_eq!(offset_of!(SipiServerConfig, knorapath), 112);
        assert_eq!(offset_of!(SipiServerConfig, knoraport), 120);
        assert_eq!(offset_of!(SipiServerConfig, docroot), 128);
        assert_eq!(offset_of!(SipiServerConfig, wwwroute), 136);
        assert_eq!(offset_of!(SipiServerConfig, loglevel), 144);
        assert_eq!(offset_of!(SipiServerConfig, subdirexcludes), 152);
        assert_eq!(offset_of!(SipiServerConfig, subdirexcludes_len), 160);
        assert_eq!(offset_of!(SipiServerConfig, max_pixel_limit), 168);
        assert_eq!(offset_of!(SipiServerConfig, rate_limit_max_pixels), 176);
        assert_eq!(offset_of!(SipiServerConfig, rate_limit_pixel_threshold), 184);
        assert_eq!(offset_of!(SipiServerConfig, serverport), 192);
        assert_eq!(offset_of!(SipiServerConfig, maxtmpage), 196);
        assert_eq!(offset_of!(SipiServerConfig, cache_nfiles), 200);
        assert_eq!(offset_of!(SipiServerConfig, subdirlevels), 204);
        assert_eq!(offset_of!(SipiServerConfig, pathprefix), 208);
        assert_eq!(offset_of!(SipiServerConfig, rate_limit_window), 212);
        assert_eq!(offset_of!(SipiServerConfig, has_serverport), 216);
        assert_eq!(offset_of!(SipiServerConfig, has_maxtmpage), 220);
        assert_eq!(offset_of!(SipiServerConfig, has_cache_nfiles), 224);
        assert_eq!(offset_of!(SipiServerConfig, has_subdirlevels), 228);
        assert_eq!(offset_of!(SipiServerConfig, has_pathprefix), 232);
        assert_eq!(offset_of!(SipiServerConfig, has_rate_limit_window), 236);
        assert_eq!(offset_of!(SipiServerConfig, has_max_pixel_limit), 240);
        assert_eq!(offset_of!(SipiServerConfig, has_rate_limit_max_pixels), 244);
        assert_eq!(offset_of!(SipiServerConfig, has_rate_limit_pixel_threshold), 248);
    }
}
