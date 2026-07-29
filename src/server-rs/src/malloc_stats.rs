//! Allocator introspection for the OTLP metrics bridge.
//!
//! Container RSS alone cannot distinguish "the process is using this memory"
//! from "the process freed it but the allocator retained it" — the difference
//! between a leak and allocator retention, which decide two entirely different
//! fixes. [`stats`] reads the process allocator's own accounting so
//! [`crate::metrics`] can export both sides as gauges.
//!
//! This library does not choose the process allocator — the final-link binary
//! does (`//src/cli-rs:sipi` links mimalloc on Linux; a downstream crate with
//! its own `main` may choose differently). The binary registers its
//! allocator's reader via [`set_source`]; without one, [`stats`] falls back to
//! glibc `mallinfo2(3)`, which is correct exactly when no override allocator
//! is linked. (The fallback cannot self-detect an override: non-libc symbols
//! like `mi_stats_get` are not exported to the dynamic table, so `dlsym`
//! cannot probe for them.)
//!
//! `mallinfo2` is resolved at runtime via `dlsym` rather than linked directly:
//! the hermetic toolchain links Linux targets against generated glibc stubs
//! whose version floor (~2.28) predates the symbol (glibc 2.33), so a direct
//! extern reference fails at link time even though every deployment target
//! (Debian 12 base image, glibc 2.36) provides it at runtime. On a runtime
//! glibc without the symbol, [`stats`] returns `None` and the gauges observe
//! nothing.
//!
//! Non-glibc builds (macOS dev hosts) compile the `None` fallback below.

use std::sync::OnceLock;

/// One allocator reading, reduced to the fields that answer the
/// leak-vs-retention question. Field docs give the glibc `mallinfo2` and
/// mimalloc `mi_stats_get` mappings.
#[derive(Clone, Copy, Debug)]
pub struct MallocStats {
    /// Bytes handed out by `malloc` and not yet freed — what the process is
    /// actually using. Grows without bound only under a true leak.
    /// glibc: `uordblks`; mimalloc: `malloc_normal + malloc_huge` current.
    pub in_use_bytes: i64,
    /// Freed bytes the allocator retains instead of returning to the OS —
    /// allocator retention. This is the series that explains an RSS ratchet
    /// with no leak. glibc: `fordblks`; mimalloc: `committed − in_use`.
    pub retained_bytes: i64,
    /// Bytes in allocations served directly by `mmap`, outside the regular
    /// heap structures. glibc: `hblkhd`; mimalloc: `malloc_huge` current.
    pub mmap_bytes: i64,
    /// Bytes the allocator holds from the OS that back RSS. glibc: `arena`
    /// (sbrk'd heap; `arena + mmap` ≈ the allocator's share of RSS);
    /// mimalloc: `committed` current.
    pub arena_bytes: i64,
}

/// The reader registered by the final-link binary for its chosen allocator.
static SOURCE: OnceLock<fn() -> Option<MallocStats>> = OnceLock::new();

/// Register the allocator-stats reader for the process allocator the binary
/// linked (call once at startup, before the server runs). Later calls are
/// ignored.
pub fn set_source(source: fn() -> Option<MallocStats>) {
    let _ = SOURCE.set(source);
}

#[cfg(all(target_os = "linux", target_env = "gnu"))]
mod imp {
    use super::MallocStats;
    use std::ffi::c_void;
    use std::sync::OnceLock;

    /// `struct mallinfo2` from `<malloc.h>` (glibc ≥ 2.33): ten `size_t`
    /// fields. Only `arena`, `hblkhd`, `uordblks`, and `fordblks` are read;
    /// the rest exist to keep the ABI layout exact.
    #[repr(C)]
    #[derive(Clone, Copy)]
    struct Mallinfo2 {
        arena: usize,
        ordblks: usize,
        smblks: usize,
        hblks: usize,
        hblkhd: usize,
        usmblks: usize,
        fsmblks: usize,
        uordblks: usize,
        fordblks: usize,
        keepcost: usize,
    }

    type Mallinfo2Fn = unsafe extern "C" fn() -> Mallinfo2;

    /// Resolve `mallinfo2` once from the already-loaded glibc. `None` on a
    /// runtime glibc older than 2.33.
    fn mallinfo2() -> Option<Mallinfo2Fn> {
        static SYM: OnceLock<Option<Mallinfo2Fn>> = OnceLock::new();
        *SYM.get_or_init(|| {
            // SAFETY: RTLD_DEFAULT is a valid pseudo-handle and the name is a
            // NUL-terminated literal; dlsym only performs a lookup.
            let addr = unsafe { libc::dlsym(libc::RTLD_DEFAULT, c"mallinfo2".as_ptr()) };
            if addr.is_null() {
                None
            } else {
                // SAFETY: a non-null dlsym result for "mallinfo2" is glibc's
                // `struct mallinfo2 mallinfo2(void)`, matching `Mallinfo2Fn`'s
                // ABI (mirrored `Mallinfo2` layout, no arguments).
                Some(unsafe { std::mem::transmute::<*mut c_void, Mallinfo2Fn>(addr) })
            }
        })
    }

    pub(super) fn stats() -> Option<MallocStats> {
        // SAFETY: the pointer was resolved from the live glibc by `mallinfo2()`
        // above; calling it has no preconditions (it takes no arguments and
        // returns the struct by value).
        let info = unsafe { mallinfo2()?() };
        // Saturate rather than wrap: the gauges are i64 and a >8 EiB reading
        // is a glibc bug, not a value worth preserving exactly.
        let clamp = |v: usize| i64::try_from(v).unwrap_or(i64::MAX);
        Some(MallocStats {
            in_use_bytes: clamp(info.uordblks),
            retained_bytes: clamp(info.fordblks),
            mmap_bytes: clamp(info.hblkhd),
            arena_bytes: clamp(info.arena),
        })
    }
}

#[cfg(not(all(target_os = "linux", target_env = "gnu")))]
mod imp {
    pub(super) fn stats() -> Option<super::MallocStats> {
        None
    }
}

/// Read the allocator's current state: the registered [`set_source`] reader
/// when the binary installed one, else the glibc `mallinfo2` fallback; `None`
/// when neither applies (non-glibc build with no source, or glibc < 2.33).
pub(crate) fn stats() -> Option<MallocStats> {
    match SOURCE.get() {
        Some(source) => source(),
        None => imp::stats(),
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn stats_is_callable_and_sane() {
        // On glibc hosts this exercises the dlsym path and the ABI struct; on
        // non-glibc hosts it pins the stub to `None`. A wrong `Mallinfo2`
        // layout shows up here as garbage negative-clamped values.
        if let Some(stats) = super::stats() {
            assert!(stats.in_use_bytes >= 0);
            assert!(stats.retained_bytes >= 0);
            assert!(stats.mmap_bytes >= 0);
            assert!(stats.arena_bytes >= 0);
            // The test binary itself has live allocations.
            assert!(stats.in_use_bytes > 0, "a running process has allocations");
        } else {
            assert!(
                !cfg!(all(target_os = "linux", target_env = "gnu")),
                "glibc build must resolve mallinfo2 (glibc >= 2.33)"
            );
        }
    }
}
