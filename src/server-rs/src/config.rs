//! CLI/env override surface for the `server` verb.
//!
//! [`ServerOverrides`] is the Rust-native bag of `server`-verb flags that layer
//! over the loaded Lua config. `cli-rs` builds it from the parsed clap args and
//! hands it to [`crate::run`]; the library never parses argv itself (decision #9
//! — the library is reusable, the binary owns the CLI).
//!
//! It carries one `Option` per forwarded `server` flag; `None` means the flag
//! was set by neither CLI nor env, so the loaded Lua config value wins (the
//! Rust analog of the C++ `user_set` gate; precedence `config < env < CLI`).
//! [`OverridesHolder`] converts it to the `#[repr(C)]` [`SipiServerConfig`] and
//! forwards it through `sipi_init`, which layers the present overrides onto the
//! parsed Lua config before the engine builds its services. The `#[repr(C)]`
//! layout is lock-step with `sipi_ffi.h`'s `SipiServerConfig` — guarded by the
//! `layout` test below paired with the header's `static_assert`s (not bindgen).
//!
//! `--drain-timeout` is deliberately *not* here: it is a Rust-owned serve knob,
//! not a config override, so it stays a direct [`crate::run`] argument.

use std::ffi::CString;
use std::os::raw::{c_char, c_int};

/// CLI/env flag overrides layered over the loaded Lua config — one `Option` per
/// forwarded `server` flag (`None` = neither CLI nor env set it → the Lua config
/// wins).
///
/// Only engine-behaviour flags are forwarded. Transport flags the Rust shell
/// owns (`--sslport`/`--sslcert`/`--sslkey`, `--keepalive`,
/// `--max-waiting`/`--queue-timeout`, `--hostname`) parse for CLI parity but are
/// never forwarded, so they are absent here.
#[derive(Debug, Default, Clone)]
pub struct ServerOverrides {
    /// HTTP listen port (`--serverport` / `SIPI_SERVERPORT`). `None` → fall back
    /// to `SIPI_RS_PORT` (dev/test) then the default port.
    pub serverport: Option<u16>,

    // Paths
    pub imgroot: Option<String>,
    pub scriptdir: Option<String>,
    pub initscript: Option<String>,
    pub tmpdir: Option<String>,
    pub maxtmpage: Option<i32>,
    pub docroot: Option<String>,
    pub wwwroute: Option<String>,
    pub pathprefix: Option<bool>,
    pub subdirlevels: Option<i32>,
    pub subdirexcludes: Option<Vec<String>>,

    // Auth (TLS terminates at Traefik; only the auth knobs forward)
    pub jwtkey: Option<String>,
    pub adminuser: Option<String>,
    pub adminpasswd: Option<String>,

    // Cache
    pub cache_dir: Option<String>,
    /// Raw size string ("200M"); the engine parses the suffix.
    pub cache_size: Option<String>,
    /// 0 = unlimited; a negative is rejected at the CLI (clap `u32` + the C++
    /// `unsigned` var), so there is no signed→unsigned wrap.
    pub cache_nfiles: Option<u32>,

    // Rate limiting
    pub rate_limit_max_pixels: Option<u64>,
    pub rate_limit_window: Option<u32>,
    pub rate_limit_mode: Option<String>,
    pub rate_limit_pixel_threshold: Option<u64>,

    // Limits
    /// Raw size string; the engine parses the suffix.
    pub max_decode_memory: Option<String>,
    pub decode_memory_mode: Option<String>,
    pub max_pixel_limit: Option<u64>,
    /// Raw size string ("300M"); the engine parses the suffix.
    pub maxpost: Option<String>,
    pub thumbsize: Option<String>,

    // Knora
    pub knorapath: Option<String>,
    pub knoraport: Option<String>,

    // Logging
    pub loglevel: Option<String>,
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
// `OverridesHolder` writes every field and C reads them across the seam; they
// are never read from Rust, so `dead_code` cannot see the use.
#[allow(dead_code)]
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
    pub cache_nfiles: u32, // 0 = unlimited; a negative is rejected at the CLI (no wrap)
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

/// Owns the C storage backing a [`SipiServerConfig`] so its pointers stay valid
/// across the synchronous `sipi_init` call (seam contract: caller-owned inputs
/// outlive the call). Built from a [`ServerOverrides`]; the engine deep-copies
/// every present value during `sipi_init`, so the holder can drop right after.
///
/// `cfg`'s pointers reference heap buffers owned by `_strings` / `_subdir_*`. A
/// `CString`'s buffer and a `Vec`'s buffer keep a stable address when the owning
/// struct moves, so the holder itself is safe to move; only [`Self::as_ptr`]'s
/// result is move-sensitive (it borrows `self.cfg`), and it is consumed inline
/// by the immediately-following `sipi_init` call.
pub(crate) struct OverridesHolder {
    _strings: Vec<CString>,
    _subdir_strings: Vec<CString>,
    _subdir_ptrs: Vec<*const c_char>,
    cfg: SipiServerConfig,
}

impl OverridesHolder {
    pub fn new(o: &ServerOverrides) -> Self {
        let mut strings: Vec<CString> = Vec::new();
        let mut subdir_strings: Vec<CString> = Vec::new();
        let mut subdir_ptrs: Vec<*const c_char> = Vec::new();

        let (subdirexcludes, subdirexcludes_len) = match &o.subdirexcludes {
            Some(list) if !list.is_empty() => {
                for s in list {
                    let c = CString::new(s.as_str()).expect("argv/env strings are NUL-free");
                    subdir_ptrs.push(c.as_ptr());
                    subdir_strings.push(c);
                }
                // Taken after the loop (no further pushes): the heap buffer the
                // pointer addresses survives the move of `subdir_ptrs` into `self`.
                (subdir_ptrs.as_ptr(), subdir_ptrs.len())
            }
            _ => (std::ptr::null(), 0),
        };

        let cfg = SipiServerConfig {
            imgroot: intern_cstr(&mut strings, &o.imgroot),
            scriptdir: intern_cstr(&mut strings, &o.scriptdir),
            initscript: intern_cstr(&mut strings, &o.initscript),
            tmpdir: intern_cstr(&mut strings, &o.tmpdir),
            jwtkey: intern_cstr(&mut strings, &o.jwtkey),
            adminuser: intern_cstr(&mut strings, &o.adminuser),
            adminpasswd: intern_cstr(&mut strings, &o.adminpasswd),
            cache_dir: intern_cstr(&mut strings, &o.cache_dir),
            cache_size: intern_cstr(&mut strings, &o.cache_size),
            maxpost: intern_cstr(&mut strings, &o.maxpost),
            max_decode_memory: intern_cstr(&mut strings, &o.max_decode_memory),
            decode_memory_mode: intern_cstr(&mut strings, &o.decode_memory_mode),
            rate_limit_mode: intern_cstr(&mut strings, &o.rate_limit_mode),
            thumbsize: intern_cstr(&mut strings, &o.thumbsize),
            knorapath: intern_cstr(&mut strings, &o.knorapath),
            knoraport: intern_cstr(&mut strings, &o.knoraport),
            docroot: intern_cstr(&mut strings, &o.docroot),
            wwwroute: intern_cstr(&mut strings, &o.wwwroute),
            loglevel: intern_cstr(&mut strings, &o.loglevel),
            subdirexcludes,
            subdirexcludes_len,
            max_pixel_limit: o.max_pixel_limit.unwrap_or(0),
            rate_limit_max_pixels: o.rate_limit_max_pixels.unwrap_or(0),
            rate_limit_pixel_threshold: o.rate_limit_pixel_threshold.unwrap_or(0),
            serverport: o.serverport.map(i32::from).unwrap_or(0),
            maxtmpage: o.maxtmpage.unwrap_or(0),
            cache_nfiles: o.cache_nfiles.unwrap_or(0),
            subdirlevels: o.subdirlevels.unwrap_or(0),
            pathprefix: o.pathprefix.map(i32::from).unwrap_or(0),
            rate_limit_window: o.rate_limit_window.unwrap_or(0),
            has_serverport: o.serverport.is_some() as c_int,
            has_maxtmpage: o.maxtmpage.is_some() as c_int,
            has_cache_nfiles: o.cache_nfiles.is_some() as c_int,
            has_subdirlevels: o.subdirlevels.is_some() as c_int,
            has_pathprefix: o.pathprefix.is_some() as c_int,
            has_rate_limit_window: o.rate_limit_window.is_some() as c_int,
            has_max_pixel_limit: o.max_pixel_limit.is_some() as c_int,
            has_rate_limit_max_pixels: o.rate_limit_max_pixels.is_some() as c_int,
            has_rate_limit_pixel_threshold: o.rate_limit_pixel_threshold.is_some() as c_int,
        };

        Self {
            _strings: strings,
            _subdir_strings: subdir_strings,
            _subdir_ptrs: subdir_ptrs,
            cfg,
        }
    }

    /// Pointer to the `SipiServerConfig` for `sipi_init`. Valid only while `self`
    /// is alive and unmoved — call it inline at the `sipi_init` call site.
    pub fn as_ptr(&self) -> *const SipiServerConfig {
        &self.cfg
    }
}

/// Push `s` (when present) into `strings` as a `CString` and return a pointer to
/// its buffer, or null when absent. The buffer keeps a stable address even if
/// `strings` later reallocates (the `CString` allocation does not move when the
/// `Vec`'s element slots do).
fn intern_cstr(strings: &mut Vec<CString>, s: &Option<String>) -> *const c_char {
    match s {
        Some(v) => {
            let c = CString::new(v.as_str()).expect("argv/env strings are NUL-free");
            let p = c.as_ptr();
            strings.push(c);
            p
        }
        None => std::ptr::null(),
    }
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
        assert_eq!(
            offset_of!(SipiServerConfig, rate_limit_pixel_threshold),
            184
        );
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
        assert_eq!(
            offset_of!(SipiServerConfig, has_rate_limit_pixel_threshold),
            248
        );
    }
}
