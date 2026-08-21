---
status: accepted
amends: 0017-extensibility-lua-and-rust.md
---

# Rust-hosted mlua Lua runtime

> **Amends [ADR-0017](0017-extensibility-lua-and-rust.md) (DEV-7013,
> 2026-08-20).** ADR-0017's contracts stand — runtime-loaded scripts with no
> toolchain, upload as a Lua route, host-side `traceparent`, TOML as the
> declarative config surface. What changes is its "a pure-Rust Lua runtime
> (mlua) is low priority" consequence: the sandbox and resource-limit
> hardening drivers invalidated its premise, and the Lua runtime is now
> hosted in Rust via mlua.

## Context

SIPI's Lua runtime — the `pre_flight` / `file_pre_flight` hooks, configured Lua
routes, `.elua` pages, and the Lua config parse — is hosted in C++
(`src/scripting/LuaServer.cpp`, ~3300 lines plus bindings). Four hardening
drivers make that hosting itself the problem:

- **Sandbox (DEV-5925).** Every VM opens the full Lua 5.3 stdlib
  (`luaL_openlibs`): `os.execute`, `io.popen`, `package.loadlib`, `debug.*` are
  reachable from any script, on top of 14 `server.fs.*` functions including a
  process-global `chdir`.
- **Resource limits (DEV-6070).** No allocator cap, no instruction hook, no
  timeout anywhere on the Lua path. A script infinite loop pins a
  `spawn_blocking` thread and holds its admission permit forever.
- **Per-request cost (DEV-6077).** A fresh VM re-parses the full init-script
  source on every preflight and every Lua route hit.
- **Correctness (DEV-6119).** `server.cookies` collapses multiple cookies and
  lowercases names.

Hardening the C++ runtime in place was viable, but every mitigation
(`luaL_requiref` whitelisting, `lua_setallocf`, `lua_sethook`, chunk precompile)
is hand-rolled C against a subsystem the strangler direction already expects to
leave. [ADR-0017](0017-extensibility-lua-and-rust.md) rated mlua "low priority"
on the premise that it *only* removes the C++ `LuaServer` dependency; the
hardening drivers invalidate that premise — a Rust-hosted runtime gets the
sandbox, limits, and lifecycle as first-class API instead of patches.

The June 2026 blocker (mlua-sys's build script probing pkg-config / vendoring
`lua-src` under the Bazel sandbox) no longer applies: mlua ≥ 0.11 ships an
**`external`** link mode whose build script does nothing — the bindings resolve
`lua_*` symbols at link time from a library already in the final link.

## Decision

**The Lua runtime moves to Rust** — a `rust_library` at `src/scripting/rust/`
(the [ADR-0021](0021-iiifparser-polyglot-colocation.md) colocation pattern; the
package keeps its name and commit scope, the implementation language flips) —
built on **mlua** (`lua53` + `serde`; `mlua-sys` feature `external`) linked
against the already-present BCR **`@lua` 5.3.6** `cc_library`. End state:
**zero C++ Lua**. The C++ engine keeps image/metadata work only, reached
through a handle-based `SipiImage` C ABI modeled on the `SipiRequestContext`
pattern.

### VM profile

Every request VM is built to one hardened profile:

- **Stdlib whitelist**: `string`, `table`, `math`, `utf8`, `package` only.
  `io` and `debug` are never loaded. Base-library escape hatches are scrubbed
  (`dofile`, `loadfile`, `load`, `collectgarbage`; `package.searchpath`, and
  `package.loadlib` — mlua's safe mode only stubs it to an erroring function,
  so the scrub removes it outright).
  `os` is replaced by a Rust-implemented shim of exactly `getenv`, `clock`,
  `date` — the audited union of what production scripts use.
- **Restricted `require`**: a single loader resolving `[A-Za-z0-9_]+` names
  against the configured script dir only; `package.path`/`cpath` neutralized.
- **Memory cap** (`Lua::set_memory_limit`): Lua-heap allocations only;
  the `server.http` response-body cap closes the request-amplifiable
  Rust-side hole.
- **Deadline**: an instruction-count hook checks a wall-clock deadline; on
  expiry it re-arms at every instruction so `pcall`-trapped scripts make no
  useful progress. Every binding enters through one checked-entry chokepoint
  that performs the deadline check (the `Icc::iccBytes()` single-chokepoint
  precedent), so a trapped timeout error still cannot do I/O. Blocking
  bindings derive budgets from the remaining deadline. Engine calls are
  uninterruptible — the deadline bounds Lua and bindings, not decode time.
- **Kill semantics**: pre-commit kill → 500 with a generic body; post-commit
  kill → the response stream is aborted, never a clean EOF. A killed preflight
  is never written to the preflight cache.
- **Per-request isolation kept**: a fresh VM per request. Cost reduction comes
  from a **bytecode cache** (init script, route scripts, and `require`d
  modules; `Function::dump` bytes loaded per VM as binary chunks), invalidated
  by mtime + size — script edits still take effect immediately. The base `load` scrub
  closes the attacker-supplied-bytecode hole the cache would otherwise open.
- **Fail-closed startup**: an init-script *error* refuses startup (the old
  probes failed open, silently disabling authorization); a hook genuinely not
  defined is the legitimate no-preflight mode. Probe results are boot-frozen;
  a post-boot script edit that breaks the init script is a request-time 500.

### Bindings: parity minus deliberate divergences

All `server.*` fields and functions are reimplemented in Rust at parity, with
these divergences (each e2e-tested and changelogged; the cutover is a breaking
change for script authors):

| Divergence | Rationale |
|---|---|
| `config.password`, `config.adminuser` dropped | credentials injected into every VM, including preflight |
| `server.shutdown` dropped | already a no-op on the FFI path |
| `server.fs.chdir` dropped | process-global CWD mutation from worker threads |
| `server.cookies` fixed (DEV-6119) | one entry per cookie, original-case names |
| stdlib whitelist / os shim | audit-proven unused surface |
| `decode_jwt` validates `exp`, algorithm pinned to HS256 | signature-only validation accepted expired tokens |
| `server.http` timeout is total-request (was connect-only) | converts a latent worker hang into a visible failure |
| `server.http` no redirect-following, response body capped | redirect-driven SSRF amplification; bodies evade the Lua memory cap |
| Config size strings (`cache_size`, `max_post_size`) parse strictly | C `stoll` silently truncated trailing garbage — `'1K'` meant 1 *byte*; now a startup error |
| Config integer/boolean keys are strictly typed at parse (as before), but errors surface pre-boot with chunk name + line, never a source echo | the config file carries `jwt_secret` literally; parity with the TOML redaction |

`server.header` keys stay lowercase and `_G` stays shared between init script
and route/hook chunk within a request VM — pinned invariants production
scripts depend on. A routed script missing on disk stays a request-time 404,
never a boot failure.

### Engine access

A new `extern "C"` `sipi_image_*` surface in `src/ffi/` (opaque handle,
create/mutate/write/send/free, deep-copied inputs, exception wall, panic
`catch_unwind` on callbacks) replaces the Lua-facing C++ `SipiImage` bindings.
The `SipiRequestContext` seam surface — preflight and lua-route FFI entries
included — is deleted: request data no longer crosses the seam at all.

## What we reject

- **Hardening the C++ runtime in place** — keeps ~5000 lines of C++ Lua alive
  indefinitely with the sandbox as hand-rolled C (maintainer decision,
  2026-08-20).
- **Luau** — strictly better sandbox, but a breaking dialect for existing user
  scripts; SIPI is a general-purpose IIIF server (ADR-0017).
- **Pure-Rust interpreters** (piccolo, hematita) — not production material as
  of 2026.
- **Vendored mlua Lua 5.4** — re-hits the `lua-src` sandbox failure, doubles
  Lua symbols while C++ Lua still exists, and 5.3 → 5.4 is not
  behavior-neutral. Possible later, irrelevant now.

## Consequences

- ADR-0017's contracts are **kept**: request-shaping extensions stay
  runtime-loaded scripts with no toolchain; upload stays a Lua route; the
  host-side `traceparent` injection carries over. Its "mlua is low priority"
  consequence is **superseded** by this ADR.
- One Lua runtime in the codebase. `LuaServer.cpp`, `LuaSqlite.cpp`,
  `SipiLua.cpp`, and the Lua config parse are deleted; the only Lua linkage is
  mlua's `external` binding against `@lua`.
- The FFI seam shrinks by its largest surface (`SipiRequestContext` +
  preflight + lua-route entries); the new `sipi_image_*` handle ABI is the
  engine's script-facing surface.
- Limits are operator knobs (`SIPI_LUA_MEMORY_LIMIT`, `SIPI_LUA_TIMEOUT_MS`)
  with kill metrics (`sipi.lua.kills`, rendered `sipi_lua_kills_total{reason}`) and structured kill logs.
- The bytecode cache's mtime invalidation preserves edit-takes-effect
  operations; a *new* preflight hook still requires a restart (probe results
  are boot-frozen).
- Lua config parsing happens in Rust (mlua evaluates `sipi.config.lua` into
  the same structs as TOML); TOML remains the co-equal declarative path.
