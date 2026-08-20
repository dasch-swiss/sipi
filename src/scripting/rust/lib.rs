//! The Rust-hosted Lua runtime (ADR-0023).
//!
//! Owns the Lua VM behind the preflight hooks, the configured Lua routes, the
//! docroot `.lua`/`.elua` pages, and the Lua config parse, built on mlua
//! (`lua53`; `mlua-sys` feature `external`) over the BCR `@lua` 5.3.6
//! interpreter. Every request VM is built to one hardened profile
//! ([`runtime::ScriptRuntime::request_vm`]): stdlib whitelist, scrubbed base
//! escape hatches, a Rust `os` shim, `require` restricted to the script dir,
//! a Lua-heap memory cap, and a wall-clock deadline enforced by an
//! instruction-count hook plus a checked-entry chokepoint on every binding.
//! Per-request isolation is a fresh VM per request; the per-request cost is
//! paid down by a bytecode cache ([`runtime::BytecodeCache`]) with mtime
//! invalidation, so script edits still take effect immediately.

// Fast unsafe check (CI `lint` gate): every `unsafe {}` block must carry a
// `// SAFETY:` comment. `allow`-by-default (clippy `restriction` group), so it
// is enabled here explicitly; CI's `-Dwarnings` promotes it to a hard error.
#![warn(clippy::undocumented_unsafe_blocks)]

pub mod limits;
pub mod runtime;

pub use limits::{kill_stats, Deadline, KillReason, KillStats, LimitConfig, RuntimeError};
pub use runtime::{config_vm, BytecodeCache, LoadError, RequestVm, ScriptRuntime};
