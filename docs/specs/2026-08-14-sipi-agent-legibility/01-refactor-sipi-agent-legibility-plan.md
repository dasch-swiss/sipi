---
title: "refactor: SIPI agent-legibility remediation (dune audit 2026-08-14)"
type: refactor
date: 2026-08-14
author: "Ivan Subotic"
status: reviewed
repository: dasch-swiss/sipi
linear: DEV-6968
---

# refactor: SIPI agent-legibility remediation (dune audit 2026-08-14)

> **Execution status (2026-08-15, ALL 15 PHASES SHIPPED).** Branch `worktree-dune`,
> sipi **PR #781** (draft). Phases 1–7 shipped in prior sessions (`f7053a34`→
> `4078d3c2`, pushed); Phases 8–15 shipped this session in 10 commits `bae89911`→
> `f92fa8e4` (local, not yet pushed): 8 carve, 9 mime, 10 banners, 11 sweep+gate
> (2 commits), 12 vocabulary, 13 operator-docs, 14 handlers-fold, a `sipi.cpp`
> oracle-scrub cleanup, 15 ARCH-MAP bootstrap. Final gate all green: rustfmt +
> clippy + `oracle-comment-check` + `bazel build //src/... //test/...` (142
> targets) + 13/13 unit + approval + 28/28 e2e (1 platform-skip). The net from
> Phase 7 on is **e2e + approval + proptest + unit** (no differential gate).
> Linear umbrella **DEV-6968**; Phase-7 child **DEV-6969** closes on merge; Rust-
> fuzz follow-up **DEV-6970** filed. Per-phase detail is in the `[x]` checkboxes.
> The Phase 7 DONE note (below) and the "Phase 7 execution map" appendix are now
> historical.
>
> **Scope additions this session (vs. the checklist), all maintainer-aligned:**
> Phase 8 broke the SipiImage↔handler cycle by maintainer choice (moved the `io`
> registry into //src/formats) rather than documenting-and-keeping; Phase 11 grew
> to strip 28 dangling deleted-C++-file citations (clean-all) and its help-text
> edits forced regenerating `cli__sipi-server-help.snap` (folded back into the
> Phase 11 sweep commit so every commit stays green); Phase 12 dropped
> `backpressure` repo-wide from src/server-rs (acceptance criterion), splitting
> load-shed sites → Throttling from genuine flow-control sites → "stalls/blocks";
> Phase 14 also removed the now-dead `parse_iiif_uri` classifier from the
> production binary (made it `testonly`); a stray `sipi.cpp` "differential-test
> oracle" label (a Phase-7 scrub miss) was fixed.
>
> Session-recorded scope changes vs. the original plan: Phase 2 grew to a full
> glossary reconciliation (maintainer-approved); Phase 3 grew (3 shared helpers
> moved with `sipi_init`, forced by the build graph); Phase 4 shipped as one
> commit not per-subpackage (interleaving rewrites); Phase 5's `layered_over`/
> `Config::base` need no destructure (exhaustive literals already guard them);
> Phase 6 could enable `layering_check` nowhere new (vendored deps emit no module
> maps — recorded on DEV-6353, contradicting its "resolved" note).
>
> **Phase 7 DONE (2026-08-14, one commit per maintainer choice), all gates green
> (C++ + Rust builds, rustfmt, clippy, 25/25 unit, 28/28 e2e).** Fuzz decision:
> **retire `//fuzz/handlers`** (it fuzzed oracle-only C++ `parse_iiif_uri`;
> production parses in Rust `iiif.rs`) — Rust cargo-fuzz harness against
> `iiif.rs::parse_request` filed as follow-up. Scope additions beyond the
> checklist: (a) the 241-file fuzz corpus was a shared fixture for the KEPT unit
> test `test/unit/handlers/parse_iiif_uri_corpus_test` — relocated (git-mv) to
> `test/unit/handlers/corpus/`, not deleted; (b) `tools/fuzz/` platform defs
> deleted (orphaned) + MODULE.bazel comment; (c) `just run`/`valgrind`/
> `bazel-build-tracy` repointed from the deleted C++ `//src/cli:sipi` server to
> the Rust `//src/cli-rs:sipi`; (d) metrics rewrite kept the label-fanned
> read_shape/essentials counters as engine-internal atomics, dropped only the two
> histograms + build_info (oracle-display-only); (e) `src/handlers/` kept for
> Phase 14 (only its dead `shttps/transport` include removed); (f)
> `UBIQUITOUS_LANGUAGE.md` oracle glossary entries deleted/rewritten (C++ route
> handler, Connection metrics adapter, Server context deleted; Server + Server
> mode rewritten to the Rust shell). **Deferred to Phase 11:** the oracle
> references in `src/cli-rs` help doc-comments (surface in
> `cli__sipi-server-help.snap`) and `src/server-rs/*.rs` comments — they are in
> Phase 11's `.rs` grep scope + gate; the snapshot stays valid until the Rust
> help text changes there.

## Overview

Implement the 14 findings of the 2026-08-14 `/dune:review` architecture audit of
the sipi repository (DUNE-001 … DUNE-014), and — since the maintainer decided
2026-08-14 to remove the shttps oracle soonish — fold the oracle removal into
this plan as Phase 7, prepared by the shttps decomposition (Phase 4). The audit
found one critical issue (the Rust↔C++ FFI seam fails silently on drift), eight
high (stale agent-context docs, enforcement gaps in the Bazel graph, shared-root
fan-outs for formats and config, production entry points inside the
oracle-labeled component), four medium, and one low.

**Ordering note.** The audit's suggested order placed the ARCH-MAP bootstrap
(DUNE-001) second. Specification review found that ordering hazard: every later
phase changes the module table, boundary rules, or paths the map would snapshot,
so a map written early is stale the day it lands — the exact failure mode
DUNE-009 diagnosed. The map bootstrap is therefore the **closing** phase, and the
doc-staleness sweep (DUNE-009 + DUNE-008) moves up so no later phase builds on
known-wrong docs. All other findings keep the audit's relative order.

## Problem Statement / Motivation

The audit's core diagnosis: sipi's *declared* architecture is good (clean
SIPI→shttps direction, disciplined single-writer Rust state, `src/metadata/` as
a model module), but enforcement and the agent-context layer drifted under the
strangler migration:

- The production FFI seam is hand-mirrored with nothing failing mechanically on
  drift; the hot-path structs (`SipiServeRequest`, `SipiIiifParams`,
  `SipiResponse`) and all 5 mirrored enums have no guard on either side
  (silent UB on drift).
- The first files an agent reads misdirect: `CLAUDE.md` points at directories
  that don't exist, `UBIQUITOUS_LANGUAGE.md` names ~10 symbols/paths that were
  never built, `CONVENTIONS.md`'s config recipe names a nonexistent file and
  omits the entire Rust half.
- Declared `structure`-level rules actually hold at `review`/`docs-only`:
  Bazel visibility constrains nothing (everything is `//src:__subpackages__` or
  public), `:sipi_lib` globs all of `src/**`, and all unit tests link the
  monolith.
- Shared-root fan-outs violate "new work adds isolated files": a new image
  format is 13–18 shared edits, a new config option 9–11 files with two seams
  that drop a forgotten field silently.

Full findings with evidence: see the DUNE-001…014 report in the conversation of
2026-08-14 (summarized per phase below; each phase names its finding).

## Proposed Solution

Fifteen phases, one concern per phase, delivered as **one PR** on a single
branch: each phase becomes one or more separate, self-contained commits
(settled 2026-08-14). This is the deliberate multi-commit case the repo
convention allows — rebase-merge puts every branch commit on `main` verbatim,
so each phase-commit is individually revertable and carries its own
conventional-commit type + scope. Phases execute strictly in order on the
branch. Mechanical seam
guards first (the only critical), then doc truth, then the structural
preparations (sipi_init move, shttps decomposition, config seam, Bazel quick
wins), then **oracle removal as Phase 7** — after which Phases 8–15 operate on
the smaller, oracle-free tree (the Bazel carving loses its oracle-churn
constraints; the mime dedup, comment sweep, and vocabulary phases shrink to
their production-only remainders). The ARCH-MAP bootstrap closes the plan and
snapshots the final state.

## Alternative Approaches Considered

Four decision points; each records the maintainer-approved resolution baked
into the phases below. Reversing any of them is a plan edit, not an
implementation-time choice.

1. **DUNE-007 seam mismatch — extract now (revised 2026-08-14; supersedes
   "ratify and wait").** Original recommendation was to ratify the seven-type
   seam and re-home at cutover, because `shttps::LuaServer` consumes
   `RequestContext`. With oracle removal now Phase 7, the calculus flips:
   extract **every cutover survivor** out of `src/shttps/` first
   (migrate-then-delete), so Phase 7 is a pure deletion of
   `shttps/transport/` + `SipiHttpServer.*` + the differential harness.
   Survivors, by evidence: `shttps/lua/` (LuaServer + `request_context.h`;
   production users `src/ffi/{lua_config.h,SipiLua.h,preflight.cpp,run_lua_route.cpp}`
   + `src/cli/cli_app.cpp`), `shttps/jwt/` (via LuaServer's JWT Lua functions),
   `shttps/util/` (the documented pending modularization), `shttps/lua_sqlite/`
   (registered on the production Lua path). Dies with the oracle:
   `shttps/transport/` (which depends on `lua/` — the interim reverse edge is
   visibility-allowlisted like `//src/logging` and dies in Phase 7), plus
   `certificate/`, `docroot/`, `scripts/`, `shttps.config.lua`.
   Survivor homes (settled 2026-08-14): `src/scripting/` (LuaServer +
   `request_context.h` + `lua_sqlite`), `src/util/`, `src/jwt/`; new
   `scripting` commit scope; `lua` scope keeps meaning scripts/config.
2. **DUNE-005 format fan-out — descriptor table now vs defer.** *Settled:
   defer the descriptor registration table until a real fifth format arrives*
   (repo scope discipline: abstractions need a second real caller). Do collapse
   the duplicated mime tables (Phase 9, post-removal, so the collapse is total —
   the oracle's copies are already deleted), and document the fan-out in the
   ARCH-MAP formats entry. This deliberately softens the audit's `structure`
   target to `docs-only` for the registration point.
3. **DUNE-014 handlers rename — include `fuzz/handlers/` or not.**
   *Settled: rename both.* Leaving `//fuzz/handlers` referencing a renamed
   source package is name drift, and the fan-out (justfile, `.bazelrc`,
   `fuzz.yml`, CLAUDE.md quick reference, corpus dir) is mechanical.
4. **DUNE-002 cli-rs extern block — move vs keep.** *Settled: keep.* The
   `extern "C"` block in `src/cli-rs/src/main.rs:114` is deliberately colocated
   inside `#[cfg(feature = "mimalloc")] mod allocator` (drops out with the
   feature; SIGSEGV history warns against re-declaring mimalloc's stats API
   elsewhere). Document the colocation instead of moving it.

Further settled 2026-08-14 (formerly open questions): the differential parity
gate retires **without replacement** in Phase 7 (e2e + approval + proptest are
the net; recorded in the removal ADR); the Prometheus endgame (drop
`GET /metrics` + prometheus-cpp, shrink the `Metrics` singleton) is executed in
Phase 7 so the Phase 8 carving never cements it; oracle removal is **Phase 7 of
this plan**, not a separate plan; the Linear umbrella is **DEV-6968**.

## Technical Considerations

- **No behavior changes to the IIIF pipeline.** Approval goldens must not
  change in any phase; banners, asserts, comments, and doc edits are
  byte-neutral. Phase 12's rename must be Rust-identifier-only (verify no JSON
  wire field is touched).
- **Guard asymmetry is accepted:** C++ `static_assert`s gate every build
  including linux-aarch64 cross-compile; Rust layout asserts are test-time on
  host platforms (`//src/server-rs:sipi_unit_test` runs inside the `//src/...`
  wildcard of `bazel-test`/`bazel-coverage`).
- **Oracle discipline (Phases 1–6):** the oracle's *behavior* is never edited
  before its removal; the differential gate is the referee. Mechanical
  include-path churn in oracle files is expected in Phases 3 and 4 (the
  `sipi_init` move and the shttps decomposition) and verified by a local
  `just bazel-test-differential` run. From Phase 8 on there is no oracle.
- **Lint gates:** every Rust-touching phase runs `just bazel-rustfmt-check` +
  `just bazel-clippy-check` before commit (CI gates not covered by tests).
  Prefer `field: _` bindings in destructuring to avoid clippy findings.
- **Every new CI gate gets a local `just` recipe invoked verbatim by CI**
  (repo pattern, cf. `just commit-lint`).
- **Commit scopes** per `CONVENTIONS.md` module table; scope = concern (e.g.
  the FFI-assert phase is `test(ffi)`/`refactor(ffi)`, the comment gate is
  `chore(ci)` + `refactor(server-rs)`). Phase 4 adds the `scripting` scope.
- **Institutional learnings applied:** deferred work always lands in the
  receiving phase's checklist, never prose only
  (`learnings/best-practices/deferred-work-lost-across-phase-handoffs.md`);
  gates without a `--check` mode use the copy-diff pattern
  (`learnings/best-practices/maudfmt-adoption-no-check-mode-and-clippy-gotchas.md`);
  Bazel carving respects the `--per_file_copt` path-regex footgun and
  `copy_to_directory` fixture rule
  (`learnings/best-practices/sipi-nix-to-bazel-migration-lessons.md`).

## Implementation Phases

#### Phase 1: FFI drift guards (DUNE-002, critical)

Make seam drift fail mechanically. Inventory-driven, not list-driven: the
deliverable covers **every** `#[repr(C)]` mirror in `src/server-rs/src/ffi.rs`,
`src/server-rs/src/config.rs`, and `src/cli-rs/src/ffi.rs` (16 items today; 3
guarded: `SipiServeTimings`, `SipiImageErrorReport`, `SipiServerConfig`).

- [x] Inventory all `#[repr(C)]` structs and mirrored enums across the three Rust mirror files; record the list in the PR description
- [x] Add paired size + `offset_of!` layout asserts (Rust test side) for every currently unguarded `repr(C)` struct, including `SipiResponse`, `SipiIiifParams`, `SipiServeRequest`, `SipiImageDims`, `SipiStrPair`
- [x] Add matching C++ `static_assert`s in `src/ffi/sipi_ffi.h` (and `metrics_snapshot.h` where the struct lives there) for the same structs
- [x] Add value asserts for every mirrored enum constant on both sides (C++ `static_assert(SIPI_SIZE_X == static_cast<int>(...))`; Rust const asserts), covering `SipiRegionType`, `SipiSizeType`, `SipiQualityType`, `SipiFormatType`, `SipiPermType`
- [x] Verify `//src/server-rs:sipi_unit_test` (layout tests) runs on the CI test legs via `just bazel-test`/`bazel-coverage`; if any leg skips it, fix the wildcard
- [x] Add a SAFETY-style comment at `src/cli-rs/src/main.rs:114` documenting why the allocator `extern "C"` block is deliberately colocated with the `mimalloc` feature gate (decision 4)
- [x] Declare or document the two out-of-header C surfaces (`mi_stats_shim.c` / `sipi_mi_stats_read`, the `dlsym`'d `mallinfo2`) at their point of use
- [x] Negative check before finalizing the phase's commit: perturb one struct's field order locally and confirm the build/test fails on both sides; revert

#### Phase 2: Agent-context doc truth sweep (DUNE-009 + DUNE-008)

Policy for glossary entries naming nonexistent code: **delete unless an ADR or
open Linear issue commits to the target shape**; committed target shapes move
to an explicitly marked "Target shape (not yet built)" section.

- [x] Fix `CLAUDE.md` component table: `include/iiifparser/` → `src/iiifparser/`, `include/formats/` → `src/formats/`, `src/SipiImage.hpp` → `src/SipiImage.h`, `src/metadata/Icc.cpp` → actual filename (verify case on disk; feeds Phase 10's banner references)
- [x] Fix `CONVENTIONS.md:188` `SipiHttpServer.hpp` → `src/SipiHttpServer.h` (also fixed the same stale `include/` paths in the canonical module/scope table, which Phase 15 lifts)
- [x] Fix `CONTEXT.md:19`: replace the retired "Backpressure" umbrella with Throttling and drop the removed rate limiter (ADR-0008 superseded, sipi#777)
- [x] Apply the delete-or-target-shape policy to every `UBIQUITOUS_LANGUAGE.md` entry naming nonexistent code: `BlockedScope`, `throttling/`, `route_handlers/` + `register_routes`, `src/server/` composition claim, `permission/` + `AllowPermission`, `lua_bindings/` + `LuaContext`, `InputSource`/`RangeSource`, `apply_watermark`, `to_server_config`, `kSipiEssentialsUuid` → `sipi_essentials_uuid` (SCOPE GREW: the whole glossary was aspirational, not ~10 entries — full-policy sweep approved by maintainer 2026-08-14; superseded-ADR-0001 targets dropped not deferred; new "Target shape (not yet built)" section holds only the two current-ADR-backed targets, ADR-0007 + ADR-0006/0004)
- [x] Correct the `Sipi::Server` entry: `SipiHttpServer` **inherits** `shttps::Server` (`src/SipiHttpServer.h:37`)
- [x] Fix the propagated wrong symbol in `test/unit/sipiimage/essentials_prefix_invariant_test.cpp:37` (`kSipiEssentialsUuid` → `sipi_essentials_uuid`)
- [x] Update the stale body of `docs/adr/0008-rate-limit-post-cache.md` (superseded banner exists; body still teaches three sub-policies and a nonexistent path) — kept the historical decision record intact, added a "historical reading" note flagging the two obsolete claims (ADR-immutability preserved)

#### Phase 3: Production entries out of the oracle label (DUNE-003)

- [x] BUILD-graph analysis first: identify which helpers `sipi_init` shares with the oracle server mode in `cli_app.cpp` and decide extraction shape (shared helper vs move); record in the PR description (shared: `LibraryInitialiser`, `detect_available_memory`, `sipiConfGlobals`; forced to move to `src/ffi` — keeping them in `src/cli` would cycle since cli already deps ffi; homed by concern per maintainer choice)
- [x] Move `sipi_init` out of `src/cli/cli_app.cpp` into `src/ffi/` (`src/ffi/init.cpp`; shared helpers → `src/ffi/startup.{h,cpp}` + `sipiConfGlobals` → `ffi/lua_config.{h,cpp}`; cli_app shrank ~490 lines)
- [x] Correct `CONVENTIONS.md:12` to CLAUDE.md's precise claim: "src/shttps + src/cli **server mode** is oracle-only"
- [x] Delete-and-replace the wrong comment at `src/ffi/engine_context.cpp:13-14` ("currently by SipiHttpServer; later by sipi_init")
- [x] Document the two-writer situation (production `sipi_init` + oracle `SipiHttpServer`, until Phase 7) at the single sink `set_engine_context` in `engine_context.cpp` — not at the callers
- [x] Run `just bazel-test-differential` locally before finalizing the phase's commit (the gate is a manual-tagged CI-only leg; the local run is the verification) — PASSED (119s); C++ + Rust builds green; all 5 ffi unit tests green

#### Phase 4: shttps decomposition — extract the cutover survivors (DUNE-007, revised)

Migrate-then-delete preparation for Phase 7. After this phase, no production
file includes anything under `src/shttps/`; Phase 7 is a deletion-only change.
One commit per moved subpackage.

- [x] Confirm the keep/delete classification with fresh evidence in the PR description (keep: `lua`, `jwt`, `util`, `lua_sqlite`; delete-in-Phase-7: `transport`, `certificate`, `docroot`, `scripts`, `shttps.config.lua`) — mapped via subagent: intra-four graph util/jwt leaves → lua deps both → lua_sqlite deps lua; only transport reverse-edges to lua+util (not jwt/lua_sqlite)
- [x] Move `shttps/lua/` (LuaServer, `request_context.h`) to `src/scripting/` as a `cc_library` with narrowed visibility and colocated tests (homes settled 2026-08-14; add the `scripting` commit scope to the scope vocabulary; `lua` scope stays for scripts/config) — added scripting/util/jwt scopes
- [x] Move `shttps/util/` (Hash, Parsing, Error, Global) to `src/util/` — completes `CONTEXT.md`'s "pending modularization"
- [x] Move `shttps/jwt/` to `src/jwt/`
- [x] Move `shttps/lua_sqlite/` into `src/scripting/` (sqlite Lua bindings) — `:lua_sqlite` target in the scripting package
- [x] Visibility-allowlist the interim reverse edges (`shttps/transport` → moved modules) on the `//src/logging` precedent, each grant commented as dying in Phase 7
- [x] Update ADR-0013 (amend in place; note the date): the four-type seam story is superseded by the decomposition — production reaches scripting/util/jwt directly; only the oracle transport remains inside `shttps/`
- [x] Update `CONTEXT.md`'s internal-module section and delete the now-completed "Pending modularization" paragraph (replaced with an "Extracted domain modules" done-state note)
- [x] Confirm `include/SipiConf.h` uses no `shttps::` transport type in its body, then delete the unused `#include "shttps/transport/Connection.h"` at line 13
- [x] Mechanical include-path updates in oracle files are in scope; behavioral edits are not — run `just bazel-test-differential` locally after each moved subpackage's commit (CONSOLIDATED to ONE Phase-4 commit + one differential run: the include/label rewrites interleave across shared files, so per-subpackage staging would be artificial; git records renames so each package stays traceable; differential PASSED 118s, full build of 106 src+unit targets + server shell green)
- [x] File the Linear child issue of DEV-6968 tracking Phase 7 (oracle removal), listing this phase as its prerequisite and the deletion inventory (transport, `SipiHttpServer.*`, cli_app server mode, differential harness + corpus guard, CI legs that exist only for the oracle) — **DEV-6969** filed

#### Phase 5: Config seam fails on omission (DUNE-006)

The mechanism must cover all four drop sites, recursing into the 8 flattened
clap sub-structs.

- [x] Exhaustively destructure in `From<&ServerArgs>` (`src/cli-rs/src/commands/server/mod.rs:19`), recursing into every `#[command(flatten)]` group (`args/{network,paths,cache,limits,tls_auth,knora,logging,concurrency}.rs`) with explicit `field: _` bindings for deliberately dropped flags (sslport/sslcert/sslkey, keepalive, hostname, logfile)
- [x] Exhaustively destructure both `self` and `base` in `ServerOverrides::layered_over` (`src/server-rs/src/config.rs:108`) — REVISED: not needed. `layered_over`'s output type IS `ServerOverrides`, so its exhaustive struct literal already forces every field to be merged (a new field fails to compile there). A separate self/base destructure would be belt-and-suspenders over the proven exhaustive-literal pattern; documented the property on the fn instead. Same applies to `Config::base` (the 4th touch point, TOML→overrides).
- [x] Add source-side destructuring of `ServerOverrides` in `OverridesHolder::new` (`config.rs:254`) so a field that is never read fails to compile (cloned bindings; unused → `-D warnings`)
- [x] Add a per-field-group precedence unit test for `layered_over` (destructuring cannot catch wrong-precedence merges)
- [x] Rewrite the `CONVENTIONS.md` config recipe with the honest full path: the 5 C++ places (with `include/SipiConf.h`, not the wrong filename) + `src/ffi/sipi_ffi.h` `SipiServerConfig` struct + the Rust half (config.rs 4 sites, config_file.rs, clap arg with colocated `env =`, mod.rs wiring), and name the C++ `sipi_init` apply block as the one unmechanized link
- [x] Negative check: add a dummy field to one clap group locally and confirm compilation fails at the `From` impl; revert — confirmed `error[E0027]: pattern does not mention field 'dummy_probe'`

#### Phase 6: Bazel enforcement — quick wins (DUNE-004, part 1)

- [x] Delete the stale `//src/shttps:__pkg__` visibility grant + wrong comment in `src/observability/BUILD.bazel:24-27` (confirmed stale: transport uses the ConnectionMetricsAdapter IoC bridge + `<tracy/Tracy.hpp>` directly, no observability include)
- [x] Fix the wrong layering_check-deferral rationale at `src/shttps/BUILD.bazel:31-35` (per-target `features = [...]` applies only to the target's own compiles; `//src/logging` proves it) — corrected to the REAL blocker (vendored native deps emit no module maps), evidenced below
- [x] Timeboxed: enable per-target `layering_check` on packages where it passes today under the hermetic LLVM toolchain; if module maps for vendored native deps block everything, record that in the DEV-6353 issue and stop — STOPPED: `//src/util` fails on `@libmagic` `magic.h` despite the declared dep (contradicts DEV-6353's 2026-06-19 "modulemap blocker resolved" claim); `//src/observability` fails on an oracle-coupled `shttps/transport` edge; only `//src/logging` (no vendored includes) passes. Recorded the evidence + the stale-carve-names note as a DEV-6353 comment; enabled nothing new.

#### Phase 7: Oracle removal

**NOT deletion-only** — a full subagent inventory (2026-08-14; reproduced in the
"Phase 7 execution map" appendix at the end of this file) surfaced four
BUILD-graph blockers and a real metrics-refactor obligation the plan under-scoped.
Tracked by Linear child **DEV-6969**. **Resume Phase 7 from the appendix map** —
it has every file, line number, and label. Order below is the recommended
execution sequence.

- [x] Pre-removal verification — CLEARED by maintainer 2026-08-14: "nothing to check with ops; the Rust shell has been in production for weeks." No ops-deploy/monitoring reference to the C++ server remains. Go.
- [x] **Blocker fix first (or the build breaks on deletion):** rewire the stale/misrouted BUILD deps that point at `//src/shttps:shttps` but only use symbols that now live in `//src/util` — `src/metadata/BUILD.bazel:90` (`:metadata`) and `src/metadata/internal/BUILD.bazel:49` (`:protobuf_codec`) → change `//src/shttps:shttps` to `//src/util`; verify `src/BUILD.bazel:219` (`:sipi_top`, SipiError) similarly, and drop the dead `//src/shttps:shttps` / `@lua` / `@sqlite3` deps from `:sipi_lib` (`src/BUILD.bazel:445`) once `SipiHttpServer.cpp` is gone (Flag 1 + Flag 3) — DONE: retargeted `:sipi_top`/metadata/metadata-internal to `//src/util`; `:sipi_lib` swapped `//src/shttps:shttps`→`//src/scripting:scripting` (SipiConf.cpp needs `shttps::LuaServer`) and dropped `@sqlite3`; also removed the now-dead `#include "shttps/transport/Connection.h"` from `src/handlers/iiif_handler.cpp` (urldecode already comes from `util/UrlDecode.h`)
- [x] **fuzz harness decision (Flag 4 — hard build break):** `fuzz/handlers/BUILD.bazel:50` deps `//src/shttps:fuzz_subset`, which compiles oracle transport sources (`ChunkReader.cpp`/`Connection.cpp`/`SockStream.cpp`) + `iiif_handler.cpp`; `.bazelrc:297` documents it as the fuzz binary's whole graph. Deleting `src/shttps/` breaks it. **DECIDED (maintainer, 2026-08-14): retire `//fuzz/handlers` in Phase 7.** Evidence (subagent trace): the harness fuzzes ONLY the C++ classifier `handlers::iiif_handler::parse_iiif_uri`, which is oracle-only (`SipiHttpServer.cpp:1337`) and never on the production path — production parses IIIF URIs entirely in Rust (`src/server-rs/src/iiif.rs::parse_request`, `routes.rs:305`), handing a pre-parsed `SipiIiifParams` across the seam. So the harness guards dead oracle code. Delete the target + all wiring (folds in Phase 14's fuzz-rename work) and **file a follow-up to build a cargo-fuzz/libFuzzer-on-Rust harness against `iiif.rs::parse_request`** (the real production parser) — filed as **DEV-6970** (child of DEV-6968).
- [x] Delete `src/shttps/` entirely (confirmed contents = `transport/` + `certificate/` + `docroot/` + `scripts/` + `shttps.config.lua` + README + BUILD.bazel; the BUILD docstring is stale re: the old five-sub-package layout — irrelevant, it's deleted)
- [x] Delete `src/SipiHttpServer.{h,cpp}` and the server-mode branch of `src/cli/cli_app.cpp` (per appendix §2 line ranges: `run_server` 607-1008, `attach_server_opts` 1101-1203, `cmd_server` 1206-1208, server-only option storage 268-307, `detect_available_cores` 80-123, the two oracle includes at 33 + 45). **Keep:** `sipi_cli_main`, all offline verbs, `LibraryInitialiser::instance()`
- [x] Delete `src/observability/connection_metrics_adapter.{h,cpp}` + `connection_metrics_adapter_test.cpp` and drop `//src/shttps:shttps` from `src/observability/BUILD.bazel:45` (Flag 2 — the `ConnectionMetricsAdapter`'s only caller is the dying `run_server`; it implements the transport's `ConnectionMetrics` interface which dies too)
- [x] Delete the differential harness: `test/e2e/tests/differential.rs`, the `differential` target + docstring in `test/e2e/BUILD.bazel:1-54,193-215`, `$SIPI_BIN_REF` plumbing (`test/e2e/src/lib.rs:46,149,302,724,730` + `sipi_e2e_test.bzl:89-96`), `just bazel-test-differential` + `just differential-coverage-check` (`justfile:235-258`), `tools/differential_coverage_check.sh`, and the two `ci.yml` steps (lines 132-134 drift guard + 211-221 parity gate)
- [x] Remove Phase 4's interim visibility allowlists / reverse-edge deps (`transport → //src/util`, `transport → //src/scripting:scripting`) — they die with `src/shttps/BUILD.bazel`
- [x] Retire the oracle-only metrics surface + convert the singleton (NOT trivial — prometheus-cpp is still live): delete `GET /metrics` + `metrics_handler` (`SipiHttpServer.cpp:1395-1401,1460`) and `Metrics::serialize()`; **rewrite `Sipi::observability::Metrics` from prometheus-cpp types to plain atomic counters/gauges, keeping the 20 fields the production FFI snapshot reads** (`sipi_metrics_snapshot`, `src/ffi/sipi_ffi.cpp:203-247` — full field map in appendix §3); update `metrics_registry_test.cpp` (currently calls `registry()->Collect()`); keep the `SipiMetricsSnapshot` 160-byte `static_assert`/`offset_of!` lock-step (`metrics_snapshot.h:79-99` ↔ `server-rs/src/ffi.rs`) valid; drop the prometheus-cpp `bazel_dep` + `single_version_override` + patch (`MODULE.bazel:44-68`, `bazel/patches/prometheus_cpp_load_cc.patch`) and the `@prometheus-cpp//core` deps (`src/BUILD.bazel:338,459`, `src/observability/BUILD.bazel:47`). The FFI snapshot bridge to OTLP is production and stays. **NOTE:** the maintainer believed prometheus was already removed weeks ago — that was the *production* path (OTLP); the oracle-side prometheus-cpp dep + singleton backing are still live and are removed here.
- [x] Write the removal ADR (supersedes the oracle-retention part of ADR-0013): oracle deleted; the differential parity gate retires **without replacement** — e2e + approval + proptest are the net. Add closing notes to ADR-0001 + ADR-0013 (keep as historical record, don't delete)
- [x] Repo-wide reference scrub (clean-all rule) — appendix §5 has every hit by file: **rewrite (not just delete)** the load-bearing explainers: `CLAUDE.md:110` (Production surface vs oracle para) + the component-table rows (118 SipiHttpServer, 121 SHTTPS, 123 GET /metrics reword, 150 prometheus-cpp), `CONVENTIONS.md` §Production-surface-vs-oracle + the route-registration worked examples (152,163,166,178 show `SipiHttpServer::run()`/`add_route()` as "how to add a route" — replace) + the `ConnectionMetrics` convention block (298-307) + §Prometheus (257-261,290-291), `CONTEXT.md:23-52` (whole "Internal module: shttps" section — rewrite), `REVIEW.md:5-7,44,62,65`, `justfile`, `.bazelrc:297`, `docs/src/development/{ci.md,building.md,testing-strategy.md,profiling.md}`. Also fix pre-existing drift found: `docs/src/development/ci.md:78-85` references a "shttps→sipi boundary check" that no longer runs
- [ ] Close DEV-6969 with the deletion inventory checked off

#### Phase 8: Bazel enforcement — per-module carving (DUNE-004, part 2)

One commit per module, `src/metadata` as the pattern; DEV-6353 alignment. Runs on
the oracle-free tree, so include churn is confined to live code; prefer
`strip_include_prefix`/exported-hdrs where it keeps diffs small.

- [x] Carve `src/iiifparser/` into its own `cc_library` package with narrowed visibility and colocated tests (prerequisite for Phase 14) — clean leaf (SipiError + util only); `strip_include_prefix="/src"`; `parse_benchmark` moved in (ADR-0003)
- [x] Carve `src/formats/` (fix the `../../src/SipiImage.h` relative-path escapes) — SCOPE: maintainer chose to break the SipiImage↔handler cycle (2026-08-14) rather than document-and-keep. Moved `SipiImage::io` registry out of the engine into `//src/formats:format_registry.cpp`; declared the one engine→codec callback (`Sipi::read_watermark`) engine-side (SipiImage.h); split `output_sink` into its own leaf target so engine's `SipiIO.h` reaches it without deping the handler package; `//src/formats` deps `//src:engine` one-way; decode/encode benchmarks moved in
- [x] Carve cache + memory-budget + image (or document why they stay in `:engine`) — DOCUMENTED in the `:engine` docstring: no internal cycle to cut, SipiImage is the hub every module points at; iiifparser + formats were the separable leaves and were carved
- [x] Point each carved module's `test/unit/<mod>` target at the narrow library instead of `//src:sipi_lib` — retargeted iiifparser→`//src/iiifparser`, decode_dims→`//src/iiifparser`, formats→`//src/formats:formats`, output_sink→`//src/formats:output_sink` (+ per-test visibility grants); sipiimage/commands tests keep `sipi_lib` but were repointed at the prefixed include forms
- [x] Fix the `../SipiError.h` relative includes in `src/iiifparser/*.cpp` during that module's carve

#### Phase 9: Mime-table dedup (DUNE-005, scoped per decision 2)

Post-removal the collapse is total — the oracle's copies in
`SipiHttpServer.cpp` were deleted in Phase 7.

- [x] Collapse the remaining C++ image-mime duplication to one table — SCOPED: `content_type_for` + `detect_in_format` (serve_image.cpp) were exact inverses each hard-coding the 4 format/mime pairs; both now source from a single `kFormatMimes` table (behaviour-neutral). `SipiImage::getFileType`'s read-time classifier kept its separate sniff-alias list (image/x-tiff, image/pjpeg) — unifying across the ffi/engine boundary is descriptor-table work, deferred per decision 2. (Parsing.cpp's extension→mime table can't home a FormatType mapping: util sits below iiifparser.)
- [x] Keep `src/server-rs/src/routes.rs` `IMAGE_MIMES` as the single Rust-side table; fix its drifted cross-reference comment — repointed from the deleted `SipiHttpServer.cpp:568-570,913-914` to the surviving `detect_in_format` table
- [x] Defer the format-descriptor registration table — already recorded: Phase 15 carries the "formats-entry fan-out documentation deferred from Phase 9" checkbox; decision 2 flags the ADR-0006 prior-art check before any future descriptor work

#### Phase 10: In-place invariant banners (DUNE-011)

`src/metadata/icc.h:112-129` is the model.

- [x] Add the byte-exact cross-arch golden banner (+ benchmark obligation pointer) to `src/formats/SipiIOTiff.cpp`, `SipiIOPng.cpp`, `SipiIOJ2k.cpp` (file-head comment naming the approval gate and `docs/adr/0002`) — comment-only; approval green
- [x] Add the FFI snapshot-bridge banner to `src/observability/metrics.h` (pointing at `metrics_registry_test.cpp`'s rule and `src/ffi/metrics_snapshot.h`'s "Inclusion rule" block) — metrics_registry_test tripwire green

#### Phase 11: Oracle-framing comment sweep + CI gate (DUNE-012)

Post-removal this shrinks: most `shttps`/oracle references died in Phase 7's
scrub; this phase sweeps the framing comments that remain in production Rust
and installs the gate that keeps them out. Sweep first, gate second, strictly
before Phase 12 (so the gate vets Phase 12's new comment text).
Rewrite-not-delete: comments carrying WHY rationale (e.g. last-write-wins
precedence) are restated in Rust-native terms.

- [x] Sweep the remaining oracle/transport/roadmap-framing comments from `src/server-rs/src/*.rs` and `src/cli-rs/src/**/*.rs`, including the strangler-fig module-doc taglines and the `DIFFERENTIAL-VERIFY:` markers — ~74 sites across 17 files reworded to Rust-native terms, preserving each WHY (last-write-wins, NUL-only guard, CORS 204 contract)
- [x] Reword or relocate parity framing inside `#[cfg(test)]` regions (`iiif.rs` unit tests, `src/server-rs/BUILD.bazel` docstring) so the gate can stay simple
- [x] Build the gate as a `just` recipe (`just oracle-comment-check`): case-insensitive grep over `src/server-rs/src` + `src/cli-rs/src` `.rs` files only for `oracle|shttps|cutover|parity|strangler|C\+\+ server` (pattern does not match "engine"); verified clean on the swept tree before wiring
- [x] Wire the recipe into `ci.yml` as a gate step (amd64 leg, beside rustfmt/clippy)
- [x] Fix the drifted line-number cross-references flagged by the audit (`routes.rs:3`, `routes.rs:33`) — SCOPE GREW (clean-all): stripped all 28 dangling citations to the deleted `SipiHttpServer.cpp`/`Connection.cpp`/`Server.cpp` across sink/path/routes/info.rs, not just the two audit-named sites

#### Phase 12: Vocabulary alignment in production code (DUNE-010)

- [x] Verify the `file_info_json` rename is Rust-identifier-only, then rename it — VERIFIED identifier-only (JSON wire fields `@context`/`internalMimeType`/`fileSize` and the `FILE_CONTEXT` URL value unchanged). `file_info_json` → `bitstream_info_json` (pairs with `image_info_json`); callers + tests updated. `file_context`/`FILE_CONTEXT` KEPT: they name the `/api/file/` context URL, where the glossary allows "file" to survive
- [x] Align `backpressure` → Throttling vocabulary — SCOPED per glossary: the load-shed/503 sites (routes.rs:45/211/522/2084, metrics.rs) → **Throttling**; the genuine byte-stream flow-control sites (sink.rs, routes.rs:363) reworded to "stalls/blocks" (dropping the word, since renaming them to Throttling would be wrong). `src/server-rs` now has zero "backpressure" (acceptance criterion). Also sipi.md:372, proptest_iiif_uri.rs:133, and e2e lib.rs (pipe-buffer occurrence, not the drifted :323)
- [x] Fix `src/iiifparser/SipiRegion.h:7` file-head comment; align `SipiDecodeDims.h` doc comments to Decode level / Region terms — kept `reduce`/`crop_coords`/`roi_dims` as codec-API param names (glossary allows)
- [x] Add "Preflight cache" (and its **Preflight cache key** = `(prefix, identifier, Cookie, Authorization)`) as a glossary entry in `UBIQUITOUS_LANGUAGE.md`
- [x] Rename the `canonical` variable used as the cache key in `src/ffi/serve_image.cpp` (local + async-struct field `canonical_`→`cache_key_`) and fix the term-fusing comment — kept `build_canonical_url`/`canonical_header`/`canonical_region` (they genuinely name the Canonical URL)
- [x] Add the glossary avoid-alias list to `docs/src/development/reviewer-guidelines.md` as a review checklist item (new "Ubiquitous Language" section)
- [x] Run the full e2e suite (`just bazel-test-e2e`) after the renames — 28/28 green (the `cli__sipi-server-help.snap` snapshot drift from the Phase 11 help-text edits was regenerated and folded back into the Phase 11 sweep commit so every commit stays green)

#### Phase 13: Operator-docs truth (DUNE-013)

- [x] Fix the four wrong pool-knob values in `docs/src/guide/sipi.md` and `docs/src/guide/running.md` — verified against `routes.rs`: max-waiting default = `2×nthreads` (was `0`/unlimited); `0` = shed immediately (not "unlimited depth"); Retry-After = 1 (was 5); queue-timeout = 5s (was 10). Fixed in the CLI-flag AND `SIPI_*` env-var tables of both files
- [x] Fix the `nthreads` auto claim — `default_pool_size()` = `std::thread::available_parallelism()` (container-aware), fallback 4 (was the wrong "cores - 1, minimum 2")
- [x] Document `SIPI_RS_PORT` in the env-var table as the highest-precedence (env-only) port override
- [x] State the port-precedence chain once, next to the code (`lib.rs::serve()` resolution comment is the single authority), and reduce the copies to pointers — trimmed the `DEFAULT_PORT` doc and `config.rs::ServerOverrides::serverport` doc; the `SIPI_RS_PORT`-ahead-of-`serverport` gotcha is preserved

#### Phase 14: handlers rename (DUNE-014; depends on Phase 8 iiifparser carve)

- [x] Fold `src/handlers/` (`parse_iiif_uri`) into `src/iiifparser/` — FOLDED (not renamed): git-mv'd the two files into the carved package as a NEW `testonly` `:iiif_handler` target. Since it's `testonly` + out of the `//src:sipi_lib` glob, this also drops the classifier (dead code — production parses in Rust `iiif.rs`) from the production binary; the 3 test targets (2 unit + 1 approval) dep the narrow target. Kept the `handlers::iiif_handler` C++ namespace (scope ≠ namespace; the `iiif.rs` cross-references stay accurate)
- [x] Rename `fuzz/handlers/` — MOOT: Phase 7 deleted the whole `fuzz/` tree with the oracle (decision 4). Verified no functional `fuzz/handlers` refs survive in justfile/.bazelrc/fuzz.yml; the remaining mentions are historical (ADR-0020, fuzzing.md, testing-strategy.md) correctly describing the retirement
- [x] Re-home the fuzz-subset build logic — MOOT (fuzz gone in Phase 7); verified no `iiif_handler_hdrs` / `fuzz_subset` export was recreated
- [x] Update the `CONVENTIONS.md` scope table and `commit-conventions.md` scope vocabulary — retired the `handlers` scope, folded its responsibility into the `iiifparser` row; noted the retirement in the commit-scope list

#### Phase 15: ARCH-MAP bootstrap (DUNE-001, closing phase)

- [x] Run `/dune:map` to bootstrap `ARCH-MAP.md` — via the `dune:map` skill (update/bootstrap mode); 15 code components lifted from the corrected CONVENTIONS.md table, fanned out 10 `Explore` (sonnet) subagents in 2 batches to answer the 8-item questionnaire, synthesized the entries. Completeness verified: all 183 `src/` files map to exactly one component
- [x] Record every boundary rule with its enforcement mechanism — every rule carries the dune enum (`structure` / `static-analysis` / `review` / `docs-only`) with the mechanism named in the why (Bazel visibility/dep-direction, the FFI `static_assert`↔`offset_of!` locks, `oracle-comment-check`, the approval gate, exhaustive-destructure); no unlabeled claims
- [x] Include the formats-entry fan-out documentation deferred from Phase 9 — `tools/formats-fanout.sh` (the mechanically-generated new-format edit-site list) committed next to the formats entry, which cites it
- [x] Include local-context kits for the audited components — every component entry carries a ≤7-file kit (metadata's ICC kit is the model)
- [x] Add the one-line pointer to `ARCH-MAP.md` in `CLAUDE.md` — added in the Domain Model section (pull-on-demand; never auto-loaded)

## Acceptance Criteria

- [ ] Every `#[repr(C)]` mirror and mirrored enum constant is guarded on both sides; a deliberately perturbed field order fails build/test (Phase 1 negative check performed)
- [ ] Every path and symbol named in `CLAUDE.md`'s component table, `CONTEXT.md`, and `UBIQUITOUS_LANGUAGE.md` resolves in the tree (spot-check by grep)
- [ ] No `sipi_*` FFI entry is defined under `src/cli/`; `CONVENTIONS.md` and `CLAUDE.md` agree on what is oracle-only (until Phase 7 makes the question moot)
- [ ] After Phase 4: no production file (`src/ffi`, `src/server-rs`, `src/cli-rs`, engine modules) includes any header under `src/shttps/`; the oracle transport's dependencies on the extracted modules are visibility-allowlisted with dies-in-Phase-7 comments; ADR-0013 reflects the decomposition
- [ ] After Phase 7: `src/shttps/` and `src/SipiHttpServer.*` no longer exist; no differential CI leg, `$SIPI_BIN_REF` reference, or `GET /metrics` route remains; the removal ADR exists; repo-wide grep finds no dangling oracle reference
- [ ] Adding a field to a clap arg group without touching the `From` impl fails to compile
- [ ] Carved modules' unit tests link narrow targets, not `//src:sipi_lib`; the stale observability→shttps visibility grant is gone
- [ ] `just oracle-comment-check` exists, runs in CI, and passes; grep for `backpressure` in `src/server-rs` returns nothing
- [ ] `SipiIOTiff/Png/J2k.cpp` and `metrics.h` open with their invariant banner naming the enforcing gate
- [ ] Operator docs' pool-knob defaults match `routes.rs` constants; `SIPI_RS_PORT` is documented
- [ ] `src/handlers/` no longer exists; fuzz targets, justfile, workflows, and scope vocabulary reflect the rename
- [ ] `ARCH-MAP.md` exists with `last_verified_commit`, per-component globs, kits, edges, and per-rule enforcement labels; completeness validation passes
- [ ] All phases: approval goldens unchanged; `just bazel-rustfmt-check` + `just bazel-clippy-check` green; differential gate green through Phase 6 (local run for Phases 3 and 4), full e2e green from Phase 7 on

## Dependencies & Risks

- **DEV-6968** is the umbrella Linear issue; Phase 4 files the Phase 7 child issue with the deletion inventory.
- **DEV-6353** (layering_check rollout) — Phases 6 and 8 align with it; findings feed it.
- **ADR-0003** (module co-location) — Phase 8 executes its direction; ADR-0013 is amended by Phase 4 and superseded-in-part by Phase 7's removal ADR; ADR-0006 must be checked before any future descriptor work (Phase 9 deferral).
- **ops-deploy** — Phase 7's pre-removal verification must confirm no deployment or rollback path references the C++ server; the maintainer applies any ops-side change (infra is operator-only).
- Phase 3 is the phase most likely to grow (helper extraction from `cli_app.cpp`); its first checkbox forces the analysis before code moves.
- macOS-local test flake (`generate-xml.sh` segfault) is known; rerun until cached green, don't debug.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| FFI assert work uncovers a live layout divergence | L | H | That is the point; fix the divergence in the same PR, verify with the differential gate |
| Phase 3 `sipi_init` move breaks oracle startup → differential gate red | M | M | BUILD-graph analysis first; local `just bazel-test-differential` before merge |
| Phase 4 extraction perturbs oracle behavior via include churn | L | M | Mechanical-only rule; local differential run per moved subpackage |
| Oracle removal severs an undocumented ops rollback path | L | H | Phase 7's pre-removal verification checkbox; removal lands as one revertable commit |
| Phase 8 carving header moves ripple through includes | M | L | Oracle-free tree; `strip_include_prefix` where it keeps diffs small |
| layering_check blocked by vendored-dep module maps | M | L | Timeboxed; record in DEV-6353 and stop |
| Comment gate false positives block unrelated PRs | M | M | Rust sources only; verify zero hits on the swept tree before wiring CI; allowlist "engine" |
| Phase 12 rename changes wire behavior | L | H | Identifier-only verification checkbox before the rename; full e2e run after |

## Success Metrics

- The dune review's accept lines (quoted per finding in the audit) all hold on re-audit; a follow-up `/dune:review` reports no critical or high finding among DUNE-001…014's subjects, excluding the recorded DUNE-005 deferral (the format-descriptor fan-out remains documented-not-structural until a fifth format arrives).
- After Phase 7, `grep -ri "oracle\|shttps" src/ docs/ justfile .bazelrc .github/` returns only historical ADR content.
- An agent pointed at any audited component can name its kit from `ARCH-MAP.md` and finds every referenced path/symbol on first try.

## References

- Dune audit findings DUNE-001…DUNE-014 (2026-08-14 session; file:line evidence per finding)
- Linear: DEV-6968 (umbrella)
- `src/metadata/BUILD.bazel` — the model module (docstring README, test-seam visibility, colocated tests)
- `src/metadata/icc.h:112-129` — the model in-place invariant banner
- `src/ffi/metrics_snapshot.h:25-34` — the model "Inclusion rule" block
- ADR-0002 (ICC determinism), ADR-0003 (co-location), ADR-0006 (format handler API), ADR-0008 (superseded rate limit), ADR-0013 (shttps seam; amended in Phase 4, superseded-in-part in Phase 7), ADR-0017 (Lua+Rust extensibility)
- Institutional learnings: `learnings/best-practices/deferred-work-lost-across-phase-handoffs.md`, `learnings/best-practices/maudfmt-adoption-no-check-mode-and-clippy-gotchas.md`, `learnings/best-practices/sipi-nix-to-bazel-migration-lessons.md`

---

## Phase 7 execution map (subagent inventory, 2026-08-14)

Full read-only inventory captured before any deletion, so Phase 7 can resume in a
fresh session. Line numbers are as of commit `d7126beb` (Phase 6 tip on branch
`worktree-dune`, sipi PR #781). Re-verify line numbers if the branch moved.

### §1 Files/dirs to delete
- `src/shttps/` (whole dir): `BUILD.bazel`, `README.md`, `certificate/*` (8 files), `docroot/*` (exit.lua, gaga.lua, gugus.elua, post.elua, send-file.html, send-form.html), `scripts/*` (test1.lua, test2.lua, test_functions.lua), `shttps.config.lua`, `transport/*` (ChunkReader, Connection, ConnectionMetrics.h, Server, Shttp.cpp, SockStream, SocketControl, ThreadControl, connection_request_context.h, socketcontrol_test.cpp).
- `src/SipiHttpServer.{h,cpp}` (only two `SipiHttpServer*` files in the repo).
- Differential harness: `test/e2e/tests/differential.rs` (1590 lines); `test/e2e/BUILD.bazel` `differential` target (209-215) + docstring (1-54) + comment block (193-208); `$SIPI_BIN_REF` in `test/e2e/src/lib.rs:46,149,302,724,730` + `test/e2e/sipi_e2e_test.bzl:89-96`; `justfile:235-258` (both recipes); `tools/differential_coverage_check.sh` (pins `EXPECTED_E2E_TESTS=272`).

### §2 cli_app.cpp server mode (file = 1378 lines)
- `run_server` lambda: **607-1008** (builds `SipiConf`, constructs `Sipi::SipiHttpServer server` at 873-874, `server.setMetrics(...ConnectionMetricsAdapter...)` at 882, `set_lua_config` parity shim 975-998, `server.run()` at 1002).
- `attach_server_opts` lambda: **1101-1203** (~40 server-only CLI11 options).
- `cmd_server` subcommand + callback: **1206-1208**.
- server-only option storage vars: **268-307**.
- `detect_available_cores()`: **80-123** (used only by run_server @869).
- oracle includes: line 33 `shttps/transport/Server.h`, line 45 `SipiHttpServer.h`.
- **KEEP:** `sipi_cli_main` shell, `LibraryInitialiser::instance()` (@193), all offline verbs (convert/verify/query/compare/health, ~316-600 + 1210-1356), the CLI11_PARSE tail (1358-1377). `sipiConfGlobals` is FFI-owned (`src/ffi/`) — only its *registration on the oracle server* dies.

### §3 Metrics singleton → plain-counter conversion
`Sipi::observability::Metrics` (`src/observability/metrics.{h,cpp}`) is prometheus-cpp-backed (`registry_` = `std::shared_ptr<prometheus::Registry>` @metrics.h:23). Members: ~30 counters, 8 gauges (incl. `build_info`), 2 histograms (`decode_memory_estimate_bytes`, `request_duration_seconds`), 3 `Family<Counter>` (`decode_memory_decisions_total`, `read_shape_fast_path_total`, `essentials_hash_mismatch_total`).
- **DIES:** `metrics_handler` (`SipiHttpServer.cpp:1395-1401`) + route reg (`:1460` `add_route(GET,"/metrics",...)`) + `Metrics::serialize()` (its only caller).
- **STAYS — production FFI snapshot** `sipi_metrics_snapshot` (`src/ffi/sipi_ffi.cpp:203-247`) reads these 20 singleton members into `SipiMetricsSnapshot`: cache_hits_total, cache_misses_total, cache_evictions_total, cache_skips_total, image_too_large_total, client_disconnected_total, memory_alloc_failures_total, rejected_connections_total, decode_memory_acquired(→acquired_total), decode_memory_rejected(→rejected_total), decode_memory_shadow_rejected(→shadow_rejected_total), decode_memory_near_limit_total, tiff_pyramid_reduced_decodes_total, waiting_connections, cache_size_bytes, cache_files, cache_size_limit_bytes, cache_files_limit, decode_memory_budget_bytes, decode_memory_used_bytes. A plain-counter rewrite must keep all 20 readable.
- NOT snapshotted (per `metrics_snapshot.h:25-34` Inclusion rule): the 2 histograms, `build_info`, the 3 label-fanned families — rewrite may handle these freely (only the dying oracle serializes them).
- `registry()` accessor (`metrics.h:28`) called ONLY by `metrics_registry_test.cpp:112` (`registry()->Collect()`) — a KEPT seam-tripwire test that needs rewriting to the new representation. No production code calls `registry()`.
- Lock-step: `metrics_snapshot.h:79-99` (`static_assert sizeof==160` + per-field offsetof) ↔ `#[repr(C)]` in `server-rs/src/ffi.rs` — keep both valid if field types/order change.

### §4 prometheus-cpp Bazel wiring to drop
`MODULE.bazel`: comment 44-51, `bazel_dep` @52, `single_version_override` 64-68 (+comment 54-63), patch `bazel/patches/prometheus_cpp_load_cc.patch`. `@prometheus-cpp//core` deps: `src/BUILD.bazel:338` (`:engine`, real — engine bumps counters directly), `src/BUILD.bazel:459` (`:sipi_lib`), `src/observability/BUILD.bazel:47` (`:observability`, defines the singleton). MODULE comment 44-46 itself is oracle-referencing ("served by SIPI's own shttps handler") — reword.

### §5 Repo-wide scrub (functional vs doc)
- **CI-functional (`.github/workflows/ci.yml`):** step 132-134 (differential-coverage drift guard) + step 211-221 (differential parity gate). Delete both. Comments at 11/14/17/131/149/211/214.
- **justfile-functional:** 235-252 + 254-258. **.bazelrc:** 297 (fuzz_subset graph comment — see Flag 4).
- **Docs to REWRITE (load-bearing):** `CLAUDE.md:110` (Production-surface-vs-oracle para), rows 118/121/123/150/175, cmd 54; `CONVENTIONS.md:10,12,16-17,25,89,113,115,120,128,130,152,163,166,178,232-233,257,260-261,290-291,298-307` (route-registration worked examples @152-178 show oracle code as the how-to; ConnectionMetrics block @298-307 dies); `CONTEXT.md:23-52` (whole "Internal module: shttps" section — rewrite); `REVIEW.md:5-7,44(SipiHttpServer.hpp→three surfaces),62,65`.
- **Docs (minor):** `docs/src/development/{testing-strategy.md:779, building.md:132-133, profiling.md:33, rbe-write-pressure.md:90, ci.md:78-85}`. ci.md:78-85 describes a "shttps→sipi boundary check" that no longer runs — pre-existing drift, fix too.
- **ADRs:** 0001 + 0013 keep as history + closing note. 0008/0018/0019 matched only incidental "oracle"/"differential" words — verify not substantive.

### §6 Flags (the plan's "deletion-only" assumption was wrong)
- **Flag 1 (blocker):** `src/metadata/BUILD.bazel:90` + `src/metadata/internal/BUILD.bazel:49` dep `//src/shttps:shttps` but only use `shttps::Error`/`HashType` which now live in `//src/util` (via `#include "util/..."`). Stale edge working only through shttps's transitive re-export. Deletion breaks the build. **Fix: retarget both deps to `//src/util`.**
- **Flag 2:** `src/observability/connection_metrics_adapter.h:9` includes `shttps/transport/ConnectionMetrics.h`; the adapter's only caller is `run_server` @882. Delete the adapter + its test + the `//src/shttps:shttps` dep (`observability/BUILD.bazel:45`) — it dies with the oracle, not rewired.
- **Flag 3:** `src/BUILD.bazel:219` (`:sipi_top`/SipiError — verify) and `:445` (`:sipi_lib`) dep `//src/shttps:shttps` (+ `@lua`,`@sqlite3` on sipi_lib). Only `SipiHttpServer.cpp` in the sipi_lib glob uses them; `SipiConf.{h,cpp}` + `SipiReport.cpp` (also in the glob, used by production `src/ffi`/`src/cli/commands`) must STAY. Drop the dead deps when SipiHttpServer is deleted.
- **Flag 4 (hard build break + maintainer decision):** `fuzz/handlers/BUILD.bazel:50` deps `//src/shttps:fuzz_subset` (compiles transport `ChunkReader.cpp`/`Connection.cpp`/`SockStream.cpp` + `iiif_handler.cpp`); `.bazelrc:297` = its whole graph. Deleting `src/shttps/` breaks the fuzz target. Decide: retarget fuzz at the Rust HTTP layer / the Phase-14 carved iiifparser, or retire `//fuzz/handlers`. Coordinate with Phase 14 (which folds `src/handlers` into iiifparser).
- Bottom line: no production `#include` of `src/shttps/` outside `cli_app.cpp` + `connection_metrics_adapter.h` (both oracle, both die). But the BUILD-graph edges (Flags 1,3) + fuzz (Flag 4) must be rewired/decided in the same change.
