---
date: 2026-04-26
type: feat
status: implemented
repositories:
  - name: sipi
related-context:
  - sipi/CONTEXT-MAP.md
  - sipi/shttps/CONTEXT.md
  - sipi/docs/adr/0001-shttps-as-strangler-fig-target.md
commit-prefix: build
---

# build: Add shttps→sipi context boundary check

## Problem

`sipi/CONTEXT-MAP.md` and ADR `0001-shttps-as-strangler-fig-target.md` declare a one-way dependency: SIPI consumes shttps; shttps must not name any `Sipi*` symbol. The boundary is a precondition for the planned strangler-fig replacement of shttps with a Rust HTTP layer. Today the boundary is documented but unenforced, and `shttps/Server.cpp` already violates it five times: one `#include "SipiMetrics.h"` on line 38 and four `SipiMetrics::instance()` call sites on lines 1021, 1030, 1207, 1340. Note that `SipiMetrics` is a global-namespace class — not a member of the `Sipi::` namespace — so any check that only looks for the literal `Sipi::` namespace prefix would miss the four call-site leaks. Without a mechanical gate, the next leak is one PR away.

## Acceptance criteria

- [x] A grep-based check fails when any source file under `shttps/` (extensions `.cpp`, `.cc`, `.c`, `.h`, `.hpp`) contains either:
  - an `#include` of a header whose name starts with `Sipi` (`Sipi*.h` / `Sipi*.hpp`), or
  - a use of any `Sipi*` symbol via scope resolution: an identifier whose name starts with `Sipi` followed by an uppercase letter, used immediately before `::` (covers both `SipiMetrics::instance()` and `Sipi::SipiConf`).
- [x] The check passes today via a single hard-coded allowlist entry for `shttps/Server.cpp` referencing `SipiMetrics`. That one entry suppresses the existing five violations (line 38 include + lines 1021, 1030, 1207, 1340 namespace-use). The entry carries an inline `TODO: remove once metrics-hook lands` comment.
- [x] The check is invokable as `just shttps-context-check` and produces a non-zero exit on violation, with `<file>:<line>: <kind>: <matched-text>` output for each unsuppressed violation.
- [x] The check runs as the first step in the `test.yml` GitHub Actions workflow on every PR, immediately after `checkout`, before LFS pull and Nix setup. It is invoked directly as `bash scripts/shttps-context-check.sh` (no `just`, no LFS, no Nix dependency — fails fast).
- [x] The same check runs locally as a Git pre-commit hook in `sipi/.githooks/pre-commit`, gated on staged files matching `^shttps/` (no-op when the commit doesn't touch the boundary). The hook is auto-activated for anyone using the Nix dev shell: every dev-shell `shellHook` in `flake.nix` runs `git config core.hooksPath .githooks` so the hook fires automatically on `nix develop` (and via direnv). The mandatory gate remains CI; the auto-installed hook is the local fast-feedback path for the canonical dev workflow.
- [x] The check is documented with a one-line pointer in `sipi/shttps/CONTEXT.md` and `sipi/CONTEXT-MAP.md`.
- [x] Vendored sources under `shttps/` (`shttps/sole.hpp`, `shttps/jwt.c`, `shttps/jwt.h`) are excluded from scanning. They are third-party code and not part of the maintained shttps surface.

## Files

### New: `sipi/scripts/shttps-context-check.sh`

A bash script, executable, that:

1. Sets `set -euo pipefail`, resolves `REPO_ROOT` from its own location (`SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"; REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"` — mirrors `scripts/vendor.sh`), and `cd "$REPO_ROOT"` so all subsequent paths are repo-relative.
2. Defines a hard-coded allowlist as an array of `file|symbol` entries, each preceded by an inline `# TODO: …` comment justifying the entry. Initial allowlist:
   - `shttps/Server.cpp|SipiMetrics`  (`# TODO: remove once metrics-hook lands`)
3. Defines a hard-coded vendored-files exclusion set: `shttps/sole.hpp`, `shttps/jwt.c`, `shttps/jwt.h`.
4. Runs **two separate** `grep -rEn` passes against `shttps/`, both with `--include='*.cpp' --include='*.cc' --include='*.c' --include='*.h' --include='*.hpp' --exclude='sole.hpp' --exclude='jwt.c' --exclude='jwt.h'`:
   - Includes pass: pattern `^\s*#\s*include\s*[<"]Sipi[A-Za-z0-9_]*\.(h|hpp)[>"]`, kind label `include`.
   - Symbols pass: pattern `\bSipi[A-Z][A-Za-z0-9_]*::`, kind label `symbol`.

   Each pass captures stdout into a variable and tolerates an empty match set (`grep` returns exit code 1 when nothing matches; under `set -e` the script must guard each call, e.g. `out=$(grep ... || true)` or an `if grep -q ...; then` form). A missing `grep` binary is still a hard failure (no `command -v` defense-in-depth).
5. Filters each pass's lines through the allowlist. **Matching rule:** a violation `(file, line, matched_line)` is suppressed iff there exists an allowlist entry `(allow_file, allow_symbol)` such that `file == allow_file` *and* `matched_line` contains `allow_symbol` as a `\b`-bounded substring. Any other violation is unsuppressed.
6. Prints unsuppressed violations to stderr in `<file>:<line>: <kind>: <matched-text>` form (`<kind>` = `include` or `symbol` per pass) and exits 1 if any remain. Exits 0 otherwise.

### Modify: `sipi/justfile`

Add one recipe near the existing verification recipes (after `vendor-verify`, before the docs section):

```
# Enforce shttps → sipi one-way dependency boundary.
# See sipi/shttps/CONTEXT.md and docs/adr/0001-shttps-as-strangler-fig-target.md.
shttps-context-check:
    @scripts/shttps-context-check.sh
```

### Modify: `sipi/.github/workflows/test.yml`

Add a single step as the first step of the `test` job, immediately after the `checkout` action and before `pull LFS objects` (current line 43). The step invokes the script directly:

```yaml
      - name: Enforce shttps→sipi boundary
        run: bash scripts/shttps-context-check.sh
```

Direct invocation (not `just shttps-context-check`) avoids the `setup-just` dependency and lets the check fail before any expensive setup runs. The check is sub-second; the matrix runs in parallel, so duplicating across arches adds no measurable cost — leaving it on every matrix arch keeps the workflow rectangular.

### New: `sipi/.githooks/pre-commit`

Executable bash script that runs the same boundary check locally before each commit. Behaviour:

1. Sets `set -euo pipefail`.
2. Reads staged files via `git diff --cached --name-only --diff-filter=ACMR`.
3. If no staged path begins with `shttps/`, exits 0 immediately (no-op for commits that don't touch the boundary).
4. Otherwise resolves the repo root via `git rev-parse --show-toplevel` and invokes `"$repo_root/scripts/shttps-context-check.sh"`. Propagates the script's exit code.

### Modify: `sipi/flake.nix`

Append one line to every dev-shell `shellHook` (clang shell at line 430, fuzz shell at line 459, gcc shell at line 488) so entering the dev shell auto-activates the repo-tracked hook directory:

```nix
            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              export MKSHELL=clang
              git config core.hooksPath .githooks 2>/dev/null || true
            '';
```

Notes:
- `2>/dev/null || true` keeps the dev shell usable outside a Git checkout (e.g. CI sandboxes that build from a Nix store path); the assignment is a best-effort convenience, never a fatal error.
- `git config` writes to the repo's local `.git/config`. It is per-clone, never global, idempotent on re-entry, and forgotten when the clone is deleted.
- direnv's `use flake .` (already in `.envrc`) re-runs `shellHook` on every `cd` into the worktree, so the assignment is refreshed for every shell session without explicit user action.

### Modify: `sipi/docs/src/development/developing.md`

Add one short paragraph in the local-setup section:

> **Pre-commit hook:** `nix develop` (and direnv-driven shell loads) automatically point Git at the repo-tracked hook directory `.githooks/` via `git config core.hooksPath .githooks`. The pre-commit hook runs `scripts/shttps-context-check.sh` on commits that touch `shttps/` and refuses commits that introduce a SIPI→shttps leak. Working outside the dev shell? Run the same `git config` line by hand. The mandatory gate is CI; the local hook is fast-feedback parity.

### Modify: `sipi/shttps/CONTEXT.md`

Add a new `## Enforcement` subsection after `## Primary seam`:

> ## Enforcement
>
> The one-way direction is enforced by `just shttps-context-check`, which fails CI on any new `#include "Sipi*.h"` or `Sipi*::` symbol use inside `shttps/`. Allowlist entries live in `scripts/shttps-context-check.sh`.

### Modify: `sipi/CONTEXT-MAP.md`

In the existing "Direction of dependency" paragraph (which already names the leak), append:

> The boundary is enforced by `just shttps-context-check`. See `scripts/shttps-context-check.sh` for the allowlist.

## Verification

Run locally before pushing:

```
just shttps-context-check                                                         # exits 0 with the allowlist in place
git grep -E '^\s*#\s*include\s*[<"]Sipi' shttps/                                  # confirm one match (Server.cpp:38)
git grep -E '\bSipi[A-Z][A-Za-z0-9_]*::' shttps/ -- '*.cpp' '*.cc' '*.c' '*.h' '*.hpp'  # confirm four matches in Server.cpp (1021, 1030, 1207, 1340)
```

Negative test (manual): temporarily remove the `shttps/Server.cpp|SipiMetrics` allowlist entry, re-run the check, confirm it fails with five lines: `shttps/Server.cpp:38: include: SipiMetrics.h` plus the four `symbol: SipiMetrics::` lines. Restore the allowlist.

Pre-commit hook smoke test:
1. Enter the dev shell: `nix develop` (or `cd` into the repo with direnv loaded). Confirm `git config --get core.hooksPath` prints `.githooks`.
2. Stage a file outside `shttps/`, commit — hook is no-op.
3. Stage a clean change under `shttps/` (e.g. an `shttps/Connection.cpp` edit), commit — hook runs the check, passes.
4. Stage a leak — add `SipiCache::dummy()` to any `shttps/*.cpp` — `git commit`. Confirm the hook prints the violation and refuses the commit.

CI verification: open a PR with this plan applied; confirm the new `Enforce shttps→sipi boundary` step shows up green as the first step of the `test` job. Then push a one-line trial PR adding `#include "SipiCache.h"` to any shttps source — confirm CI fails red on that step before LFS pull or Nix setup runs.

## Out of scope

The known `SipiMetrics` leak in `shttps/Server.cpp` is not fixed here. Fixing it requires designing a metrics-callback hook on `shttps::Server` and porting the four call sites — separate plan. Re-homing `Hash`, `Parsing`, `Error`, `Global`, and `makeunique` from `shttps/` into the SIPI side is also separate, sequenced after the metrics fix.

## Commit shape

One commit, prefix `build:`. Conventional Commit prefix is `build:` because this change has no runtime effect on the binary and is release-please-hidden (CI/build infrastructure only). PR title: `build: enforce shttps→sipi context boundary`.
