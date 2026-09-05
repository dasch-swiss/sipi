---
title: "Unify sipi builds behind Nix derivations (justfile + CI single source of truth)"
type: feat
date: 2026-04-20
author: "Ivan Subotic"
status: superseded
superseded_by: "docs/specs/2026-04-29-01-refactor-sipi-bazel-migration-plan.md"
linear: DEV-6280
repositories:
  - sipi
---

> **Superseded.** The `just nix-*` recipe-unification this plan
> proposed is obsolete: Sipi's build orchestrator moved from Nix to
> Bazel under [the Sipi Bazel migration](2026-04-29-01-refactor-sipi-bazel-migration-plan.md)
> (DEV-6341). After that migration's PR Y+6 (DEV-6348) every CI step
> invokes `just bazel-*` recipes; PR Y+7 (DEV-6349) removed the last
> `just nix-*` build recipes. The "single source of truth" goal is
> preserved (the Bazel `BUILD.bazel` graph), but the implementation
> route is different.

# Unify sipi builds behind Nix derivations (justfile + CI single source of truth)

## Overview

Phase 1 of the Nix unified build (see `01-feat-nix-unified-build-plan.md`, status: implemented) wrapped sipi's existing CMake + ExternalProject build inside the flake and gave us `packages.{default,dev,release,static-amd64,static-arm64,docker,…}`. But it left the justfile and the CI wiring bifurcated:

- **Justfile drift.** Today `just nix-build` runs raw `cmake -B build && cmake --build` inside the dev shell (imperative, not cached). `just nix-build-release`, `just nix-build-static-amd64`, `just nix-build-static-arm64` call `nix build .#<variant>` (cached via Cachix). Two mental models coexist under one `nix-*` prefix.
- **CI drift.** Some workflows already route through justfile (`test.yml` → `just nix-build && just nix-test`; `loadtest.yml` → `just nix-build`). Others inline build commands (`test.yml` nix-static step inlines `nix build -L --option impure-env …`; `sanitizer.yml`, `fuzz.yml`, `publish.yml` release-archive step each inline cmake or nix build calls). This violates `CLAUDE.md:11` ("All targets are in a single `justfile`.").
- **Performance gap.** Because the justfile's `nix-build` bypasses Nix's derivation graph, ext/ rebuilds from source every CI run. Measured impact: `nix-clang / amd64` takes 873 s for build+unit-test, versus `nix-static / amd64` 663 s for the cross-compile (which is strictly more work, but fully cached).

This plan unifies both, strictly. After it lands: **every build recipe in the justfile goes through a Nix derivation — no exceptions**. Imperative cmake is not exposed as a recipe. The justfile is a thin wrapper around `nix build .#<variant>` + a handful of test-running recipes that consume `$SIPI_BIN`. For incremental inner-loop iteration (edit one `.cpp`, see a fast rebuild), developers drop into `nix develop` and run `cmake -B build && cmake --build build` by hand — a documented pattern in `CLAUDE.md` and `docs/src/development/developing.md`, deliberately not a recipe. The architectural precondition for DEV-5939 (split `ext/` ExternalProject tree into per-dep Nix derivations) is satisfied as a side effect — every consumer is now a derivation.

## Problem Statement

### 1. CI drift — places that bypass justfile

Audit from research (2026-04-20):

| Workflow | Step | Command | Through justfile? |
|---|---|---|---|
| `test.yml` | Build and test (unit) | `nix develop --command bash -c "just nix-build && just nix-test"` | yes |
| `test.yml` | Rust e2e and Hurl tests | `nix develop --command bash -c "just rust-test-e2e && just hurl-test"` | yes |
| `test.yml` | Gather code coverage | `nix develop --command bash -c "just nix-coverage"` | yes |
| `test.yml` | Build static binary | `nix build -L --option impure-env … .#static-${arch}` | **no — inlined** |
| `test.yml` | Verify static linkage | `file result/bin/sipi` + `readelf -d …` | **no — inlined** |
| `test.yml` | Rust e2e tests against static binary | inline `SIPI_BIN=$GITHUB_WORKSPACE/result/bin/sipi cargo test …` | **no — inlined** |
| `test.yml` | Audit runtime dylibs (macOS) | inline `otool -L` + bash filtering | **no — inlined** |
| `sanitizer.yml` | Build | `just nix-build-sanitized` | yes (recipe itself is imperative cmake) |
| `sanitizer.yml` | Run tests | inline `cd build-sanitized && ctest --output-on-failure` | **no — inlined** |
| `sanitizer.yml` | Rust e2e | inline `SIPI_BIN=$PWD/../../build-sanitized/sipi cargo test …` | **no — inlined** |
| `fuzz.yml` | Configure + build fuzzer | inline `cmake -S . -B build-fuzz -DSIPI_ENABLE_FUZZ=ON && cmake --build build-fuzz …` | **no — inlined (no recipe exists)** |
| `fuzz.yml` | Run fuzzer | inline `./iiif_handler_uri_parser_fuzz corpus/ …` | **no — inlined** |
| `publish.yml` | Docker build/test | `just docker-test-build-{amd64,arm64}` | yes |
| `publish.yml` | Release archive | `nix build -L --option impure-env … .#release-archive-${arch}` | **no — inlined** |
| `loadtest.yml` | Build sipi | `nix develop --command bash -c "just nix-build"` | yes |
| `loadtest.yml` | Start sipi server | inline `nix develop --command bash -c "cd test/_test_data && exec ../../build/sipi …"` | **no — assumes cmake layout** |

### 2. Justfile — two incompatible recipe families

| Recipe | Implementation | Cache model |
|---|---|---|
| `just nix-build` (justfile:147-149) | imperative `cmake -B build` | **no Cachix for ext/** |
| `just nix-build-sanitized` (222-224) | imperative `cmake -B build-sanitized` | no Cachix |
| `just build` (239-241) | imperative `cmake -B build` RelWithDebInfo | no Cachix |
| `just nix-build-release` (249-251) | `nix build .#default` | full Cachix |
| `just nix-build-static-amd64` (254-255) | `nix build .#static-amd64` | full Cachix |
| `just nix-build-static-arm64` (258-259) | `nix build .#static-arm64` | full Cachix |
| `just nix-docker-build` (262-263) | `nix build .#docker-stream \| docker load` | full Cachix |

The `nix-*` prefix is ambiguous: sometimes it means "goes through Nix", sometimes "run this inside `nix develop`, then an imperative tool takes over."

### 3. Hardcoded `./build/` assumptions

Post-unification, tests must consume the built binary from `./result/bin/sipi` (Nix convention), not `./build/sipi` (cmake convention). Audit of hardcoded `./build/` assumptions:

- `justfile:159-163` — `rust-test-e2e` guards `{{justfile_directory()}}/build/sipi`
- `justfile:186` — `hurl-test` runs `{{justfile_directory()}}/build/sipi`
- `justfile:202,213` — `nix-coverage`/`nix-coverage-html` assume `cd build`
- `justfile:228` — `nix-test-sanitized` assumes `cd build-sanitized`
- `justfile:232, 236` — `nix-run`, `nix-valgrind` run `./build/sipi`
- `sanitizer.yml:63,75` — `cd build-sanitized && ctest`, `SIPI_BIN=$PWD/../../build-sanitized/sipi`
- `fuzz.yml:94,105,119,125,152-177` — all reference `build-fuzz/…`
- `test.yml:85` — codecov reads `build/coverage.xml`
- `docs/src/development/building.md:135`, `docs/src/development/developing.md:30`, `config/sipi.localdev-config.lua:5` — documentation / config references

### 4. Coverage pipeline is in the cmake tree

- `CMakeLists.txt:217-232,563-564` — `CODE_COVERAGE=ON` adds instrumentation; `.gcno` files land beside objects under `build/**/CMakeFiles/*.dir/`
- `justfile:147-149` (`nix-build`) — always passes `-DCODE_COVERAGE=ON`, so every dev build is instrumented
- Tests write `.gcda` counters beside `.gcno` files (paths baked into the binary at compile time)
- `justfile:201-209` (`nix-coverage`) — `cd build && gcovr -j … --xml coverage.xml …` → `build/coverage.xml`
- `test.yml:85` — codecov uploads `build/coverage.xml`

Enabling `doCheck` (so unit tests run inside the derivation) means the sandboxed build directory is reaped after the test step completes. Coverage must migrate to either (a) a multi-output (`$coverage`) that preserves the report, or (b) a separate `.#coverage` flake output that depends on `.#dev`.

## Proposed Solution

Five moves, each canonically endorsed in the Nix/C++ ecosystem:

1. **Set `doCheck = enableTests` in `package.nix:116`.** Unit tests run inside the derivation's `checkPhase` for `.#default` and `.#dev`; `.#release` opts out via `enableTests = false`. A `nix build .#dev` that succeeds is, by construction, a tested build. Cachix refuses to substitute failed-test derivations.
2. **Add the missing derivation variants to `flake.nix`.** Today the flake has `default`, `dev`, `release`, `static-{amd64,arm64}`, `release-archive-*`, `docker`, `docker-stream`. Missing: `sanitized` (Debug + ASan/UBSan) and `fuzz` (Clang + libstdc++, `SIPI_ENABLE_FUZZ=ON`, builds fuzzer binaries). Introduce both as new `packages.*` outputs using `pkgs.sipi.override` with new `enableSanitizers` / `enableFuzzing` parameters on `package.nix`. Every build configuration today's CI invokes imperatively gets a derivation.
3. **Rewrite the justfile as a thin Nix wrapper.** Delete all imperative `cmake` invocations from recipes. `nix-build` → `nix build .#dev`. `nix-build-sanitized` → `nix build .#sanitized`. `nix-build-fuzz` → `nix build .#fuzz`. Today's `nix-build-release`, `nix-build-static-{amd64,arm64}`, `nix-docker-build`, `nix-build-static-*` stay as-is. Remove: today's imperative `nix-build`, `nix-build-sanitized`, `nix-test`, `nix-test-sanitized`, `build`, `nix-coverage` (re-add as a derivation wrapper), `nix-coverage-html`. No `cmake-*` recipes are introduced — the imperative path is documented only, not a recipe.
4. **Add `coverage` as an extra output on `.#dev`.** `package.nix` declares `outputs = [ "out" "coverage" ]` and runs `gcovr` in `postCheck`, writing `$coverage/coverage.xml`. A `just nix-coverage` recipe becomes `nix build .#dev^coverage` + a path echo. Codecov uploads from `result-coverage/coverage.xml`.
5. **Migrate test recipes to read `SIPI_BIN` with a canonical default.** Every recipe that runs sipi resolves it via `SIPI_BIN=${SIPI_BIN:-{{justfile_directory()}}/result/bin/sipi}`. The Rust harness already does this (`test/e2e-rust/src/lib.rs:40`). The `./build/sipi` assumption is retired from the justfile; docs and config files are updated.

Every CI workflow is audited: every `run:` step that builds or tests sipi invokes `just <recipe>`. Every `nix-*` recipe goes through a derivation. After this plan, running the exact CI build locally is `just <recipe>` on a machine with Nix — which pairs directly with Determinate Systems' native-linux-builder (now available to the author), letting the macOS dev machine build Linux-target artifacts locally via a remote builder: <https://docs.determinate.systems/troubleshooting/native-linux-builder/>. This means `nix build .#sanitized`, `.#fuzz`, `.#static-amd64`, `.#static-arm64`, `.#release-archive-*`, and `.#docker` all run locally during implementation — CI is the final gate, not the iteration loop.

### The dev-shell inner loop is documented, not recipe-fied

A developer editing one `.cpp` file and wanting a seconds-fast incremental rebuild does NOT run a justfile recipe. They drop into the dev shell and run cmake by hand:

```
nix develop
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON
cmake --build build --parallel
./build/sipi --config config/sipi.localdev-config.lua
```

This path is documented in `CLAUDE.md` and `docs/src/development/developing.md` as the "inner-loop development" workflow, with an explicit note that it is non-reproducible and will NOT match CI outputs byte-for-byte. It is intentionally not a recipe — a recipe would become an alternate source of truth and create the drift surface this plan is eliminating.

Why this asymmetry is correct:
- Recipes are contracts. Every recipe is a promise that "CI runs this too." Having a recipe that CI cannot run violates the contract.
- Humans doing ad-hoc incremental rebuilds are already committing to a non-reproducible workflow. Documenting the pattern is the honest signal that this is a local-only inner loop.
- If the inner-loop cmake invocation genuinely needs to be the same across a team (e.g. non-trivial CMake flags), that's evidence the configuration deserves a Nix variant in `flake.nix`, not a recipe wrapper around cmake.

### Not in scope

- **Dockerfile migration to `nix build .#docker`.** The `.#docker`/`.#docker-stream` targets in `flake.nix:260-332` work but the current `Dockerfile` is pure imperative cmake + apt on `ubuntu:24.04`. Migrating touches base-image, runtime layout, deploy paths, SBOM flow — a separate PRD. (Also: CONVENTIONS.md:80 already claims "Alpine (musl)" while the Dockerfile uses Ubuntu; that drift is its own cleanup.)
- **DEV-5939 (split ext/ into per-dep derivations).** This plan is the precondition; the split is Phase 2.
- **Switching away from `wrk`, Cachix, or Determinate Nix action.** All three remain.
- **Changes to CMake's `CODE_COVERAGE=ON` invariant** (always-on instrumentation). Left as-is.

## Technical Approach

### Architecture

Three boundaries change:

1. **`package.nix` grows** new parameters (`enableSanitizers`, `enableFuzzing`), a `checkPhase` (ctest), and a second output (`$coverage`) populated by a `postCheck` gcovr invocation. `doCheck = enableTests` (not unconditional). These changes apply to every consumer of `pkgs.sipi` — `.#default`, `.#dev`, `.#release` all gain the conditional-doCheck behavior; `.#dev` (with `enableCoverage = true`) also populates the `coverage` output.
2. **`flake.nix` grows** two new `packages.*` outputs — `sanitized` and `fuzz` — so every CI-invoked configuration has a derivation. The pre-existing outputs (`default`, `dev`, `release`, `static-*`, `release-archive-*`, `docker`, `docker-stream`) keep their attribute definitions unchanged; their behavior shifts only through the `package.nix` parameter additions.
3. **`justfile` recipes** adopt exactly two shapes:
   - `nix-*` recipes → call `nix build .#<variant>` and optionally echo an artifact path
   - test-running recipes → resolve `$SIPI_BIN` env var with canonical default `./result/bin/sipi`

No imperative cmake invocations live in the justfile. Every CI workflow step becomes `just <recipe>`. New recipes are added for cases that are currently inlined in CI (`nix-build-fuzz`, `nix-release-archive-<arch>`, `nix-static-<arch>` with impure-env handling, `nix-static-linkage-verify`, `nix-macos-dylib-audit`).

### Implementation Workstream

All changes land in a **single PR** on one feature branch against `sipi` `main`. No phases, no staged rollout. The work is mutually dependent — splitting it would break CI on every intermediate commit — and the scope is small enough (one repo, ~15 files) to review as one unit. During implementation, group work into separate commits on the branch by concern for reviewability; immediately before merge, rewrite the branch's commit history into the final commit sequence per the Commit Organization section at the end of this plan.

The tasks below are grouped by concern, not by sequencing. During implementation they can be interleaved.

#### A. Package and flake changes

**Files:** `package.nix`, `flake.nix`.

- **Set `doCheck = enableTests` in `package.nix`** (not unconditional `true`). This preserves the `.#release` semantics (`enableTests = false → doCheck = false`) and keeps `.#default` / `.#dev` running tests in-sandbox. `mkStaticBuild` in `flake.nix:127` is a fresh `stdenv.mkDerivation` that does not consume `pkgs.sipi`, so `doCheck` on `package.nix` has no effect on `static-*` / `release-archive-*`.
- Set `checkPhase` to `cd build && ctest --output-on-failure` (or use the default auto-derived `ctest` invocation if `stdenv` supports it).
- Add `outputs = [ "out" "coverage" ]` to the derivation. Declare a `postCheck` phase that runs `gcovr -j $NIX_BUILD_CORES --xml "$coverage/coverage.xml" --root .. --gcov-executable "llvm-cov gcov" --exclude '../test/' --exclude '../fuzz/' --exclude '../ext/' --exclude '../include/'` against the build tree. The `coverage` output is populated only when **both** `enableCoverage = true` **and** `enableTests = true` (no tests → no `.gcda` counters → gcovr has nothing to aggregate); otherwise `postCheck` creates an empty `$coverage` marker directory (Nix requires every declared output to exist).
- Keep `enableCoverage` as a `package.nix` parameter. `.#dev` sets it to `true` (align with today's always-instrumented behavior); `.#default` / `.#release` leave it `false`.
- **ApprovalTests sandbox-compatibility probe (first verification step).** Immediately after the `package.nix` edits compile, run `nix build .#dev` locally before committing anything else, and verify that ApprovalTests tests (`test/approval/metadata_golden_test.cpp`) pass in the Nix sandbox. ApprovalTests sometimes writes `.received.txt` alongside source; if that path is outside `build/`, it will fail in sandbox. If it fails, either (a) configure ApprovalTests to redirect received files into `$TMPDIR`, (b) skip ApprovalTests in the sandboxed check phase (`ctest --output-on-failure -LE approval`) and keep them as a dev-shell-only invocation, or (c) fix the test layout. Pick one; document in `package.nix` comment. Failing fast here avoids a messy unwind later in the workstream.
- **Kakadu FOD + `GH_TOKEN`.** `pkgs.kakaduArchive` is a fixed-output derivation; once Cachix or the local store has a hit, no token is needed. But on a cold cache with no local hit, `.#dev` will require `GH_TOKEN` to pull the archive — same invariant as `.#default` today. Verify every CI workflow step that runs `just nix-build` exports `GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}` in the `env:` block. Today `test.yml:65` and `loadtest.yml:53` already do; verify the migrated sanitizer / fuzz / publish steps do too.
- **Introduce `packages.sanitized` in `flake.nix`.** The `enableSanitizers` parameter already exists in `package.nix` at line 23 (introduced in Phase 1 but unused); wire it through `cmakeFlags` so `enableSanitizers = true` adds `-DENABLE_SANITIZERS=ON` (parallel to the existing `enableCoverage → -DCODE_COVERAGE=ON` mapping at `package.nix:87-99`). Flake attr: `packages.sanitized = pkgs.sipi.override { cmakeBuildType = "Debug"; enableSanitizers = true; enableTests = true; }`. `doCheck = enableTests` runs the sanitizer-instrumented gtest suite in the sandbox; document a derivation-level `ASAN_OPTIONS` env-var override so suppressions are consistent with today's `sanitizer.yml` behavior.
- **Introduce `packages.fuzz` in `flake.nix`.** Add a new `enableFuzzing` parameter to `package.nix` and wire it through `cmakeFlags` so `enableFuzzing = true` adds `-DSIPI_ENABLE_FUZZ=ON` (parallel to the `enableCoverage` / `enableSanitizers` mappings). Fuzz needs `llvmPackages_19.stdenv` (libstdc++, matching the `fuzz` dev shell at `flake.nix:374-400`) because libFuzzer's ABI is tied to libstdc++. Flake attr: `packages.fuzz = (pkgs.sipi.override { stdenv = pkgs.llvmPackages_19.stdenv; enableFuzzing = true; enableTests = false; }).overrideAttrs (old: { buildPhase = ''cmake --build build --parallel $NIX_BUILD_CORES --target iiif_handler_uri_parser_fuzz''; installPhase = ''mkdir -p $out/bin && cp build/fuzz/handlers/iiif_handler_uri_parser_fuzz $out/bin/''; })`. **Produces fuzzer binary only** — the derivation intentionally does not install sipi's runtime files (config, scripts, server), since libFuzzer invocations don't consume them. Running the fuzzer stays outside the derivation, consumed by the `nix-run-fuzz` justfile recipe, which **forwards libFuzzer's exit code** (non-zero on new crash detection) so `fuzz.yml`'s crash-reporting logic keeps working unchanged.
- Verify: `nix build .#dev` fails if any test fails; `nix build .#dev.debug` still produces symbols; `nix build .#dev^coverage` produces the XML; `nix build .#default` and `.#release` stay green; `nix build .#sanitized` runs ASan+UBSan-instrumented tests; `nix build .#fuzz` produces `result/bin/iiif_handler_uri_parser_fuzz`.

#### B. Justfile rewrite

**Files:** `justfile`.

- **Delete** every imperative recipe from today's justfile:
  - `nix-build` (L147-149), `nix-test` (L152-153), `nix-build-sanitized` (L222-224), `nix-test-sanitized` (L227-228), `build` (L239-241), `nix-coverage` imperative gcovr (L201-209), `nix-coverage-html` (L212-216), `nix-coverage-full` (L219). Rewrite `nix-run` and `nix-valgrind` (L231-236) to resolve `$SIPI_BIN`.
- **Write** the new derivation-backed recipes:
  - `nix-build` → `nix build .#dev` (canonical CI path; supersedes today's imperative recipe name)
  - `nix-build-default` → `nix build .#default`
  - `nix-build-release` → `nix build .#release` (already exists; unchanged)
  - `nix-build-sanitized` → `nix build .#sanitized` (new — replaces the deleted imperative recipe)
  - `nix-build-fuzz` → `nix build .#fuzz` (new — derivation-backed)
  - `nix-build-static-{amd64,arm64}` (already exist; extend to pass `--extra-experimental-features "configurable-impure-env" --option impure-env "GH_TOKEN=$GH_TOKEN"` when the env var is set). The `configurable-impure-env` feature flag is required by the Kakadu FOD's `impureEnvVars` declaration; CI sets it via the Determinate Nix Action config (`test.yml:111-112`), local devs need it in `~/.config/nix/nix.conf` OR accept the per-invocation `--extra-experimental-features` flag from the recipe.
  - `nix-build-release-archive-{amd64,arm64}` (new) → `nix build .#release-archive-${arch}` with the same impure-env + experimental-features handling as the static recipes
  - `nix-docker-build` (already exists; unchanged)
  - `nix-coverage` → `nix build .#dev^coverage` and echo `result-coverage/coverage.xml`
  - `nix-static-linkage-verify $path` (new) → `file "$path" && ! readelf -d "$path" | grep -q '(NEEDED)'`
  - `nix-macos-dylib-audit $path` (new) → `otool -L "$path"` + awk filter + fail-on-`/opt/homebrew/` / `/usr/local/` hits
- **Rewrite** test recipes to resolve `$SIPI_BIN`:
  - `rust-test-e2e` → default `SIPI_BIN=${SIPI_BIN:-{{justfile_directory()}}/result/bin/sipi}`. Existence guard validates the resolved path: `if [ ! -x "$SIPI_BIN" ]; then echo "sipi binary not found at $SIPI_BIN. Run 'just nix-build'." >&2; exit 1; fi`. The Rust harness's `find_sipi_bin()` fallback (`test/e2e-rust/src/lib.rs:40`) remains intact: when `$SIPI_BIN` is unset AND `./result/bin/sipi` doesn't exist, the harness falls back to `./build/sipi` — intentional for devs iterating via the dev-shell inner-loop (`cmake --build build` then `cargo test` without `just`).
  - `hurl-test` (justfile:183-198) → same pattern; replace hardcoded `{{justfile_directory()}}/build/sipi` with `$SIPI_BIN`.
  - `nix-run` → `$SIPI_BIN --config config/sipi.config.lua`
  - `nix-valgrind` → `valgrind --leak-check=yes --track-origins=yes $SIPI_BIN --config config/sipi.config.lua`
  - `nix-run-fuzz $corpus $duration` (new) → resolves fuzzer binary via `$FUZZ_BIN` env var, defaults to `result/bin/iiif_handler_uri_parser_fuzz`, forwards corpus + `-max_total_time`. Exit code propagates from libFuzzer verbatim: zero = time budget reached with no new crash; non-zero = new crash found (fuzz.yml's existing crash-detection logic inspects the exit code and opens a GitHub issue).
- **`clean` recipe** → remove `build/`/`build-sanitized/`/`build-fuzz/` (legacy or manual dev-shell artifacts) AND `result`/`result-*` symlinks; be explicit about each path.
- **No `cmake-*` recipes.** The inner-loop workflow is documented in the docs section, not a recipe.

#### C. CI workflow migration

**Files:** `.github/workflows/{test,sanitizer,fuzz,publish,loadtest}.yml`.

- **`test.yml` nix-clang job:**
  - Replace `"just nix-build && just nix-test"` with `"just nix-build"` (tests now run inside the derivation).
  - Replace `"just rust-test-e2e && just hurl-test"` with the same invocation after exporting `SIPI_BIN=$PWD/result/bin/sipi` (or letting the default resolve).
  - Replace `just nix-coverage` invocation; update codecov `files:` to `result-coverage/coverage.xml`.
- **`test.yml` nix-static job:**
  - Replace inline `nix build -L --option impure-env … .#static-${arch}` with `just nix-build-static-${arch}`.
  - Replace `file result/bin/sipi` + `readelf` with `just nix-static-linkage-verify result/bin/sipi`.
  - Replace inline `SIPI_BIN=… cargo test …` with `SIPI_BIN=$GITHUB_WORKSPACE/result/bin/sipi just rust-test-e2e`.
- **`test.yml` nix-macos-audit job:**
  - Replace inline `otool -L` pipeline with `just nix-macos-dylib-audit result/bin/sipi`.
- **`sanitizer.yml`:**
  - Replace imperative `just nix-build-sanitized` with the new derivation-backed `just nix-build-sanitized` (same recipe name, new implementation). Tests run inside the derivation's `checkPhase`, so the separate inline `cd build-sanitized && ctest` step is deleted.
  - Replace inline `SIPI_BIN=$PWD/../../build-sanitized/sipi cargo test …` with `SIPI_BIN=$PWD/result/bin/sipi just rust-test-e2e`.
  - Export `ASAN_OPTIONS` at the step level if suppressions differ between today's inline invocation and the derivation's `checkPhase`.
- **`fuzz.yml`:**
  - Replace inline `cmake -S . -B build-fuzz …` with `just nix-build-fuzz`.
  - Replace inline fuzzer invocation with `just nix-run-fuzz $corpus $duration`.
  - **Update the crash-reproduction snippet in the "Open GitHub issue for crash" step** (lines 157-184 — heredoc in the issue body) to reference `just nix-build-fuzz`.
- **`publish.yml`:**
  - Replace inline release-archive build with `just nix-build-release-archive-${arch}`.
- **`loadtest.yml`:**
  - Replace `cd test/_test_data && exec ../../build/sipi --config config/sipi.e2e-test-config.lua` with `cd test/_test_data && exec "${SIPI_BIN:-../../result/bin/sipi}" --config config/sipi.e2e-test-config.lua`. Keep the supervision logic (PID + log capture + `kill -0` liveness) in YAML — it's CI-specific glue already in place (commit `0392dfb`).

#### D. Documentation and config updates

**Files (all contain stale recipe names or `./build/sipi` path references — verified by grep 2026-04-20):**

- `CLAUDE.md:11,24-40` (Quick Reference — the canonical mental model for new devs)
- `CONVENTIONS.md` (add "Build reproducibility invariant" section)
- `README.md:85-87` (legacy `make nix-build`, `make nix-test`, `make nix-test-e2e`)
- `docs/src/development/building.md:67,135-136,206-215`
- `docs/src/development/developing.md:30,96,115`
- `docs/src/development/ci.md:140`
- `docs/src/development/testing-strategy.md:693,712-713,766,771`
- `config/sipi.localdev-config.lua:5`

Tasks:

- Update `CLAUDE.md` "Quick Reference" to list the new `nix-*` recipe taxonomy. Every build-related recipe goes through `nix build .#<variant>`. No `cmake-*` entries.
- **Add a new "Inner-loop development" section to `CLAUDE.md` and `docs/src/development/developing.md`** explaining that the fast-iteration workflow is NOT a recipe:
  ```
  # Inner-loop development (incremental rebuilds)
  # The justfile does not expose an imperative build recipe — by design.
  # For the edit/rebuild/run cycle, drop into the dev shell and call cmake by hand:
  nix develop                   # enters the dev shell with all build deps
  cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON
  cmake --build build --parallel
  ./build/sipi --config config/sipi.localdev-config.lua
  # Subsequent edits: cmake --build build (incremental)
  # This is non-reproducible and does not match CI outputs byte-for-byte.
  # To run the reproducible CI build, use `just nix-build` instead.
  # Prerequisites: `just kakadu-fetch` once per version (pulls the Kakadu
  # archive into vendor/; the dev-shell cmake invocation consumes it there).
  ```
- Update `docs/src/development/building.md` to reflect the new recipe taxonomy and `./result/` artifact path.
- Update `docs/src/development/ci.md` to reflect new step invocations.
- Update `docs/src/development/testing-strategy.md` to replace `make nix-*` with `just nix-*`.
- Update `README.md` to replace `make nix-*` with `just nix-*`.
- Update `config/sipi.localdev-config.lua:5` comment to point at `./result/bin/sipi` or `just nix-run`.
- `CONVENTIONS.md` — add "Build reproducibility invariant" section: "Every justfile recipe that builds sipi goes through `nix build .#<variant>`. CI invokes only `just <recipe>`. Incremental inner-loop development is a documented dev-shell pattern, not a recipe."

#### E. Verification (local-first, CI as final gate)

**No file changes — validation only.** Determinate Systems' native-linux-builder is available to the author, so most verification happens locally on the macOS dev machine without touching CI. CI remains the final confirmation that the workflow YAML changes are correct in a GHA environment.

**Local verification (primary — do all of this before the first CI run):**

- `nix build .#dev` — repeat ≥ 10× to surface flaky tests that silently passed outside the sandbox.
- `nix build .#dev.debug` — confirm debug symbols still produced (separateDebugInfo + multi-output interaction).
- `nix build .#dev^coverage` — confirm `result-coverage/coverage.xml` is produced and ingestable by a local `codecov --dry-run` or at minimum parses as valid XML.
- `nix build .#default` — ensure RelWithDebInfo variant stays green.
- `nix build .#release` — ensure `enableTests = false` → `doCheck = false` path works.
- `nix build .#sanitized` — ASan+UBSan-instrumented tests run in sandbox. Verify against a representative issue from today's `sanitizer.yml` history (e.g. reproduce a past sanitizer finding by checking out the pre-fix commit).
- `nix build .#fuzz` — confirm `result/bin/iiif_handler_uri_parser_fuzz` is produced. Run with `-runs=1` against a trivial corpus to confirm the binary executes under the local machine.
- `nix build .#static-amd64`, `nix build .#static-arm64` — confirm Zig-in-Nix cross-build still works through the native-linux-builder.
- `nix build .#release-archive-amd64`, `nix build .#release-archive-arm64` — confirm tarball + checksum + debug artifact layout.
- `nix build .#docker` — confirm OCI image build.
- `nix develop` — verify the dev shell opens and `cmake --version` / `clang --version` work (dev-shell `inputsFrom = [ pkgs.sipi ]` interaction).
- Run `just nix-build && just rust-test-e2e && just hurl-test` end-to-end against the result; verify `SIPI_BIN` resolution works.
- Grep audits (local, zero GHA runs): `rg 'cmake ' justfile | rg -v '^#' | rg -v 'cmake-'` → should be empty; `rg '\./build/sipi' justfile .github/workflows/ docs/src/development/` → should be empty outside the "Inner-loop development" doc snippet.

**CI verification (final gate — push once all local checks pass):**

- `gh workflow run test.yml` on the feature branch: nix-clang / nix-static / nix-macos-audit green.
- `gh workflow run sanitizer.yml`: green.
- `gh workflow run fuzz.yml`: green (build step at minimum; fuzzer runtime is a time budget, not a build metric).
- `gh workflow run loadtest.yml`: green, `sipi.log` + `sipi.pid` supervision still working.
- `publish.yml` green on a dry-run (if feasible) or demonstrably correct by inspection. Mac-local can't exercise publish.yml's full matrix (it pushes to Docker Hub + GitHub Releases), so rely on the in-job `nix build .#release-archive-*` step completing — which we've already verified locally.
- Measure `nix-clang` amd64 build+test duration (expected: ≤ 780 s on warm Cachix, ≤ 873 s cold).
- Measure `sanitizer.yml` duration (expected unchanged ±10%).
- Verify `nix-static` amd64 duration is unchanged (±5%).
- Update DEV-5939's Linear description to note the precondition is met.

**Why this split matters:** previously, verifying a `packages.sanitized` change meant pushing to a feature branch and watching CI take 15 minutes per iteration. With the native-linux-builder, iteration is seconds-to-minutes locally. Only the irreducibly CI-bound concerns (workflow YAML syntax, GHA action versions, artifact upload paths, Codecov integration) need a CI round-trip. Design the work as local-first iteration + single final CI push, not the old "commit, push, wait 15 min, repeat" loop.

### Commit Organization (pre-merge cleanup)

Follow `docs/src/development/commit-conventions.md`. The branch is expected to accumulate messy intermediate commits during implementation (experimentation, revert-and-retry, reviewer-prompted adjustments). **As the last step before opening or merging the PR, rewrite the branch history into a clean commit sequence.**

Target final shape (one commit per row; consolidate further if any ends up trivial):

| Type | Subject | Contents |
|---|---|---|
| `build` | `package.nix: add sanitized + fuzz variants, doCheck + coverage output` | `package.nix` + `flake.nix` additions: `doCheck = enableTests`, multi-output `coverage`, `enableSanitizers` / `enableFuzzing` parameters, `packages.sanitized`, `packages.fuzz`. |
| `build` | `justfile: replace imperative recipes with nix-build wrappers` | The full `justfile` rewrite. |
| `ci` | `workflows: route every build step through justfile` | All `.github/workflows/*.yml` migrations. One commit for all workflows unless they're large enough to warrant splitting (judgment call during cleanup). |
| `docs` | `document inner-loop dev pattern and nix-first recipes` | `CLAUDE.md`, `CONVENTIONS.md`, `README.md`, `docs/src/development/*.md`, `config/sipi.localdev-config.lua`. |

All commits use hidden types (`build`, `ci`, `docs`) — the change is invisible to Sipi deployers (no user-facing behavior change in the shipped binary). Per `commit-conventions.md` rule 2, hidden-type commits squash aggressively: the four rows above are the maximum reviewability-friendly shape, not a lower bound. If any commit ends up under ~50 lines of substantive diff, fold it into an adjacent one.

The PR description follows the template in `commit-conventions.md` (Motivation / Summary / Key Changes / Challenges and Decisions / Gotchas / Test Plan), with `Fixes DEV-6280` on the first line.

**Cleanup procedure:**

1. Before opening the PR, run `git log --oneline` and sketch which in-flight commits belong to which of the four buckets above.
2. Use `git rebase -i <base>` to reorder, squash, and reword into the final sequence. Drop WIP messages, merge fixups into their parent, consolidate same-concern commits.
3. Run the full CI once more on the cleaned branch to confirm each commit builds on its own (a future `git bisect` could land on any of them).
4. Open the PR, or — if already open — `git push --force-with-lease`.

## Alternative Approaches Considered

### A. Keep imperative cmake, add GHA-level cache over `./build/ext_build/`

Keep the current `just nix-build` imperative recipe. Add `actions/cache@v4` (or `nix-community/cache-nix-action`) keyed on `cmake/dependencies.cmake` + vendor archive hashes to cache `./build/ext_build/`.

**Why rejected:**

- Delivers a performance win for nix-clang without touching the derivation graph.
- But: reinforces the imperative pattern that DEV-5939 needs to retire. Makes the per-dep-derivation migration harder because it normalizes cmake-as-source-of-truth for ext/ builds.
- Leaves the justfile/CI drift unresolved.
- Adds another cache layer (GHA + Cachix) with independent eviction policies — maintenance burden.

### B. Expose both `nix-*` (derivation) and `cmake-*` (imperative) recipes in the justfile

Keep today's imperative paths under an honest `cmake-*` namespace, alongside the derivation-backed `nix-*` recipes. Two ways to build, both recipes.

**Why rejected (explicit user decision, 2026-04-20):**

- A recipe is a contract: "CI runs this too." A `cmake-*` recipe that CI cannot run violates the contract and creates a drift surface where the two paths diverge over time.
- Humans already doing ad-hoc incremental rebuilds are committing to a non-reproducible workflow; documenting the pattern (dev shell + manual `cmake --build`) is the honest signal that it's local-only.
- If the inner-loop cmake invocation genuinely needs to be the same across a team (e.g. non-trivial CMake flags), that's evidence the configuration deserves a Nix variant in `flake.nix` — not a recipe wrapper around imperative cmake.
- The dev-shell inner loop remains available; it's just explicit and documented, not hidden behind a recipe that pretends to be part of the justfile's contract.

### C. Introduce a `.#coverage` flake output instead of a multi-output

Create `packages.coverage = runCommand … { gcovr … }` that depends on `.#dev`. Separate derivation for separate artifact.

**Why rejected (weakly):**

- Cleaner separation, but requires a second full test run just for coverage (or complex output-sharing).
- Multi-output keeps the tested build and the coverage report content-addressed together — Cachix substitutes both on a hit, one build on a miss.
- For sipi (Codecov runs on every amd64 PR), the multi-output wins. If coverage-gating becomes rare, revisit.

### D. Flip `doCheck = true` but keep `just nix-test` as separate recipe

Run tests both inside the derivation AND as a separate recipe. Redundant but compatible.

**Why rejected:**

- Once `just nix-build` wraps `nix build .#dev`, the derivation build tree is garbage-collected after the Nix sandbox exits — `cd build && ctest` has no `build/` to `cd` into. Keeping `nix-test` would require extracting test binaries as a third output (`$test`) or copying them to `$out/test/`, adding derivation complexity for zero signal improvement.
- Even if feasible, tests would run twice per CI run: once in sandbox, once against `./result/bin/sipi`. Slower CI, no value — if sandbox tests pass, the extra run adds nothing; if they fail, the build is already red.

## Acceptance Criteria

### Functional Requirements

- [ ] `package.nix` has `doCheck = enableTests` and `outputs = [ "out" "coverage" ]`; `.#dev` and `.#default` both run unit tests as part of their check phase; `.#release` does not.
- [ ] `flake.nix` exposes new `packages.sanitized` and `packages.fuzz` outputs.
- [ ] `nix build .#dev` fails if any unit test fails.
- [ ] `nix build .#dev^coverage` produces `result-coverage/coverage.xml` ingestable by Codecov.
- [ ] `nix build .#sanitized` runs ASan+UBSan-instrumented tests in the sandbox.
- [ ] `nix build .#fuzz` produces `result/bin/iiif_handler_uri_parser_fuzz`.
- [ ] Justfile contains zero imperative `cmake` invocations in recipes. Every build-related recipe wraps `nix build .#<variant>`.
- [ ] Every `.github/workflows/*.yml` step that builds/tests sipi invokes `just <recipe>` (no inline cmake or nix build calls).
- [ ] `just rust-test-e2e`, `just hurl-test`, `just nix-run`, `just nix-valgrind` resolve the binary via `SIPI_BIN` env var with default `./result/bin/sipi`.
- [ ] `fuzz.yml` and `publish.yml` inline commands are replaced with `just` recipes (`nix-build-fuzz`, `nix-run-fuzz`, `nix-build-release-archive-<arch>`).
- [ ] `CLAUDE.md` and `docs/src/development/developing.md` document the dev-shell inner-loop pattern; no `cmake-*` recipe is introduced.
- [ ] Dockerfile is unchanged (explicitly out of scope).

### Non-Functional Requirements

- [ ] `nix-clang / amd64` CI wall-clock time: **cache-hit runs ≤ 780 s**; cache-miss runs at or below baseline (873 s). Cache hits apply when `.#dev` closure hash is unchanged — i.e. every run where neither `package.nix`, `flake.nix`, `flake.lock`, nor `cmake/dependencies.cmake` changed.
- [ ] `nix-static / amd64` CI wall-clock time is within ±5% of today's (952 s). Same commands underneath, now wrapped in `just`.
- [ ] Local `just nix-build` on a warm Cachix completes in < 5 min for a clean clone.
- [ ] Local dev-shell incremental rebuild (`cmake --build build` after a single `.cpp` edit, inside `nix develop`) completes in < 30 s.

### Quality Gates

- [ ] `test.yml` green on the feature branch, for at least one full run.
- [ ] `sanitizer.yml`, `fuzz.yml`, `loadtest.yml` green on manual dispatch on the feature branch.
- [ ] `publish.yml` green on a dry-run (if feasible) or demonstrably correct by inspection.
- [ ] All docs under `docs/src/development/` reflect the new taxonomy.
- [ ] `CLAUDE.md` Quick Reference section updated.
- [ ] No grep hit for `./build/sipi` in `justfile`, `.github/workflows/`, or `docs/src/development/` after the docs update.
- [ ] Branch history has been rewritten per the Commit Organization section before the PR is opened (or before it is merged, if reviewer feedback accumulates new commits).

## Success Metrics

| Metric | Today | Target | Measurement |
|---|---|---|---|
| nix-clang / amd64 build+test duration | 873 s | ≤ 873 s (warm cache: ≤ 780 s) | `gh run list --workflow=test.yml` |
| nix-static / amd64 duration | 952 s | 900–1000 s (unchanged ±5%) | same |
| Inline build commands in `.github/workflows/*.yml` (excluding benchmark drivers like `wrk`) | 10 | 0 | grep audit; see Problem Statement §1 for the full list |
| Imperative `cmake` invocations in justfile recipes | 6+ (today's `nix-build`, `nix-build-sanitized`, `build`, `nix-coverage`, `nix-coverage-html`, `nix-test-sanitized`) | 0 | `just --list` + recipe-body inspection |
| Derivation-backed build variants in flake.nix | 7 (default, dev, release, static-*, release-archive-*, docker, docker-stream) | 9 (+ sanitized, fuzz) | `nix flake show` |
| Hardcoded `./build/sipi` / `./build-sanitized/sipi` / `./build-fuzz/` refs in justfile and CI | 15+ | 0 | grep audit |

## Dependencies & Prerequisites

- `01-feat-nix-unified-build-plan.md` (the initial unified-build phase) implemented — ✅ done (merged sipi PR #558).
- `DEV-6027` loadtest startup hardening — ✅ merged sipi PR #564 (`0392dfb`).
- Cachix `dasch-swiss` cache — ✅ live since 2024-04-30.
- `DASCHBOT_PAT` secret with impure-env — ✅ already configured for nix-static job.
- No external blockers.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| `doCheck = enableTests` surfaces flaky tests that silently pass outside the sandbox | M | M | During implementation, run `nix build .#dev` repeatedly (≥ 10×) **locally via the native-linux-builder** (no CI round-trips needed). Any flaky test gets either fixed or `disabled_` via gtest filter with a Linear ticket. |
| ApprovalTests writes `.received.txt` files outside `build/` and fails under Nix sandbox | M | M | The package/flake task list has an explicit pre-flip probe as the first verification step — **run locally via native-linux-builder** before anything else in the workstream, so sandbox incompatibility surfaces in minutes, not after a CI round-trip. If it fails, use the `-LE approval` ctest label to skip ApprovalTests in sandbox and retain them as a dev-shell-only invocation; document as a known constraint. |
| `.#release` tries to run tests because `doCheck` is unconditional | M | H | Plan specifies `doCheck = enableTests` (conditional). `.#release` sets `enableTests = false` → `doCheck = false`. `mkStaticBuild` is independent (fresh derivation in `flake.nix:127`), so `static-*` and `release-archive-*` are unaffected. |
| Cold-cache CI for `.#dev` fails because `GH_TOKEN` is missing from migrated workflow steps | L | M | CI migration checklist: verify every `run:` step invoking `just nix-*` recipes has `env: { GH_TOKEN: ${{ secrets.DASCHBOT_PAT }} }`. Today `test.yml:65` and `loadtest.yml:53` already do; verify sanitizer, fuzz, publish steps during the CI migration task. |
| Hermetic sandbox breaks tests that assumed network / writable HOME / /tmp behaviors | M | M | Use `checkPhase` or `NIX_HARDENING_ENABLE` overrides only as last resort; prefer fixing the test. Document any sandbox-specific test skips in `package.nix` with a comment. |
| `separateDebugInfo` + multi-output `coverage` collide (package.nix:118 already uses `separateDebugInfo`) | L | L | Nixpkgs supports multiple outputs + `separateDebugInfo` — the `debug` output is auto-added. Verify both `nix build .#dev.debug` AND `nix build .#dev^coverage` produce expected artifacts before merge. |
| Cache miss on first post-merge CI run makes all PRs slow for a day | M | L | Schedule the merge on a low-activity day. Alternatively: seed Cachix from a local `nix build .#dev` + `cachix push dasch-swiss result` before merging. |
| codecov reads `result-coverage/coverage.xml` incorrectly (e.g., missing / wrong base path) | L | L | The codecov config uses the relative symlink path. If codecov-action@v6 doesn't follow the symlink, fall back to copying: `cp result-coverage/coverage.xml build/coverage.xml` before the upload step. |
| Dev inner-loop iteration frustration (no fast recipe, must drop into `nix develop` + cmake by hand) | M | M | Docs make the inner-loop workflow prominent in `CLAUDE.md` and `developing.md`. Native-linux-builder (now available) means devs who want Linux-target artifacts no longer need CI round-trips — the pain surface is narrower than initially scoped. If friction proves unacceptable post-merge, escalate by adding a Nix derivation variant (not a `cmake-*` recipe). |
| Fresh-clone dev tries `cmake --build build` in dev shell without running `just kakadu-fetch` first | M | L | Docs call out the `kakadu-fetch` prerequisite next to the dev-shell inner-loop snippet. |
| Rust e2e / Hurl recipe `SIPI_BIN` guard change breaks sanitizer CI path | L | L | Sanitizer CI now uses `$PWD/result/bin/sipi` (the Nix-built sanitizer binary). No hardcoded `./build-sanitized/sipi` anywhere. |
| `fuzz.yml` issue-body reproduction snippet diverges from the new recipe name | L | L | The CI migration task explicitly updates the heredoc to reference `just nix-build-fuzz`. |
| `.#sanitized` derivation's `checkPhase` behaves differently from today's imperative `ctest` w.r.t. ASan suppressions / fail-fast behavior | M | M | Verify by running `.#sanitized` against the current sanitizer test corpus during implementation; any behavior divergence gets a targeted `ASAN_OPTIONS` passthrough in `package.nix`. |
| `.#fuzz` derivation builds but `nix-run-fuzz` can't exec the binary (sandbox vs runner differences) | L | M | `.#fuzz` only builds the fuzzer binary; running it is post-build via `just nix-run-fuzz` outside the sandbox. Verify the binary runs under the runner by invoking it with `-runs=1` against a trivial corpus. |
| Pre-merge history rewrite introduces CI breakage on an intermediate commit | L | L | After the final `git rebase -i`, push to the feature branch and let CI run end-to-end before merging. If a commit turns out non-buildable in isolation, fold it into an adjacent commit. |
| Dev shell `inputsFrom = [ pkgs.sipi ]` (flake.nix:345) breaks after `package.nix` parameter additions | L | L | `inputsFrom` copies `buildInputs` + `nativeBuildInputs`; the new `enableSanitizers` / `enableFuzzing` parameters don't add inputs, they add CMake flags and phase overrides. Verify by running `nix develop` after the package changes and confirming the dev shell opens and basic tools (`cmake --version`, `clang --version`) work. |
| Local devs invoke `just nix-build-static-*` without `configurable-impure-env` enabled in their `nix.conf` | M | L | The recipe passes `--extra-experimental-features "configurable-impure-env"` per-invocation so `nix.conf` does not need to be pre-configured. Document in `CLAUDE.md`/`building.md` as a "once per machine" setup hint anyway. |

## Resource Requirements

- **Team:** 1 developer (Ivan). ~3-4 days total on a single feature branch (reduced from the earlier ~4-5 day estimate; the native-linux-builder's local iteration shaves a day of CI-round-trip time).
- **Infrastructure:** Cachix `dasch-swiss` account (existing). Determinate Systems native-linux-builder (available to author as of 2026-04-20) — used for all Linux-target `nix build` variants during local verification.
- **Approvals:** Self-reviewed; PR-level review by reviewer-of-the-day.

## Future Considerations

- **DEV-5939 (split ext/ into per-dep derivations).** This plan is the precondition. After it lands, DEV-5939 can convert each `ext/*/CMakeLists.txt` into a top-level `mkDerivation` in `flake.nix`, with the sipi derivation gaining `buildInputs = with pkgs; [ libtiff-sipi kakadu-sipi exiv2-sipi … ]`. Bumping libtiff then rebuilds libtiff + sipi only, not the whole ext/ tree.
- **Dockerfile migration to `nix build .#docker`.** Deferred. Once done, deploy pipeline consumes a Nix-built image; matches local-dev exactly.
- **~~Native-linux-builder adoption~~ Already realized.** Available as of 2026-04-20; see §E (Verification) which has been restructured around local-first validation.
- **Remote Nix cache warming on merge to main.** Post-migration, a `.github/workflows/cache-warm.yml` could run `nix build .#{default,dev,static-amd64,static-arm64,release-archive-amd64,release-archive-arm64,docker}` on merge and push to Cachix. This makes the first PR on any new branch a cache-hit.
- **`configurable-impure-env` stability.** The experimental feature is marked experimental in Nix 2.x and required by the Kakadu FOD's `impureEnvVars`. If Determinate Nix (or upstream Nix) graduates or renames the flag, `nix-build-static-*` / `nix-build-release-archive-*` recipes and the Determinate Nix Action config (`test.yml:111-112`) need a coordinated update. Not a blocker — just noting this is a known external dependency on an evolving feature name.

## Documentation Plan

| Doc | Change |
|---|---|
| `CLAUDE.md:24-40` (Quick Reference) | Replace the flat list with sections: "Nix build (reproducible, what CI runs)", "Dev-shell inner loop (local only — not a recipe)", "Tests (consume `SIPI_BIN`)", "Docker (unchanged)". |
| `CONVENTIONS.md` | Add "Build reproducibility invariant" section: "Every justfile recipe that builds sipi goes through `nix build .#<variant>`. CI invokes only `just <recipe>`. Incremental inner-loop development is a documented dev-shell pattern (`nix develop` + `cmake --build build` by hand), not a recipe." |
| `docs/src/development/building.md` | Rewrite Nix section around `nix build .#<variant>` and `just nix-*` recipes. |
| `docs/src/development/developing.md` | Update path references from `./build/sipi` to `./result/bin/sipi` (or `just nix-run`). Add the "Inner-loop development" snippet from §D. |
| `docs/src/development/ci.md` | Document the "CI invokes justfile only" rule. |
| `config/sipi.localdev-config.lua:5` | Update comment to point at `./result/bin/sipi` or `just nix-run`. |

## References & Research

### Internal References

- **Prior plan (implemented):** `dasch-specs/specs/2026-04-16-sipi-nix-unified-build/01-feat-nix-unified-build-plan.md`
- **Core files touched by this plan:**
  - `sipi/package.nix` (`doCheck = enableTests`, multi-output `coverage`, new `enableSanitizers` and `enableFuzzing` parameters)
  - `sipi/flake.nix` (adds `packages.sanitized` and `packages.fuzz` outputs)
  - `sipi/justfile` (full recipe rewrite — imperative recipes deleted, replaced with Nix-derivation wrappers; new recipes added for inlined-today CI steps)
  - `sipi/.github/workflows/test.yml` (migrate nix-clang, nix-static, nix-macos-audit steps)
  - `sipi/.github/workflows/sanitizer.yml:55,63,71-75` (migrate inline ctest + cargo test)
  - `sipi/.github/workflows/fuzz.yml:73-81,93-110` (migrate inline cmake + fuzzer)
  - `sipi/.github/workflows/publish.yml:195-198` (migrate release-archive build)
  - `sipi/.github/workflows/loadtest.yml:57-84` (update to use SIPI_BIN / unified recipe)
  - `sipi/CMakeLists.txt:217-232,563-564` (coverage instrumentation — unchanged, just noted)
  - `sipi/CLAUDE.md:11,24-40`, `sipi/CONVENTIONS.md`, `sipi/docs/src/development/{building,developing,ci}.md`
- **Rust test harness SIPI_BIN resolution:** `sipi/test/e2e-rust/src/lib.rs:40-42`
- **Flake output graph:** `sipi/flake.nix:221-243` (`packages.{default,dev,release,static-*,release-archive-*,docker,docker-stream}`)
- **CLAUDE.md invariant violated by current drift:** `sipi/CLAUDE.md:11` ("All targets are in a single `justfile`.")

### External References

- **Nixpkgs multi-output convention:** [Nixpkgs Manual §6.4 "Multiple-output packages"](https://nixos.org/manual/nixpkgs/stable/#chap-multiple-output). Standard outputs: `out`, `bin`, `dev`, `doc`, `debug`, `lib`, `man`. `coverage` is a natural extension.
- **Nixpkgs `doCheck` guidance:** [Nixpkgs Manual §13 "Quality Assurance"](https://nixos.org/manual/nixpkgs/stable/#chap-quality). Default is `doCheck = true`; disable only for network / flaky / long tests.
- **Determinate Systems native-linux-builder:** <https://docs.determinate.systems/troubleshooting/native-linux-builder/> — unlocks macOS → Linux-target builds, reinforces "CI and local run the same commands."
- **Cachix multi-output behavior:** separate outputs are separately content-addressed and substitutable; `$coverage` output is fetched only if a consumer references it.

### Institutional Learnings

- `dasch-specs/learnings/build-errors/cmake-externalproject-cross-compile-zig-autotools.md` — ExternalProject dependencies don't inherit parent toolchain settings. Relevant for DEV-5939 (the split-ext/ follow-up) more than for this plan, but worth keeping in mind for the `nix-build-static-*` impure-env handling.
- `dasch-specs/learnings/design-decisions/multi-arch-static-build-ci-docker-native-per-arch.md` — native per-arch CI avoids emulation fragility. Reinforces the "let CI matrix set the arch, don't cross-compile" stance this plan inherits.
- `dasch-specs/learnings/build-errors/non-deterministic-build-inputs-cause-recompilation.md` — any non-determinism in build inputs kills cache hits. Applies to `SIPI_BIN` env-var resolution and to `result-coverage/coverage.xml` timestamp handling.
- `dasch-specs/learnings/configuration-errors/github-actions-composite-action-main-ref-pr-isolation.md` — composite actions @main break PR isolation. The CI migration task should review whether any migrated workflow step pulls an action @main that could bypass the feature branch.
