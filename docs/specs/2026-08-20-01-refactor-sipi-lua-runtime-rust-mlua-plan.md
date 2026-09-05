---
title: "refactor: Rust-hosted mlua Lua runtime replaces C++ LuaServer (hardened)"
type: refactor
date: 2026-08-20
author: "Ivan Subotic"
status: reviewed
repository: dasch-swiss/sipi
linear: DEV-7013
---

# refactor: Rust-hosted mlua Lua runtime replaces C++ LuaServer (hardened)

## Overview

Move SIPI's entire Lua runtime from C++ (`src/scripting/LuaServer.cpp`) into the Rust axum shell using **mlua** (feature `lua53` + `external`), linked against the already-present Bazel BCR `@lua` 5.3.6 `cc_library`. The migration delivers the three hardening items as properties of the new runtime rather than patches to the old one:

- **DEV-5925** — stdlib whitelist (no `io`, no `debug`, `os` reduced to an audited shim, restricted `require`)
- **DEV-6070** — per-VM memory cap (`Lua::set_memory_limit`) + execution deadline (instruction-count hook + wall-clock check)
- **DEV-6077** — per-request VM cost reduction (bytecode caching of the init script; pooling only benchmark-gated later)
- **DEV-6119** — `server.cookies` returns one entry per cookie with original-case names (fixed during the binding rewrite)

End state: **zero C++ Lua**. `LuaServer.cpp`, `SipiLua.cpp`, `LuaSqlite.cpp`, and the `SipiConf(LuaServer&)` config parse are deleted. The C++ engine keeps only image/metadata work; scripts reach it through a new handle-based C ABI (`SipiImage`), modeled on the existing `SipiRequestContext` pattern. This executes the strangler plan's deferred D+ Lua slice (`dasch-specs/specs/2026-06-19-sipi-rust-strangler/01-…-plan.md:203`), pulled forward by the hardening driver.

## Problem Statement / Motivation

The current Lua surface (investigated 2026-08-20, Linear project [Sipi Lua runtime hardening](https://linear.app/dasch/project/sipi-lua-runtime-hardening-b21226273cbc)):

- Every VM opens the **full** Lua 5.3 stdlib (`luaL_openlibs` at `src/scripting/LuaServer.cpp:229,243,260,288`): `os.execute`, `io.popen`, `package.loadlib`, `debug.*` — on top of `server.fs.*` (14 filesystem functions including a process-global `chdir`).
- **No resource limits**: no `lua_sethook`, no `lua_setallocf`, no timeout anywhere on the Lua path. A script infinite loop pins a `spawn_blocking` thread and holds its admission permit forever (`src/server-rs/src/routes.rs:997`).
- **Per-request VM + per-request init-script re-parse**: `make_lua_server` (`src/ffi/lua_config.cpp:35`) re-executes the full init-script source on every preflight and every Lua route hit.
- Bonus hazards: `config.password`/`config.adminuser` injected into every VM (`src/ffi/lua_config.cpp:114-120`); the raw `RequestContext*` stored as an overwritable Lua global (`LuaServer.cpp:2799` — script can cause a C++ null deref); `package.path` appended, never replaced; two dead VM-creating constructors (`LuaServer.cpp:223,237`); fail-open preflight probes (`routes.rs:151-152` — a probe failure silently disables authorization).

Hardening this in C++ was viable (investigation Path 2/3), but the maintainer chose Path 1: hosting the runtime in Rust makes the sandbox, limits, and lifecycle first-class API (`Lua::new_with(StdLib::…)`, `set_memory_limit`, `set_hook`) instead of hand-rolled C, deletes an entire C++ subsystem, and removes the seam's largest surface (the opaque `SipiRequestContext` + preflight + lua-route FFI entries) because request data no longer crosses the seam at all.

**Why the June 2026 blocker no longer applies:** the mlua spike was blocked on `mlua-sys`'s build script under crate_universe (pkg-config absent in the sandbox; `lua-src` absolute-path failure — recorded in `dasch-specs/specs/2026-06-19-sipi-rust-strangler/01-…-plan.md:131`). mlua ≥ 0.11 ships an **`external`** link mode whose build script does nothing: the hand-written FFI resolves `lua_*` symbols at link time from `@lua`, which is already in the final link of `//src/cli-rs:sipi` (via `//src/ffi:sipi_ffi`). No build-script probing, no vendored source, no duplicate symbols.

## Alternative Approaches Considered

- **Harden the C++ runtime in place** (investigation Path 2/3): selective `luaL_requiref`, `lua_setallocf`/`lua_sethook`, chunk precompile. Cheapest, but keeps `LuaServer.cpp` (~3300 lines) alive indefinitely, leaves the hardening as hand-rolled C against a codebase-wide direction that already anticipates the mlua slice (`src/scripting/request_context.h:22-23`). Rejected by maintainer decision 2026-08-20.
- **Luau instead of Lua 5.3**: strictly better sandbox (`Lua::sandbox()`, interrupts, no io/os by default) but a breaking dialect for existing user scripts (5.1-based: no `goto`, doubles-only numbers, different `os`/`string` surface). SIPI is a general-purpose IIIF server (ADR-0017); rejected.
- **Pure-Rust interpreters** (piccolo, hematita): not production material as of 2026. Rejected.
- **Vendored mlua Lua 5.4**: hits the original `lua-src` sandbox failure and would double Lua symbols while C++ Lua still exists; 5.3→5.4 is not ABI/behavior neutral (`MODULE.bazel:94-96` pin rationale). Possible later, irrelevant now.

## Proposed Solution

New Bazel package **`src/scripting/rust/`** (`rust_library` `//src/scripting/rust:scripting`, a dep of `//src/server-rs:lib` — the polyglot co-location precedent of `src/iiifparser/rust` and `src/throttling/rust`). The C++ Lua lives in the same `src/scripting/` directory and is deleted as the Rust lands, so the package and the `scripting` commit scope keep their name and meaning (the implementation language flips; the concern doesn't). server-rs passes in the response-writer callback, config, and metric handles; the `sipi_image_*` extern declarations live in this crate, which deps `//src/ffi:sipi_ffi` exactly as server-rs does:

```
src/scripting/rust/
  runtime.rs     — VM factory: StdLib whitelist, base scrub, restricted require,
                   memory limit, deadline hook, bytecode cache
  limits.rs      — knobs (env), deadline state shared with bindings, kill accounting
  bindings/
    server.rs    — server.* table (fields + ~30 fns), response sink writes
    config.rs    — config table (minus password/adminuser)
    image.rs     — SipiImage userdata over the new sipi_image_* C ABI
    sqlite.rs    — sqlite/Stmt userdata
    helpers.rs   — helper.filename_hash, uuid/base62
  entry.rs       — pre_flight / file_pre_flight / run_lua_route / elua / config-parse
```

### VM profile (the hardening core)

- `Lua::new_with(StdLib::STRING | StdLib::TABLE | StdLib::MATH | StdLib::UTF8 | StdLib::PACKAGE, …)`. Base is always open in mlua; **scrub** `dofile`, `loadfile`, `load`, `collectgarbage` from globals. `os` is not loaded; install an **os shim table**: `getenv` + `clock` + `date` — the frozen union of both audits (`env_echo.lua` asserts `os.getenv`; `test_thread_isolation.lua` uses `os.clock`; dsp-api `log_util.lua:5` calls `os.date` inside `log()` on **every production request** — audit `02-…-audit` D1). The shim is a Rust reimplementation — `date` is a strftime-subset in Rust, never a passthrough to C `strftime`; `getenv` stays unrestricted (deliberate: scripts legitimately read deployment env, audit D11; an allowlist would be defense-in-depth). `io`, `debug`: never loaded (zero live uses across both repos). mlua's safe mode already disables `package.loadlib`/C-module loading when `PACKAGE` is open (verified against mlua 0.12 source); scrub `package.searchpath` as well, and the loadlib absence is e2e-asserted.
- **Restricted `require`**: replace `package.searchers` with a single loader resolving `[A-Za-z0-9_]+` module names against the configured script dir only; `package.path`/`cpath` neutralized. All 27 existing `require` uses are literal names in the script dir.
- Whitelist invariants proven by the audits and pinned by tests: the string metatable stays linked to the `string` table (path-traversal guards use `identifier:find`), `table` stays mutable (`scripts/upload.lua:26` monkey-patches `table.contains`), `_G` writable AND shared between the initscript and the route/hook chunk within a request VM (dsp-api's entire inter-module contract is bare globals — audit D9; isolation is *across* requests only), **`server.header` keys stay lowercase** (dsp-api JWT auth does exact lowercase lookups for `authorization`/`cookie` — audit D6; only `server.cookies` moves to original case per DEV-6119, which dsp-api provably never reads).
- **Memory cap**: `lua.set_memory_limit(n)` — allocation beyond it raises `Error::MemoryError`. Scope: the cap counts **Lua-heap allocations only**; Rust-side allocations (http response bodies, `serde_json` values, sqlite rows) live outside it — the `server.http` body cap is what closes the one request-amplifiable hole.
- **Deadline**: `set_hook(HookTriggers::new().every_nth_instruction(N), …)` checking a wall-clock deadline. Two hardening details from spec review: on expiry the hook **re-arms at count 1** (so `pcall`-trapped scripts cannot make useful progress), and **every binding routes through a single checked-entry chokepoint** that performs the deadline check — one wrapper on the VM context, structurally unbypassable (the `Icc::iccBytes()` chokepoint precedent), with a registry-enumeration test asserting every registered binding goes through it — so a trapped timeout error still cannot do I/O. Blocking calls get budgets derived from the *remaining* deadline: `server.http` total-request timeout, sqlite progress/busy budget. `SipiImage` engine calls are uninterruptible: the deadline bounds Lua execution and bindings, **not** engine decode time — a crafted upload can still hold a worker for the full decode (unchanged from today; bounded by `SipiMemoryBudget`, and operators size the pool with this in mind). A `pcall`-trapped `Error::MemoryError` leaves the VM over its limit, so subsequent allocations keep failing — wedged but harmless; the request still terminates through the kill path.
- **Kill semantics** (spec'd, tested): pre-commit kill → 500 with generic body; post-commit kill (Lua routes stream) → truncate and abort the body stream so clients never see a clean EOF; `.elua` identical. A killed preflight is a 500 and is never written to the preflight cache; a preflight that already committed its own response (return-shape 5, `false` after `sendStatus`) follows the post-commit truncate rule instead. Mechanically the abort requires changing the streaming sink's body channel to carry `Result<Bytes, _>` — today `sink.rs:265` maps every chunk to `Ok`, so dropping the sender produces a clean EOF and cannot express an abort.
- **Per-request isolation kept**: fresh VM per request (pinned by the `lua_state_thread_isolation` e2e). Cost reduction comes from a **bytecode cache** covering the init script, route scripts, AND `require`d modules (the restricted loader loads from the same cache — dsp-api's live closure is 9 files, 8 of them arriving via `require`, so an init-only cache would forfeit most of the win): compiled once via `Function::dump(strip=false)` (keep debug info — error-message-shape parity), cached as bytes (never as VM-tied `Function`s), loaded per VM with `ChunkMode::Binary` + `set_name`. Binary chunks load in mlua safe mode (≥0.8), and the base `load` scrub closes the attacker-supplied-bytecode hole the cache would otherwise open. Invalidated by mtime (preserves today's edit-takes-effect-immediately operations; no silent behavior change).
- **Fail-closed startup**: the has-preflight probes currently fail open (`routes.rs:151-152`). New rule: init-script *error* under the new runtime → refuse startup; hook genuinely not defined → legit no-preflight mode. Probe VMs run under the request-VM limit profile so the probe is an honest canary; the startup config-parse VM runs without request limits (trusted startup path). Probe results are boot-frozen (`AppState`, `routes.rs:151-152`): after a post-boot script edit, an init-script error or a vanished hook while `has_preflight=true` is a request-time 500 (fail closed); a newly *added* hook takes effect only on restart.

### Bindings: parity minus hazards

All `server.*` fields and functions reimplemented in Rust with these **deliberate divergences** (each e2e-tested and changelogged):

| Divergence | Rationale |
|---|---|
| `config.password`, `config.adminuser` dropped | credentials in every VM incl. preflight; only consumer is the test-only `test/_test_data/scripts/cache.lua` — rework that script/e2e; a Rust cache-admin surface is DEV-6404's concern |
| `server.shutdown` dropped | already a no-op on the FFI path (`request_context.h:128-132`) |
| `server.fs.chdir` dropped | process-global CWD mutation from worker threads |
| `server.cookies` fixed per DEV-6119 | one entry per cookie, original-case names |
| stdlib whitelist | audit-proven unused surface (both repos) |
| JWT `exp` validated by `decode_jwt` (HS256 pinned) | today libjwt checks signature only (`LuaServer.cpp:2144-2209`); **de-risked by audit D5**: dsp-api already self-checks `exp` against `server.systime()` and never mints JWTs in Lua — only the 401 message shape changes |
| `server.http` timeout param becomes total-request (was connect-only) | audit D4: converts a latent worker hang on slow dsp-api `/admin/files` into a visible failure the script already maps to a 404-class response; gated on a p99 check |
| `server.http` stops following redirects and caps the response body (was: curl `FOLLOWLOCATION`, unbounded body) | red-team: redirect-driven SSRF amplification, and Rust-side bodies evade the Lua memory cap; dsp-api's single live call is a direct internal GET |

Route-target policy: a routed script that is missing on disk stays a **request-time 404**, never a boot failure — dsp-api's production config still routes two deleted scripts (audit D12). `server.fs.mkdir`'s decimal mode-argument interpretation is preserved (dsp-api passes `511`).

Implementation mapping: fs → `std::fs` (13 fns); json ↔ table → `serde_json` + mlua `serde` feature; `uuid`/`uuid62`/`uuid_to_base62`/`base62_to_uuid` → Rust port of sole's base62 (**load-bearing DSP IRI scheme — golden-vector tests against C++ outputs, round-trips, malformed input**); jwt → `jsonwebtoken` with `rust_crypto` (never `aws-lc-sys` — `MODULE.bazel:505-524` constraint); http → `reqwest` (blocking; one process-wide `OnceLock<reqwest::blocking::Client>` — a per-request blocking client is the classic constructed-inside-the-runtime panic path — with per-request `RequestBuilder::timeout(remaining_deadline)`; rustls/ring; red-team requirements: response-body size cap (Content-Length check + streamed cap — this also bounds `json_to_table` of fetched bodies) and redirect-following OFF by default (divergence-table row); no SSRF host-allowlisting — URL construction from request inputs is the script's boundary, documented; parity checklist: timeout param semantics now total-not-connect, multi-value headers, TLS trust source); mimetype → existing libmagic seam entries; log/loglevel → `tracing` with the same level constants; `server.print`/`sendHeader`/`sendCookie`/`sendStatus`/`setBuffer` → **direct Rust writes to the streaming response** (the FFI response-sink hop disappears; bare Lua `print` stays stdout per the Phase 0 decision); `systime`, `requireAuth`, `parse_mimetype`, `file_mimetype`, `file_mimeconsistency`, `table_to_json`/`json_to_table`, `copyTmpfile`, `generate_jwt`/`decode_jwt` — 1:1. Field parity includes the omitted-when-empty conditionality (`get`/`post`/`uploads`/`content`) that scripts branch on, and `has_openssl` (keep, hardcoded `true`, as today).

### Engine access: the `SipiImage` handle C ABI

New `extern "C"` surface in `src/ffi/`, modeled verbatim on `SipiRequestContext` (opaque `#[repr(C)]` handle, create/mutate/free, deep-copied inputs, no owned C-string outputs, `sipi_guard`-class exception wall, Rust `catch_unwind` on every callback):

- `sipi_image_new(path, region?, size?, reduce, original?) → *mut SipiImageHandle` (NULL on failure + error-message emit callback preserving current `false, msg` shapes scripts may match on)
- `sipi_image_dims / crop / scale / rotate / topleft / watermark / exif_get / gps / mimetype_consistency`
- `sipi_image_write(handle, path, ftype, params)` — carries the **Essentials packet** fields for `file_role="service-file"` (origname, mimetype; pixel-hash/ICC/dims computed engine-side as today, `SipiLua.cpp:1540-1583`; TIFF forces pyramid) — this is the DEV-6537 upload wiring, unchanged
- `sipi_image_send(handle, ftype, params, write_cb, ctx)` — streams through a Rust callback; no Rust panic crosses the C++ codec frames
- `sipi_image_free`

Rust side: `SipiImage` userdata owning the handle with `Drop` → kill/unwind at any point frees all handles. Contract doc (ownership, error channel, reentrancy, callback blocking during a killed-VM unwind) written **before** Phase 4 code. `helper.filename_hash` gets a small FFI wrapper over `SipiFilenameHash` (must stay byte-identical — it derives storage paths).

`sqlite` binding: Rust, via `rusqlite`/`libsqlite3-sys` **linked against the existing BCR `@sqlite3`** (annotation shape: `SQLITE3_INCLUDE_DIR`/`SQLITE3_LIB_DIR` via `build_script_env` + `build_script_data` supplying the Bazel files, plus a `deps = ["@sqlite3"]` annotation so linkage comes from Bazel, not build-script link-search paths; never the `bundled` feature — duplicate sqlite symbols against `@sqlite3` while C++ `LuaSqlite` still links it — and no `buildtime_bindgen`; same `-sys`-under-sandbox class as mlua, proven in the same spike phase). Fix the C++ version's Stmt-outlives-Db finalizer hazard with owned lifetimes. Fallback if `libsqlite3-sys` misbehaves under crate_universe: thin FFI over a retained C++ sqlite shim (last resort; prefer Rust).

### Entry points

`entry.rs` replaces `ffi::preflight` / `ffi::file_preflight` / `ffi::has_preflight` / `ffi::has_file_preflight` / `sipi_run_lua_route` and the whole `RequestContext`-across-the-seam machinery: `routes.rs` hands the Rust-native request data straight to the runtime. Preflight return-shape polymorphism (`'allow',path` / `'deny'` / `'restrict:…'` string / restrict table / `false` after a direct `sendStatus` response / malformed) gets a table-driven unit-test matrix. `.elua` interleaving reimplemented with pinned edge cases (unterminated `<lua>`, zero chunks, state sharing across chunks); docroot `server.docroot` injection and the readable-check/open 404 TOCTOU semantics carried over verbatim. Config parsing: mlua evaluates `sipi.config.lua` (config-VM whitelist includes `os.getenv` — docker configs use it) into the existing `config_file.rs` structs; TOML stays the co-equal path; the `routes` global feeds the axum router as today.

### Operational surface

- Knobs (env, matching the pool-knob precedent `SIPI_NTHREADS`/…): `SIPI_LUA_MEMORY_LIMIT` (bytes), `SIPI_LUA_TIMEOUT_MS`, hook instruction period. Defaults sized in Phase 2 by measuring the DSP init-script footprint; the limit must comfortably cover init execution.
- Metrics recorded directly against the Rust OTel meter (instrument handles passed in from server-rs's `metrics.rs`) — the engine's scalar FFI snapshot bridge is NOT involved (it is scalar-only and cannot carry labeled families): `lua_kills_total` split by reason (timeout/memory), VM-build + script-duration counters/histograms per entry point.
- On kill: one structured log line (script path, entry point, reason, elapsed, memory).
- Docs: `docs/src/lua/` pages updated (sandbox contract, limits, divergences).

## Technical Considerations

- **Bazel**: first `crate.annotation()` in the repo, and it is **required unconditionally** — `external` is a feature of `mlua-sys`, not of `mlua` (mlua 0.12 has no passthrough feature), so the hub gets `crate.spec(package = "mlua", features = ["lua53", "serde"])` **plus** `crate.annotation(crate = "mlua-sys", crate_features = ["external"])`. `jsonwebtoken[rust_crypto]` and `reqwest[blocking, rustls-tls]` are already in the hub (MODULE.bazel:562,566) — reuse in `server-rs` is a BUILD-label change only. While C++ Lua exists, `lua_*` symbols come from the existing `@lua` in the final link; in Phase 6, `@lua` moves to an explicit dep of the Rust target. Repin via `CARGO_BAZEL_REPIN=1 bazelisk fetch @crates//:all`. RBE: build scripts execute remotely — the spike must pass the cross-compile legs.
- **ABI compatibility is a named spike exit criterion, not a discovery**: @lua is compiled as C (longjmp error handling) — verify mlua's `lua53` bindings' `luaconf.h` assumptions (error mechanism, `LUA_COMPAT_*`, integer/float sizes) against the BCR `lua` 5.3.6 module's compile flags (no local `bazel/lua.BUILD*` exists — lua is a BCR `bazel_dep`, MODULE.bazel:97; inspect the module's build file in the BCR source). During Phases 1–5 the same `@lua` serves both LuaServer and mlua in one binary — same symbols once, but config-flag mismatch would be UB.
- **mlua threading**: no `send` feature; VMs are created, used, and dropped on one `spawn_blocking` thread per request. `!Send` is correct and cheapest.
- **No shims**: each entry point has exactly one implementation at any commit — its seam entry is deleted in the same commit that cuts it over (per-entry-point cutover, not a parallel run).
- **Hot-path rule**: preflight VM build + init execution is on the uncached IIIF path. A microbench (VM build + init + hook overhead, old vs new) is a Phase 2 exit criterion, not a Phase 7 afterthought.
- **Commit scopes**: `scripting` (the Lua runtime — runtime/bindings/entry, Rust from now on, plus the C++ deletions), `server-rs` (routes/sink/glue), `ffi` (image handle ABI, seam deletions), `lua` (script/config changes), `deps`/`bazel` (MODULE.bazel), per `CONVENTIONS.md:105-138`.
- **Production-surface rule**: no oracle/roadmap references in the new Rust comments; describe the runtime on its own terms.
- **Docs land with the facts** (dune review): ADR-0023 in Phase 1. The ARCH-MAP diff — new `scripting/rust` component stub (noting a local-context-kit budget exception for the `bindings/` fan-out, same format as the `formats` entry) and the corrected `ffi` entry (its "Lua config VM" durable-state line goes false in Phase 2) — lands in the same Phase 2 commit that invalidates the old facts. Likewise the `CONVENTIONS.md` scope-table line and `UBIQUITOUS_LANGUAGE.md` stub entries for the new terms (kill semantics, os shim, VM profile, bytecode cache) land with the code that introduces them. Phase 6 is the completeness pass, not the first write.
- **Release & rollback**: the migration lands as **one PR**, phases as separate self-contained commits — all or nothing (maintainer decision, 2026-08-20). Rebase-merge puts each commit on main verbatim, but nothing is released until the PR merges, so exactly one release ships the whole migration. The 5a cutover commit carries `!` with a BREAKING CHANGE footer enumerating the full divergence table (one major bump; the cutover is breaking for script authors — SIPI is general-purpose, ADR-0017). Rollback is an image repin in dsp-api's two pins (the runtime is stateless; caches are in-memory) — deliberately no canary beyond the Phase 7 dsp-api integration-suite run, which gates the merge.

## Implementation Phases

Delivery mode: **one PR, all phases as separate self-contained commits, merged all-or-nothing.** Every commit must build green and keep exactly one implementation per entry point (no shims), because rebase-merge lands each commit on main verbatim — but no intermediate state is ever released or deployed.

#### Phase 0: Decisions + dsp-api script audit (gate for the whitelist)

- [x] Audit dsp-api's SIPI scripts + configs (done 2026-08-20 — see `02-audit-dsp-api-lua-surface.md`; whitelist and os shim frozen: `getenv` + `clock` + `date`; production-live closure is 9 files; `server.header` lowercase pin and shared-`_G` invariants added; missing-route-script = request-time 404)
- [x] JWT policy de-risked by audit D5 (dsp-api self-checks `exp`, never mints JWTs in Lua) — decision: `decode_jwt` validates `exp`, HS256 pinned; changelog the 401 message-shape change
- [x] Golden-token test vectors recorded (expired / no-exp / wrong-alg) — `bindings_tests.rs::jwt_round_trip_and_hardened_validation` (plus the aud-carrying-token regression)
- [x] Validate dsp-api `/admin/files` latency ≪ 5 s (audit D4) — done 2026-08-20 via Grafana Cloud: Tapir metric (`tapir_request_duration_seconds`, count+sum only) mean 9.9 ms over 30 d / 372 k requests; trace span metrics p50 7.6 ms, p99 36 ms, p99.9 208 ms (7 d); ~8 of 243 k requests (0.003 %) exceeded 5 s in 30 d, all inside 2–3 brief degradation windows (worst 5-minute average 9.7 s). In exactly those windows today's connect-only timeout hangs the SIPI worker indefinitely; the total timeout fails them cleanly after 5 s. **Verdict: 5 s total timeout is safe and an improvement — gate cleared**
- [x] dsp-api cleanup PR (recommended, non-blocking): delete `cache.lua`, `exit.lua`, `debug.lua`, `json.lua`, `util.lua:82-95` (`io.popen` dead code); remove the 4 dead route entries; fix or drop `test_knora_session_cookie.lua` — opened as dsp-api [#4263](https://github.com/dasch-swiss/dsp-api/pull/4263) (2026-08-21; dropped `test_knora_session_cookie.lua` + its routes, also removed the unused `deleteTempFileRoute` Scala config field)
- [x] Decisions recorded: fail-closed probes (init error fatal, hook-absent = no-preflight), cache-admin test-script rework (`config.password` removal — SIPI-repo test script only; dsp-api's `cache.lua` is unrouted dead code), `print` stays stdout
- [x] Linear project target date stays 2026-08-23 (maintainer decision, 2026-08-20)

#### Phase 1: Bazel mlua enablement spike

- [x] Add `mlua` to the `@crates` hub: `crate.spec(package = "mlua", version = "0.12", features = ["lua53", "serde"])`, repin (done 2026-08-20)
- [x] Add `crate.annotation(crate = "mlua-sys", crate_features = ["external"])` — no link-input annotations needed (the spike passed with `@lua` as a direct dep of the rust_test); pattern documented in `MODULE.bazel` comments
- [x] `rust_test` proving: VM create via external-linked `@lua`, chunk exec, `set_memory_limit` enforcement, instruction hook firing — `//src/scripting/rust:mlua_external_link_test`, 4/4 green on macOS first run
- [x] Verify `luaconf.h`/ABI assumptions of mlua `lua53` against the BCR `lua` module's compile flags — verified: stock luaconf.h compiled as C (longjmp), only `LUA_USE_MACOSX`/`LUA_USE_LINUX` defines; `LUA_INT_LONGLONG`/double defaults match mlua-sys i64/f64; default `LUA_EXTRASPACE` matches mlua-sys's hardcode; no `LUA_COMPAT_*` symbols referenced. Written down in the test header + MODULE.bazel comment; i64-precision test is the runtime canary
- [x] Assert `panic=unwind` in the Rust build profiles — nothing sets a panic strategy anywhere (rules_rust default unwind); pinned by the `panics_unwind` test
- [x] ADR-0023 is the **first commit of the migration PR** (80b8f42e) — supersedes ADR-0017's "mlua is low priority" consequence; 0017's consequence line struck through with a pointer
- [x] Green on macOS + linux-x86_64 + linux-aarch64 incl. the RBE cross-compile legs — PR #789 CI all green (test matrix ×3, asan-ubsan, docs, commit-lint), 2026-08-21

#### Phase 2: Rust runtime core (`src/scripting/rust/`)

- [x] New Bazel package `//src/scripting/rust:scripting` (`rust_library`, dep of `//src/server-rs:lib`; rustfmt/clippy gate lists updated — rust-project discovers rust targets automatically)

- [x] VM factory: StdLib whitelist, base scrub, `package.searchpath` scrub, os shim, restricted `require`, path/cpath neutralized, `catch_rust_panics(false)` — note: mlua safe mode only STUBS `package.loadlib` to an erroring fn, so the scrub removes it outright (ADR-0023 updated)
- [x] Memory limit + deadline hook with re-arm-at-1 on expiry (set_hook-inside-hook works — mlua's lock is reentrant and the callback is cloned before invocation); binding-entry chokepoint via `RequestVm::register_binding` + `verify_bindings_checked` enumeration
- [x] Sink body channel carries `BodyItem = Result<Bytes, BodyAbort>` — an Err propagates as an axum Body error so hyper resets the connection (post-commit abort expressible)
- [x] Kill-semantics wiring at the entry points: pre-commit → 500, post-commit → send `Err(BodyAbort)` (extended to uncaught post-commit script errors after review); killed preflight never cached — all e2e-proven in `lua_hardening.rs`
- [x] Bytecode cache (init + route scripts + `require`d modules; dump keeps debug info, binary-chunk reload, mtime+size invalidation)
- [x] Limit knobs (`SIPI_LUA_MEMORY_LIMIT`, `SIPI_LUA_TIMEOUT_MS`, `SIPI_LUA_HOOK_PERIOD`; invalid values fail startup) — defaults 64 MiB / 5 s / 1000 instructions; the dsp-api closure allocates well under 1 MiB and inits in 147 µs, so the defaults have generous headroom (`from_env` is called at the 5a wiring)
- [x] Metrics: `sipi.lua.kills{reason}` observable counter over the runtime's KillStats + structured kill log line
- [x] VM-build/script-duration histograms per entry point (deferred to Phase 7 wrap-up with the metrics polish)
- [x] Unit tests: whitelist invariants, limit enforcement, `pcall`-resistance (incl. trapped-kill-returns-normally), require restriction, chokepoint bypass detection, os shim semantics (17 tests)
- [x] Send boundary: `RuntimeError` (Killed/Script/Setup, owned strings) classified by `RequestVm::run` at the blocking-thread boundary
- [x] Extend `SipiServerConfig` with `hostname`/`sslport` + lock-step layout guards both sides (240 → 256 bytes); removed again in the Phase 6 cleanup commit
- [x] Lua config parsing in Rust (`scripting::entry::parse_config_file` → `ServerOverrides::from_lua_config`; routes sourced Rust-side for both config flavors; config VM includes `os.getenv`; full e2e suite green). Divergence found+documented: strict size-string parsing (C stoll silently truncated `'1K'` to 1 byte; upload e2e fixture fixed to `'1024'`)
- [x] Lua config-parse errors sanitized (cut at the `near '…'` source echo; chunk name + line survive) — pinned by a regression test with a secret literal
- [x] `jwtkey`/`adminpasswd` render `[redacted]` via a manual `Debug` on `ServerOverrides`
- [x] Deleted `sipi_init`'s Lua-config branch (and its `lua_config_path` param), `SipiConf(LuaServer&)`, plus the orphan chain (configRoute/LuaRoute, SipiConf routes+drain_timeout, LuaConfig.routes, sipi_routes/sipi_port both sides); `test/unit/configuration` ported to `//src/scripting/rust:config_parse_test`
- [x] Docs-with-facts: ARCH-MAP (`scripting` polyglot entry + corrected `ffi` purpose/durable-state lines), `CONVENTIONS.md` scope row, glossary terms (VM profile, os shim, Kill semantics, Bytecode cache)
- [x] Microbench (paired manual binaries, 2026-08-21, darwin-aarch64 -c opt): dsp-api 9-file closure per-request VM+init — C++ 739 µs vs Rust 147 µs (~5×); SIPI init script 102 µs vs 34 µs; VM build alone ~21 µs both; count-hook tax ~1.6× on pure-compute Lua (per-instruction counter, period-independent; noise for I/O-shaped scripts); Rust-no-hook ≈ C++ (772 vs 779 µs) confirms attribution

#### Phase 3: Pure-Rust `server.*` bindings

- [x] `server` table fields with omitted-when-empty parity (`get`/`post`/`request`/`uploads`/`content`/`content_type`), `has_openssl` kept, lowercase header keys pinned
- [x] `config` table (`bindings/config.rs`): full `sipiConfGlobals` inventory minus `password`/`adminuser` (18 fields; a `ConfigValues` struct the shell populates at wiring)
- [x] `server.fs.*` (13 fns, no `chdir`; strerror-shaped messages, dotfile-filtered readdir, decimal mkdir mode pinned)
- [x] `table_to_json`/`json_to_table` via serde_json with the C++ conversion rules (3-space indent, mixing-keys error strings, empty-table→nil, 0-based arrays, null vanishes); full parity re-checked by the e2e suite at cutover
- [x] uuid/base62 Rust port; golden vectors computed with the C++ sole 1.0.1 (encode/decode/round-trip); divergence: malformed input rejected with (false,msg) instead of sole's garbage-out
- [x] `generate_jwt`/`decode_jwt` via jsonwebtoken[rust_crypto]; HS256-pinned Validation + exp required/validated; golden tokens (expired / no-exp / alg-none / wrong-secret) all rejected — this also closes the open Phase 0 golden-token-vectors item
- [x] `server.http` via reqwest: shared OnceLock client, total timeout = min(param, remaining deadline), redirects off, 16 MiB body cap, traceparent injection; tested against an in-process TCP mock (result shape, redirect not followed, total timeout). Found while porting: the C++ 4-arg form silently IGNORED its timeout argument (and the 3-arg int form read arg 1) — production effectively always ran the 2000 ms connect timeout
- [x] `parse_mimetype` (pure-Rust port of the Content-Type grammar, no libmagic needed)
- [x] `file_mimetype`/`file_mimeconsistency` via the libmagic seam — landed with the Phase 4 `sipi_image_*` ABI (`bindings/server.rs`)
- [x] `log`/`loglevel` → tracing; `print`/`sendStatus`/`sendHeader`/`sendCookie`/`setBuffer` → ResponseWriter (commit-on-first-write, cookies render at commit); `requireAuth` (all quirk shapes pinned); `systime`; `copyTmpfile`
- [x] `server.cookies` DEV-6119 fix in the binding surface (one entry per cookie, original case; unit-pinned)
- [x] Update `sipi.init-knora.lua`/session-cookie e2e expectations (landed with the Phase 5 cutover; full e2e 30/30)
- [x] `test/_test_data/scripts/cache.lua` deleted: unrouted dead code, sole `config.password`/`adminuser` consumer, and it calls a `cache` global that has no registration anywhere (the cache e2e never used it)
- [x] `server.send_error` latent bug fixed at all 4 call sites (→ the `send_error` global); negative regression test + registration-site comment pin the deliberate parity gap; new e2e drives upload.lua's error path (TIFF-magic garbage → clean 500 JSON). Note: the filename_hash error sites themselves are only reachable via directory-tree corruption, so the e2e exercises the same send_error mechanism via the SipiImage.new failure path
- [x] Table-driven binding unit tests (42 tests across bindings_tests/helpers; field probes driven from the createGlobals inventory; chokepoint enumeration over every binding table)

#### Phase 4: Engine-backed bindings (new C ABI)

- [x] `SipiImage` handle ABI contract doc — written as the header doc block on the declarations (the seam's contract style), before the implementation in the same commit: ownership, error channel, reentrancy, callback-during-unwind rules, geometry validation (region/size parse-validated at the seam, reduce >= 0, range clamping in the engine paths)
- [x] `sipi_image_*` C ABI in `src/ffi/image_handle.cpp` (full method set + tostring + filename_hash + file_mimetype/mimeconsistency), sipi_guard wall; seam-probe tests are the Rust `image_test` suite driving the whole ABI from Lua against the real engine. Two historical bugs deliberately fixed (zero script consumers, flagged for review): the exif end()-deref UB on unknown tags, and gps() reading the latitude key for longitude + a malformed altitude key
- [x] Essentials-packet fields in `sipi_image_write` (origname/mimetype in; SHA-256 pixel hash/ICC/dims engine-side; TIFF pyramid forced) — service-file JP2 write e2e-shaped in image_test
- [x] Rust `SipiImage` userdata with Drop-owned handle; methods dispatch through the chokepointed `SipiImage` table via the userdata metatable's `__index` (enumeration-covered); `send` streams via a catch_unwind-wrapped trampoline
- [x] `helper.filename_hash` FFI wrapper (byte-identical `SipiFilenameHash` engine-side)
- [x] `sqlite` binding — implemented as a thin hand-written Rust FFI directly over BCR `@sqlite3` (the proven mlua↔@lua external-link pattern) instead of rusqlite/libsqlite3-sys: the -sys build script emits `-lsqlite3` link-search directives that would resolve against the SYSTEM sqlite (macOS ships one) while `@sqlite3` links statically — a concrete collision, plus RBE build-script fragility; the fallback authority covered this call. Stmt holds shared ownership of the connection (use-after-close fixed; `~db` → clean 'database is closed' error), busy timeout from the remaining VM deadline, mode argument fixed (was dead), 64-bit binds; test_sqlite.lua semantics pinned by unit tests (0-based rows, Lua-error convention), the script itself re-runs at cutover

#### Phase 5: Entry-point cutover (each sub-step a separate self-contained commit; seam entry deleted in its own cutover commit; 5a depends only on Phases 2-3 — preflight never touches `SipiImage`/sqlite, so it need not wait for Phase 4)

- [x] 5a: preflight + probes in Rust — fail-closed startup (init error refuses boot; hook-absent = legit no-preflight; boot-frozen probes; hook-vanished-post-boot = request-time 500); return-shape matrix pinned by 14 entry tests; the preflight-cache key composition + pure-function invariant carry over unchanged (credential-isolation e2e pins it); the four seam entries + PreflightCapture/DirectResponse deleted both sides. Divergence-table e2e updates: expired-JWT test asserts rejection; cache fixture token carries exp
- [x] 5b: configured routes + upload in Rust — uploads table + temp-file RAII (killed script still drops them), kill semantics wired (pre-commit 500 / post-commit BodyAbort stream reset / plain script error keeps transport truncation); sipi_run_lua_route deleted. Kill-semantics e2e lands with the Phase 7 hardening suite
- [x] 5c: docroot `.lua`/`.elua` in Rust (folded into the 5b commit — both flow through one dispatch point); elua interleave with shared cross-chunk state, unterminated-`<lua>` executes the remainder then fails (the historical code errored there via out-of-range substr), missing-script 404; `server.docroot` on the request data; elua edge-case tests land with Phase 7
- [x] The `SipiRequestContext` seam surface deleted (make/free/mutators + ffi.rs owner + SipiStrPair + response_sink.h) — request data no longer crosses the seam at all

#### Phase 6: C++ Lua deletion + docs (config parsing moved to Phase 2)

- [x] Delete `src/scripting/LuaServer.{h,cpp}`, `LuaSqlite.{h,cpp}`, `src/ffi/SipiLua.cpp`, `lua_config.{h,cpp}`, `preflight.cpp`, `run_lua_route.cpp` (C++ side), shrink/remove `request_context.h`
- [x] Move `@lua` to a direct dep of the Rust target; drop `@sole` (after the Rust base62 port), drop `@curl`/`@jansson` from scripting if unconsumed elsewhere; remove the transitional `hostname`/`sslport` seam fields added in Phase 2 (the `config` table is Rust-built after cutover)
- [x] ADR-0023 completeness pass: fold in any sandbox/limits/kill-semantics decisions that shifted during implementation (the ADR itself merged in Phase 1; contracts kept: upload stays a Lua route, host-side traceparent, full-request fidelity; records the mtime-cache operational semantics)
- [x] Docs completeness pass: `UBIQUITOUS_LANGUAGE.md` (incl. naming the upload route as the documented `file_role`/Essentials exception to the convert-pipeline rule — DUNE-007), `ARCH-MAP.md`, `CONVENTIONS.md` scope table (`scripting`: implementation language flips to Rust, concern unchanged), CLAUDE.md component table, `docs/src/lua/` pages (incremental diffs already landed with earlier phases; this closes the gaps)

#### Phase 7: Hardening verification + performance

- [x] Hardening e2e: timeout kill (500 / truncation), memory kill, `io`/`debug` absent, os shim exact (`getenv`/`clock`/`date` present, `execute`/`popen`-class absent), restricted require, `package.loadlib` absent, `server.shutdown`/`server.fs.chdir`/`config.password`/`config.adminuser` absent, fail-closed startup on bad init script, post-boot script-edit fail-closed (init error → request-time 500), lowercase `server.header` keys, missing-route-script 404 (`test/e2e/tests/lua_hardening.rs`)
- [x] e2e: a killed (timeout/memory) preflight returns 500 and is never written to the preflight cache
- [x] Run the migrated SIPI against a copy of dsp-api's production Lua closure (the 9 live files + `sipi.docker-config.lua`) as an e2e fixture, not just SIPI's own scripts (`test/e2e/tests/dsp_api_closure.rs` + `test/_test_data/dsp-api/`; caught and fixed a real bug: jsonwebtoken's validate_aud default rejected every aud-carrying token)
- [x] Attempt dsp-api's integration suite locally against the migrated SIPI: image cross-built (linux-arm64), loaded via a local registry, dsp-api scripts overlaid as `knora-sipi:latest`; `//modules/test-it` ran — 18 SipiIT failures, but a baseline run against the UPSTREAM v6.4.1 image fails the identical 18 (all-404 pattern: the testcontainers images bind-mount is empty under this macOS Docker setup). Verdict: pre-existing local-env failure, not attributable to the migration; `test_gravsearch_span` passed both runs. The in-repo `dsp_api_closure.rs` e2e covers the closure behavior
- [x] Run dsp-api's integration suite against the migrated SIPI before release — DONE locally 2026-08-21 after root-causing the env blocker: the 18 SipiIT all-404 failures were colima not sharing `/tmp` with its VM (`test-it`'s `-Djava.io.tmpdir=/tmp` makes the `SharedVolumes.Images` bind-mount a phantom empty dir), not the migration. With `java.io.tmpdir` pointed under `$HOME` (temporary local BUILD edit, reverted), `//modules/test-it:test` + `test_gravsearch_span` pass 2/2 against the migrated SIPI (worktree HEAD `ae9a9297`, linux-arm64 image, dsp-api scripts overlaid) — run twice: against dsp-api main AND against the dsp-api cleanup branch (#4263), both green. The CI leg is no longer needed as a gate
- [x] Benchmark old-vs-new per-request Lua overhead (same machine, same method as `just bench-compare` discipline); publish numbers in the PR. Measured pre-deletion on the same Mac (dsp-api 9-file closure, per-request VM build + init): C++ LuaServer 739 µs vs Rust runtime 147 µs (~5× faster via the bytecode cache). Instruction-hook tax on pure compute ≈ 1.6× (period-independent, LUA_MASKCOUNT); Rust without the hook ≈ C++ (772 vs 779 µs)
- [x] DEV-6077 closure: decide pooling (thread-local VM + fresh env) strictly benchmark-gated; file follow-up only if the bytecode cache is insufficient. DECISION: no pooling — the bytecode cache already puts the Rust runtime ~5× under the C++ baseline (147 µs vs 739 µs per request), so per-request isolation stays; no follow-up filed

## Acceptance Criteria

- [x] Full e2e suite green (24 Lua routes, upload, docroot, preflight, thread isolation, env_echo), with expectations updated only for the documented divergence table (30/30 e2e targets)
- [x] DEV-5925: scripts cannot reach `io`, `debug`, `os` beyond the shim, `package.loadlib`, or out-of-script-dir `require` (e2e-proven)
- [x] DEV-6070: memory bomb and infinite loop are both killed within configured budgets (e2e-proven). Precision on `pcall`: the deadline is untrappable (re-arming hook); a memory-cap error is trappable stock-Lua `LUA_ERRMEM`, but trapping never lifts the cap (e2e-proven by `memory_cap_survives_a_trapping_pcall`)
- [x] DEV-6077: per-request Lua overhead reduced vs baseline, measured and published (739 µs → 147 µs, ~5×); per-request `_G` isolation preserved
- [x] DEV-6119: multiple cookies, original-case names (e2e-proven)
- [x] `rg -i "luaL_|lua_State|LuaServer" src/` finds nothing — zero C++ Lua usage; the only Lua linkage left is mlua's `external` binding against `@lua`; C++ Lua files deleted
- [x] Green on all three platforms + RBE cross-compile; `just bazel-rustfmt-check` + `just bazel-clippy-check` clean (CI run 32437053817 on `ae9a9297`, incl. the sanitizer legs after the ASan-deadline test-env fix)
- [x] ADR-0023 merged; docs updated

## Dependencies & Risks

Depends on: `@lua` 5.3.6 BCR module (stays) and NativeLink RBE for the cross-compile spike legs. The former gates — dsp-api script audit, JWT policy, `/admin/files` latency check — are resolved (checked Phase 0 items).

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| mlua `external` under crate_universe has no public precedent | M | H | Phase 1 spike with named exit criteria; fallbacks: `build_script_env` `LUA_LIB` route, or (post-Phase-6 only) vendored `lua54` |
| `luaconf.h`/ABI mismatch between `@lua` build flags and mlua's `lua53` expectations | M | H | Explicit spike verification item; flags compared in writing |
| dsp-api production scripts use surface the audit missed | L | H | Audit done exhaustively (`02-audit-dsp-api-lua-surface.md`); residual risk covered by running dsp-api's integration suite before release |
| `server.header` casing or `_G` sharing regresses during the rewrite | L | H | Pinned as invariants with regression tests (audit D6/D9) |
| Fail-closed probe change breaks a legit no-preflight deployment | L | M | Distinguish hook-absent (allowed) from init-error (fatal); e2e for both |
| JWT `exp` validation breaks live DSP token flows | L | M | De-risked by audit D5 (dsp-api self-checks `exp`); golden-token tests; changelogged |
| Behavioral drift in formatting (`tostring`, json, error strings scripts match on) | M | M | e2e parity gate; error-message shapes preserved in the image ABI |
| `libsqlite3-sys` build script misbehaves under Bazel sandbox | M | L | Same-phase spike as mlua; last-resort C++ sqlite shim over FFI |
| Kill after response-head commit observed as clean truncation by clients | M | M | Abort (reset) the stream, never clean EOF; e2e asserts incomplete transfer |
| sole base62 port diverges (DSP IRIs) | L | H | Golden vectors captured from the C++ implementation before deletion |
| Hook overhead taxes the uncached IIIF hot path | M | M | Phase 2 microbench exit criterion; hook period tunable |
| One all-or-nothing PR: review size, long-lived branch drift | M | M | Phases as self-contained commits reviewed per phase on the branch; rebase regularly; PR-head CI plus the Phase 7 gates decide the merge |

## Success Metrics

- Sandbox: the DEV-5925/6070 attack scripts (shell exec, file read, mem bomb, infinite loop, pcall-wrapped loop) all fail safely, e2e-pinned.
- Performance: per-request Lua overhead (VM build + init) reduced vs the C++ baseline (bytecode cache removes the per-request ~150-line init re-parse); no regression on the IIIF preflight path beyond noise per the benchmarking discipline.
- Code: `src/scripting/` C++ Lua (~5000 lines incl. bindings) deleted; seam surface shrinks by the whole `SipiRequestContext` + preflight + lua-route entry set; one Lua runtime in the codebase.

## References

- Investigation (2026-08-20, this project): Lua surface map, stdlib audit, mlua/Bazel research — Linear project [Sipi Lua runtime hardening](https://linear.app/dasch/project/sipi-lua-runtime-hardening-b21226273cbc); issues DEV-5925, DEV-6070, DEV-6077, DEV-6119, DEV-7007, DEV-7013 (umbrella)
- dsp-api production Lua audit (Phase 0 gate): `02-audit-dsp-api-lua-surface.md` (same folder)
- Blocker record + D+ definition: `specs/2026-06-19-sipi-rust-strangler/01-feat-sipi-rust-strangler-plan.md:131,203,658`
- Current implementation: `src/scripting/LuaServer.cpp` (VM + `createGlobals` 2468-2801), `src/ffi/lua_config.cpp` (VM factory), `src/ffi/preflight.cpp`, `src/ffi/run_lua_route.cpp`, `src/ffi/SipiLua.cpp` (image bindings), `src/scripting/LuaSqlite.cpp`, `src/server-rs/src/routes.rs` (149-152 probes, 848-1090 lua routes), `src/server-rs/src/ffi.rs` (seam mirror; `SipiRequestContext` owner 1286-1434)
- Handle-ABI model: `sipi_make_request_context` family, `src/ffi/sipi_ffi.cpp:393-492`
- ADRs: `docs/adr/0017-extensibility-lua-and-rust.md` (contracts kept; §mlua consequence superseded by ADR-0023), `docs/adr/0010` (Essentials), `docs/adr/0021` (seam flattening)
- mlua: https://github.com/mlua-rs/mlua (0.12; `external` link mode, `StdLib`, `set_memory_limit`, `set_hook`); Bazel-side example of mlua-under-crate_universe: https://github.com/aduermael/herm (Luau variant)
- Learnings: `learnings/best-practices/sipi-nix-to-bazel-migration-lessons.md` (lesson 5: `-sys` build scripts vs sandbox), `learnings/integration-issues/pyo3-rust-python-shadow-execution-parity.md` (migration parity gaps: error hierarchy, normalization drift)

## Phase outcomes

### Phase 6 (2026-08-21)

C++ Lua fully deleted: `LuaServer`, `LuaSqlite`, `SipiLua`, `lua_config`,
`request_context.h`, the C++ Lua benchmark, the now-orphaned `src/jwt`
package, and the `@sole` pin (base62 ported to Rust in Phase 3). Transitional
`hostname`/`sslport` seam fields reverted (SipiServerConfig back to 240 bytes;
the Rust-side `ServerOverrides` fields stay — they feed the Lua `config`
table). `@lua` is now consumed only by mlua's `external` link in
`//src/scripting/rust`. Acceptance grep clean. Docs pass complete: ARCH-MAP
(scripting Rust-only, jwt entry removed, ffi entry corrected), CONVENTIONS,
CLAUDE.md, UBIQUITOUS_LANGUAGE (incl. the DUNE-007 upload/Essentials
exception), ADR-0023 completeness, and a full rewrite-verify of
`docs/src/lua/{index,lua-image,sqlite}.md` against the binding sources
(sandbox/limits section added; dropped bindings removed; `server.http`/JWT/
cookie semantics corrected). All gates green (unit, crate, e2e 28/28,
approval, rustfmt, clippy).

**Deferred to Phase 7:** none.

### Phase 7 (2026-08-21)

Hardening verification complete. `lua_hardening.rs` (13 e2e tests) proves the
sandbox, kills (incl. post-commit abort, trapped-pcall timeout and memory
variants, slow-reader thread-pinning bound), fail-closed startup, post-boot-edit
500, and killed-preflight-never-cached. `dsp_api_closure.rs` (8 e2e tests) runs
dsp-api's live 9-file closure verbatim against a canned `/admin/files` mock.
VM-build/script-duration histograms landed (`DurationRecorder` hook; the crate
stays OTel-free). Adversarial review workflow (5 reviewers, 2 skeptics per
finding): 13 confirmed findings, all fixed — 2 Critical code defects
(post-commit script error read as clean EOF → now aborts; unbounded
body-channel `blocking_send` let a slow reader pin a worker forever → now
deadline-bounded), 1 Critical stale doc (CONTEXT.md), the `validate_aud` and
`initscript "."` bugs (folded into their introducing commits), doc/comment
staleness, and 3 test gaps (cookie render, HTTP_BODY_CAP, http deadline
clamp). Benchmarks published in PR #789. DEV-6077: no pooling (bytecode cache
is ~5× under baseline). dsp-api integration suite: local run env-blocked
(identical 18 failures with the upstream image), deferred to CI.

**Deferred:** none remaining — both closed 2026-08-21: PR #789 CI fully green
on the final push (8/8 checks), and the dsp-api integration suite passed
locally 2×2 against the migrated SIPI (main + cleanup branch #4263) once the
colima `/tmp`-mount env blocker was root-caused (see the Phase 7 checklist
item). Plan complete; merge order: sipi #789, then dsp-api #4263.
