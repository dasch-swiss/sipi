---
title: Asset access as a first-class decision — Phase 1 (sipi) execution journal
date: 2026-09-12
author: Ivan Subotic
status: draft
repositories:
  - dsp-api
  - dsp-app
---

# Phase 1 execution journal — sipi: the `stream` permission type

Scope: **only** `#### Phase 1: sipi — the `stream` permission type` of
`2026-09-11-01-refactor-asset-access-decision-plan.md`. Phases 2–6 belong to
dsp-api and dsp-app and are executed elsewhere. The plan file's checkboxes are
deliberately **not** edited here — three orchestrators share that file, so
completion is recorded below instead, quoting each checkbox verbatim.

Base commit: `dc02c3f7`. Branch: `worktree-hide-download-link-behaviour`.

## Checkbox register

| Plan checkbox (verbatim) | Status | Where |
| --- | --- | --- |
| Add `Stream = 7` to **both** sides of the hand-mirrored FFI enum — `SipiPermType` in `src/server/rust/src/ffi.rs:352-360` and `sipi_ffi.h:236` — and extend the drift `static_assert`s (`sipi_ffi.h:458-464`) and their paired Rust guard (`ffi.rs:362-364`). **Append; never renumber**, or an existing permission is silently misread across the seam | done | `src/ffi/cpp/sipi_ffi.h` (`SIPI_STREAM = 7` + its `static_assert`), `src/server/rust/src/ffi.rs` (`Stream = 7` + its `const _` assert) |
| Add `"stream"` to `permission_from_str` (`src/server/rust/src/routes.rs:720-730`) | done | `routes.rs` `permission_from_str`, **plus** `valid_permission` in `src/scripting/rust/entry.rs` — see *Finding 1* |
| `/file` serves a `stream` decision exactly as it serves `allow`. Do **not** add a disposition override … | done | `routes.rs` `file_access`: `Allow \| Stream => Ok(access_from(outcome))`. No `content_disposition` change |
| Count `stream` decisions (a counter alongside the existing metrics) so the population is observable before any enforcement is chosen — this is the only day-one behavioural addition | done | `STREAM_DECISIONS` in `routes.rs`, exported as `sipi.preflight.stream_decisions` in `metrics.rs` — see *Finding 2* for the counting site |
| `stream` joins `Allow \| Restrict` in the `Iiif \| KnoraJson` access predicate (`routes.rs:462-467`) so metadata is served | done | `routes.rs` predicate + its comment |
| `stream` on the IIIF image route behaves as `allow` — it carries no pixel restriction … record that so it is never mistaken for a restriction | done | `restricted_dims` returns early for `Allow \| Stream`, with the *why* in its doc comment — see *Finding 3* |
| `UBIQUITOUS_LANGUAGE.md`: add **Stream** to the permission-types table, stating that it expresses intent and is not a security boundary today | done | `UBIQUITOUS_LANGUAGE.md` permission-types table |
| Update every place that hardcodes the count or enumerates the types — `UBIQUITOUS_LANGUAGE.md:117`, its **Permission** row's list at `:108`, `CONTEXT.md:54`, and `docs/src/lua/index.md:446` | done, with one correction | Seven → eight in `UBIQUITOUS_LANGUAGE.md:117` and `CONTEXT.md:54`; the `:108` list gains `stream`. **`docs/src/lua/index.md:446` was not touched** — see *Finding 4* |
| `CONTEXT.md`: record the VRE's **Original** / **Derivative** vocabulary … | already done | Landed as `461d06c6` before this round. Verified present in `CONTEXT.md` § Upstream language; not redone |
| `docs/src/lua/index.md`: document the new return value for `pre_flight` | done | New bullet under the `permission` return value (the `pre_flight` contract section), stating that it enforces nothing |
| ADR-0027 … recording why `stream` exists, that it changes no served byte on day one, what it guarantees (nothing against a determined capture), and which tightenings are reserved to SIPI versus which need a different derivative from ingest | done | `docs/adr/0027-stream-permission-type.md`, `status: accepted` |
| e2e: a `stream` decision returns 200 on `/file` and 200 on `info.json`, and is byte-identical to what `allow` returns for the same asset … | done, with one deviation | Three tests in `test/e2e/tests/security.rs` + a `test_stream` prefix in both hooks of `test/_test_data/config/sipi.init-knora.lua`. The image-route comparison is on decoded pixels, not raw bytes — see *Finding 5* |
| Correct the wave-2 plan's Phase 5 coordination note, which claims dsp-api must default a size in `sipi.init.lua` | done | `docs/specs/2026-09-04-01-fix-sipi-security-hardening-wave-2-plan.md:417` |

## Findings

### 1. The plan missed a second gate on the permission string

`permission_from_str` (`routes.rs`) is not the only place the vocabulary is
enumerated. `valid_permission` in `src/scripting/rust/entry.rs` gates the hook's
return value *before* it ever reaches the shell: a `pre_flight` returning an
unlisted string is a Lua-side error, not a `Deny`. Without adding `"stream"`
there, a hook returning it would have produced a 500 rather than the intended
decision. Both sites are now updated.

### 2. The counter's home is `access_from`, not `permission_from_str`

Counting at the parse site would silently undercount: the preflight access-cache
(`--preflight-cache-ttl`, off by default) stores the already-parsed
`SipiPermType` and a cache hit never re-parses the hook's string. `access_from`
is the single point every resolved decision passes through — hook or cache, IIIF
or `/file` — so the increment lives there, with a comment saying why.

### 3. `restricted_dims` had to learn about `Stream`

It short-circuits only on `Allow`; every other permission loses its tile grid
(`tile_width`/`tile_height` zeroed) so `info.json` / `knora.json` never disclose
the native tiling pyramid. Left alone, `stream` would have produced an
`info.json` differing from `allow`'s — a served-byte change on day one, exactly
what the phase forbids. It now returns early for `Allow | Stream`.

### 4. The plan's cited doc anchor was wrong

`docs/src/lua/index.md:446` enumerates the four **IIIF Auth API** types
(`login` / `clickthrough` / `kiosk` / `external`). That is a different list and
must not gain `stream`; it was left untouched. The site that actually documents
what `pre_flight` may return is the `permission` bullet in the *IIIF preflight
function* section, which now carries the `stream` entry. The file contains no
hardcoded count of the permission types, so nothing else there needed changing.

### 5. Byte-identity is not assertable on the IIIF image route

Two renders of the same image are **not** byte-identical: each embeds an ICC
profile stamped with the wall clock (deterministic only under the approval
tests' injected `SOURCE_DATE_EPOCH`, which production and the e2e both leave
unset). Two responses seconds apart have the same length and differing bytes.

The three e2e tests therefore pin parity at the strongest layer each surface
admits:

- `/file` — raw bytes compared directly (no re-encode happens, so this is true
  byte-identity), plus `Content-Disposition` equality.
- `info.json` — the whole document compared after removing `id` (the request's
  own canonical URL, which necessarily differs by prefix). This covers the
  native dimensions and the `tiles` pyramid a restriction would strip.
- the image route — content-type, dimensions, and decoded RGB pixels.

### 6. A pre-existing e2e isolation bug, exposed by the new test

`public_hosts_allowlist_substitutes_hostile_forwarded_host` starts a second
server in the same working directory as the binary's shared server, so both used
`./cache`. A starting SIPI reclaims every cache file its freshly-read index does
not list, which deletes the entries the shared server still holds in memory; its
next cache hit then 500s. No test after that one had rendered a cached image
before, so nothing caught it. The new image test is the first, and it failed on
`/unit/lena512.jp2/…` — the `allow` request, with no `stream` involved.

Fixed by giving that server its own cache directory (`SIPI_CACHE_DIR`), matching
what `test/e2e/tests/cache.rs` already does for its custom servers. Confirmed by
bisection: skipping that one test made the suite pass unchanged.

### 7. Worktree hygiene

The worktree's LFS fixtures were pointer files (`lena512.jp2` at 131 bytes);
`git lfs pull` was needed before any image test could pass. Separately, running
a SIPI server by hand from `test/_test_data/` writes `test/_test_data/cache/`,
which `repo_root()` then copies into the e2e fixture tree and which poisons
subsequent runs. Remove it after any manual run.

## Verification

Run from the worktree, on macOS (darwin-aarch64):

- `just bazel-build` — green.
- `just bazel-build-server` — green.
- `just bazel-test` — **77/77 pass** (76 executed, 1 cached).
- `just bazel-rustfmt-check` — green.
- `just bazel-clippy-check` — green (`-Dwarnings`).
- `//test/e2e:security` run three times with `--runs_per_test=3` — stable.

Sanitizers were not run: local ASan linking is broken on this machine, so those
legs are CI-only.

## Commits

| Commit | Subject |
| --- | --- |
| (1) | `feat(server,observability): add the stream permission type` |
| (2) | `test(e2e): give the public-hosts server its own cache directory` |
| (3) | `docs(docs): document the stream permission type` |
| (4) | `docs(specs): correct the wave-2 note on the nil restricted-view branch` |

Not pushed; no PR opened.

## Review fixes

An adversarial review of the landed Phase 1 work returned eight findings, all
verified against the code. All eight are fixed; the work was folded into the
commits that introduced each defect (`git commit --fixup` + `--autosquash`), so
the branch still presents as clean Phase 1 work rather than as a fix trail.

### 1. The version-skew failure mode was recorded wrongly (critical)

ADR-0027's Consequences claimed a hook returning `stream` against an older SIPI
gets `deny` via the `permission_from_str` fallback, and a 401. It does not.
`valid_permission` (`entry.rs`) refuses the unknown string **inside the Lua
runtime**; `parse_preflight_values` returns `Err`, that becomes
`PreflightFailure::Error`, and both serve routes map it to a bare 500.
`permission_from_str` is never reached, so its `Deny` fallback never applies.

Corrected in the ADR, and the reason it matters recorded with it: a version skew
presents as a server fault and pages, rather than as an auth response nobody
investigates. The same error in the plan's "Dependencies & Risks" row was
corrected too; nothing else in the plan was touched.

### 2. `CLAUDE.md` still said "seven Permission types" (critical)

`CONTEXT.md` and `UBIQUITOUS_LANGUAGE.md` were updated to eight; `CLAUDE.md:23`
was missed, and it is auto-loaded into every agent session. Fixed. A repo-wide
sweep (excluding `bazel-*`) for any other surviving seven-or-7 count over the
permission vocabulary found none.

### 3. The counter now has a denominator

`sipi.preflight.stream_decisions` was replaced outright — nothing was ever
released under the old name, and this repo bans compatibility shims — by
`sipi.preflight.decisions{permission}`, covering all eight permissions. A bare
`stream` count only says `stream` is non-zero; ADR-0027's premise is that the
population has to be *measurable*, which needs
`decisions{permission="stream"} / sum(decisions)`. No future permission type
needs a one-off counter now.

Shape follows `sipi.lua.kills{reason}`, the precedent in the same file: one
`u64_observable_counter`, one callback observing each value with a `KeyValue`.
`routes.rs` holds `PERMISSIONS` (the vocabulary in discriminant order),
`DECISIONS: [AtomicU64; PERMISSIONS.len()]` indexed by discriminant, and
`permission_str` — an exhaustive match, so a ninth permission type does not
compile until it has a wire string. The ADR, the glossary and the Lua reference
name the new metric. *(This supersedes the metric name recorded in the Phase 1
table above and in Finding 2.)*

**One deviation from the brief.** The increment stays in `access_from`, but
`file_access` now folds *before* it dispatches rather than inside the
`Allow | Stream` arm. With a stream-only counter that placement was correct; with
a denominator it was not — a `/file` decision refused with 401 or 403 never
reached `access_from`, so every refused `/file` decision would have been missing
from the denominator. `access_from` is still the single fold point.

### 4. The counter is now tested

`every_resolved_decision_is_counted_once_under_its_permission` (a `routes.rs`
unit test) walks all three paths that reach `access_from` against a real
`AppState` + `LuaEnv`: the fresh-hook IIIF path, the preflight-cache-hit path
(asserted to be a hit via `preflight_cache::hits()`, so it cannot silently
degrade into a second hook run), and the `/file` path with a refusing hook. It
snapshots all eight counters up front and asserts the exact delta vector at the
end, so a double count or a stray increment fails as loudly as a missing one.
No path is covered by assertion alone — each is reached through the real
function, not through `access_from` directly.

All three are unit-testable because `AppState`'s fields are private but
in-module, and `LuaEnv::new` needs only a temp dir and an init script. The
counters are process-global, so all the assertions live in one test: a sibling
test resolving a decision in parallel would make the deltas unreadable.

### 5. The two vocabulary gates are now pinned in lockstep

`valid_permission` (`scripting`) and `permission_from_str` (`server`) had to stay
in step with nothing enforcing it — the exact defect the Phase 1 implementer hit
and fixed by hand, leaving no guard behind.

`valid_permission` is now `pub` and re-exported from `scripting`'s `lib.rs`
(dependency direction allows only this: `server` depends on `scripting`, not the
reverse, and `SipiPermType` lives in `server`'s `ffi.rs`), and
`permission_vocabulary_is_identical_on_both_gates` asserts for every permission
that `permission_from_str` round-trips it **and** that `valid_permission`
accepts it, respecting the `extended` split (`clickthrough`/`kiosk`/`external`
are IIIF-only; the other five are in the base set `/file` shares).

The compile-time guard is `permission_str`'s exhaustive match. It sits in
production rather than in the test, because the metric's attribute needs it
anyway — which makes it strictly stronger than a test-local match: a ninth
variant breaks the build, not just the test run.

### 6. The duplicated rationale is gone

The "why this counter exists" comment was written near-verbatim at the static and
at the registration site. It now states the WHY once at the static (matching the
`preflight_cache.rs` + `metrics.rs` sibling pattern) and says something
complementary at the registration site — what the exported series looks like.

### 7. `ARCH-MAP.md` registers the Permission vocabulary

The map predated this work and never mentioned `SipiPermType`, despite it being a
hand-mirrored vocabulary spanning `ffi`, `server` and `scripting`. Now: a key
entity under `ffi` (the enum, appended to and never renumbered), under `server`
(the dispatch plus `PERMISSIONS`/`permission_from_str`/`permission_str`) and
under `scripting` (`valid_permission`); a `server` boundary rule recording that
the vocabulary is gated twice and what pins the two together; and a Conventions
bullet for "new permission type" naming the full fanout, analogous to the
format-handler one. No `tools/permission-fanout.sh` — the round-trip test and the
exhaustive match are the mechanical guard, and a script would only duplicate
them. The map's `date` is refreshed to 2026-09-12 (`last_verified_commit` stays
`none`: rebase-merge makes a pre-merge SHA unknowable).

### 8. The wave-2 e2e matrix covers `stream`

Phase 5's still-unchecked decision × credential matrix enumerated the vocabulary
exhaustively without `stream`, so whoever picks it up would silently under-cover
it. `stream` added there and in the related acceptance line, with what it must
assert (native dims, per ADR-0027).

### Verification (review fixes)

Run from the worktree, on macOS (darwin-aarch64):

- `just bazel-build` — green.
- `just bazel-test` — **77/77 pass**.
- `just bazel-rustfmt-check` — green (after `just bazel-rustfmt`).
- `just bazel-clippy-check` — green (`-Dwarnings`).
- `just commit-lint` — green.

Sanitizers remain CI-only (local ASan linking is broken on this machine).

### Commit grouping

No standalone `fix:` commits: every one of these corrects work introduced earlier
in this same branch, so each landed as a `--fixup` onto its introducing commit.
Findings 3–6 folded into the `feat` commit (they are the same behavioural
addition, and the `valid_permission` export exists only to pin the vocabulary
that commit introduced); findings 1 and 2 plus the metric rename in prose folded
into the docs commit; findings 8 and this journal section folded into the
`docs(specs)` commit. Finding 7 is the one exception — the architecture-map
registration is its own concern (repo topology, not the `stream` type), so it is
its own commit rather than being folded into a commit about documenting `stream`.
