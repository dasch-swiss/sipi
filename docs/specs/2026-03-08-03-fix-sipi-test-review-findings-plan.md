---
title: "fix: Address review findings from sipi PR #519 test coverage"
type: fix
date: 2026-03-10
author: "subotic"
status: reviewed
repository: dasch-swiss/sipi
repositories:
  - sipi
---

# Address review findings from sipi PR #519 test coverage

## Overview

A 9-reviewer code review of PR #519 ("test: comprehensive IIIF Image API 3.0 Level 2 compliance tests") identified 3 critical issues, 10 warnings, and 12 suggestions. This plan addresses all findings in 11 work chunks, ordered by priority and logical grouping.

## Problem Statement / Motivation

PR #519 adds 120+ tests and substantial CI infrastructure. Before merging, three critical issues must be resolved: broken fuzz corpus persistence that silently prevents corpus accumulation across nightly runs, contradictory content-type assertions between Hurl and Rust tests that will cause CI failures, and documented-but-untracked memory management bugs. The remaining findings improve CI reliability, code quality, test maintainability, documentation completeness, and security coverage.

## Proposed Solution

Fix all findings in 11 independent work chunks, each resulting in a separate commit. Chunks are ordered to resolve blockers first, then address quality and maintainability.

## Technical Considerations

- **GitHub Actions artifact scope**: `actions/download-artifact@v4` only accesses artifacts from the current workflow run. Cross-run access requires `dawidd6/action-download-artifact` or `gh run download`.
- **Action pinning convention**: The repo uses version-tag pinning (`@v4`), not SHA pins. The `@main` reference for composite actions is a documented known issue (see `dasch-specs/learnings/configuration-errors/github-actions-composite-action-main-ref-pr-isolation.md`). Changing the pinning convention is out of scope for this PR.
- **Rust test patterns**: No parameterized test macros (rstest, test-case) are used in the codebase. The simplest fix is a helper function, not a new dependency. `insta` (snapshot testing) is already declared as a dependency but unused — activating it provides golden baseline testing critical for the long-term Rust migration.
- **Content-type resolution**: The IIIF spec says servers MAY return either `application/json` or `application/ld+json` without an Accept header. The test must match actual sipi behavior.
- **HEAD request cache bug**: A documented learning (`dasch-specs/learnings/logic-errors/sipi-cache-auto-creation-head-request-empty-response.md`) describes cache-related HEAD response issues — relevant context for the content-type investigation.

## Implementation Approach

### Chunk 1: Fix fuzz corpus persistence (Critical)

**Files:** `.github/workflows/fuzz.yml`

1. Replace corpus download step (lines 45-50) with `dawidd6/action-download-artifact@v6`:
   ```yaml
   - name: Restore previous corpus
     uses: dawidd6/action-download-artifact@v6
     with:
       workflow: fuzz.yml
       name: fuzz-corpus
       path: .fuzz-corpus-ci
       if_no_artifact_found: warn
       search_artifacts: true
     continue-on-error: true
   ```
2. Add `timeout-minutes: 60` to the `fuzz-iiif-parser` job
3. Add concurrency group:
   ```yaml
   concurrency:
     group: fuzz
     cancel-in-progress: false
   ```
4. Replace shell variable interpolation in issue creation (lines 135-164) with `--body-file`:
   ```yaml
   - name: Open GitHub issue
     if: steps.check-crash.outputs.has_crash == 'true'
     env:
       GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
     run: |
       gh issue create \
         --title "Fuzz: crash in parse_iiif_uri ($(date +%Y-%m-%d))" \
         --label "bug,fuzz" \
         --body-file build-fuzz/fuzz/handlers/crash-summary.md
   ```
5. Add input validation for `FUZZ_DURATION`:
   ```bash
   if [[ ! "$FUZZ_DURATION" =~ ^[0-9]+$ ]]; then echo "Invalid duration"; exit 1; fi
   ```

### Chunk 2: Resolve contradictory content-type assertion (Critical)

**Files:** `test/hurl/info_json.hurl`, `test/e2e-rust/tests/iiif_compliance.rs`

1. Build sipi and query the actual endpoint: `curl -s -D- http://127.0.0.1:1024/unit/lena512.jp2/info.json | grep Content-Type`
2. Fix whichever test is wrong to match actual server behavior
3. **Critical for Chunk 6:** After fixing, ensure the Rust test `info_json_content_type_default` asserts the **exact** content-type including any profile parameter (e.g., `application/ld+json;profile="http://iiif.io/api/image/3/context.json"`). The Hurl test `info_json.hurl` currently checks this profile-qualified type. Before that Hurl file is deleted in Chunk 6, the Rust tests must cover every assertion it contains (content-type with profile, width, height, type, protocol, profile fields).
4. Verify both `make hurl-test` and `make rust-test-e2e` pass

### Chunk 3: File SipiFilenameHash tracking issue (Critical)

**Files:** `test/unit/filenamehash/sipifilenamehash.cpp`

1. Create GitHub issue via `gh issue create` documenting:
   - Copy constructor (line 38 of `src/SipiFilenameHash.cpp`) doesn't copy `path`/`name`
   - `operator=` (lines 40-44) leaks old `hash` pointer
   - Recommend converting raw `std::vector<char> *hash` to value member
2. Add `// TODO(#NNN)` comments in the test file referencing the issue
3. Also file issues for the two `#[ignore]` tests:
   - `profile_link_header` — sipi claims Level 2 but never emits profile Link header
   - `id_escaped` — sipi returns 500 for filenames containing `#`
4. Add `// TODO(#NNN)` to each `#[ignore]` annotation

### Chunk 4: Rust code quality fixes

**Files:**
- `test/e2e-rust/Cargo.toml`
- `test/e2e-rust/src/lib.rs`
- `test/e2e-rust/tests/server.rs`
- `test/e2e-rust/tests/upload.rs`
- `test/e2e-rust/tests/common/mod.rs`

1. Remove unused `sha2` dep from `Cargo.toml` (zero callers). **Keep `insta`** — it's declared but not yet used; convert key tests to snapshot testing in Chunk 5 to establish a golden baseline for the Rust migration.
2. Move `tempfile` to `[dev-dependencies]`
3. Fix PID cast in `lib.rs` Drop impl:
   ```rust
   let pid = i32::try_from(self.child.id()).expect("PID overflows i32");
   ```
4. Add port overflow guard in `allocate_ports()`:
   ```rust
   let http = NEXT_PORT.fetch_add(2, Ordering::SeqCst);
   assert!(http < 65534, "port counter overflow");
   ```
5. Fix bare `.parent().unwrap()` chains in `find_sipi_bin()` and `test_data_dir()` — use `.expect("e2e-rust should be two levels below repo root")`
6. Fix lossy float conversions in `tests/server.rs`:
   - `json["width"].as_f64().unwrap() as i64` → `json["width"].as_i64().expect("width must be integer")`
   - `json["duration"].as_f64().unwrap() as f32, 4.7` → `assert!((json["duration"].as_f64().unwrap() - 4.7).abs() < 0.01)`
7. Fix upload signature: `file_path: &PathBuf` → `file_path: &Path` (add `use std::path::Path;`). Verify all callers compile — `&PathBuf` auto-derefs to `&Path` so existing call sites should work without changes.
8. Simplify `common/mod.rs`: `CLIENT.get_or_init(|| http_client())` → `CLIENT.get_or_init(http_client)`

### Chunk 5: Establish golden baseline + deduplicate Rust tests

**File:** `test/e2e-rust/tests/iiif_compliance.rs`

**Step A: Golden baseline with insta snapshots (BEFORE any deduplication)**

Convert key structural tests to `insta::assert_json_snapshot!` to freeze current server behavior. This creates approved snapshots that serve as regression guards during deduplication and as a stable golden base for the long-term Rust migration.

1. Convert `info_json_has_required_fields` to snapshot the full info.json response:
   ```rust
   use insta::assert_json_snapshot;

   #[test]
   fn info_json_golden_snapshot() {
       let json = fetch_info_json();
       assert_json_snapshot!("info-json-lena512", json, {
           ".id" => "[server_url]",  // redact dynamic server URL
       });
   }
   ```
2. Add a snapshot for a representative IIIF response headers test (content-type, CORS, Link)
3. Run `cargo insta test` to generate `.snap` files, then `cargo insta review` to approve them
4. Commit the approved `.snap` files — these are the golden baseline

**Step B: Deduplicate tests (WITH golden baseline in place)**

5. Add helper at top of file:
   ```rust
   fn assert_iiif_status(path: &str, expected: u16) {
       let srv = server();
       let resp = client()
           .get(format!("{}{}", srv.base_url, path))
           .send()
           .unwrap_or_else(|e| panic!("GET {} failed: {}", path, e));
       assert_eq!(
           resp.status().as_u16(), expected,
           "unexpected status for {}", path
       );
   }
   ```
6. Replace all simple status-code-only tests with calls to this helper (~25-30 tests)
7. Consolidate `format_gif_unsupported`, `format_pdf_unsupported`, `format_webp_unsupported`, `format_unknown` into one test with a loop
8. Remove true duplicates where same URL + same assertion exists in multiple test functions. For the 7 tests hitting `/full/max/0/default.jpg` → 200, keep **`full_iiif_url_returns_image`** (clearest name for a basic IIIF delivery test) and delete: `region_full`, `id_basic`, `rotation_0`, `quality_default`, `size_max`, `iiif_image_delivery_jpg`. The region/size/rotation/quality/format concepts are already covered by tests using non-default parameter values.
9. Standardize assertion style: always `assert_eq!(resp.status().as_u16(), N)`, not `is_success()`
10. **Verification gate:** Run `cargo test -- --test-threads=1` and confirm ≥100 tests pass AND all insta snapshots still match (no `cargo insta review` pending). If test count drops below 100, review which tests were removed to ensure no coverage was lost.

### Chunk 6: Remove Hurl/Rust test overlap

**Files:** `test/hurl/*.hurl`, `test/e2e-rust/tests/iiif_compliance.rs`

1. Remove Hurl files fully covered by Rust e2e:
   - `deny_unauthorized.hurl`
   - `not_found.hurl`
   - `head_request.hurl`
   - `iiif_image_delivery.hurl`
   - `invalid_iiif_urls.hurl`
   - `info_json.hurl`
2. Keep Hurl files testing unique scenarios: `file_access.hurl`, `lua_endpoints.hurl`, `sqlite_api.hurl`, `video_knora_json.hurl`, `missing_sidecar.hurl`
3. Remove "Phase 13" comments from Rust test file
4. Verify `make hurl-test` still passes with the remaining 5 Hurl files (the `*.hurl` glob in the Makefile target auto-adjusts)

### Chunk 7: CMake CONFIGURE_DEPENDS

**Files:** All `test/*/CMakeLists.txt` using `file(GLOB ...)`

Add `CONFIGURE_DEPENDS` to each file's existing `file(GLOB ...)` call. Note: the variable name varies (`SRCS` vs `SOURCE_FILES`) — match each file's existing convention:
- `test/approval/CMakeLists.txt`
- `test/unit/cache/CMakeLists.txt`
- `test/unit/configuration/CMakeLists.txt`
- `test/unit/filenamehash/CMakeLists.txt`
- `test/unit/handlers/CMakeLists.txt`
- `test/unit/iiifparser/CMakeLists.txt`
- `test/unit/logger/CMakeLists.txt`
- `test/unit/shttps/CMakeLists.txt`
- `test/unit/sipiimage/CMakeLists.txt`

### Chunk 8: CI efficiency

**Files:** `.github/workflows/test.yml`, `Makefile`

1. Merge the two `nix develop` steps in `test.yml` into one:
   ```yaml
   - name: Rust e2e and Hurl tests
     run: nix develop --command bash -c "make rust-test-e2e && make hurl-test"
   ```
2. Replace `sleep 2` in `Makefile` `hurl-test` target with readiness loop with failure detection:
   ```makefile
   READY=0; for i in $$(seq 1 30); do curl -sf http://127.0.0.1:1024/server/test/knora.json > /dev/null 2>&1 && READY=1 && break; sleep 0.5; done; \
   if [ "$$READY" -ne 1 ]; then echo "ERROR: sipi failed to start within 15s"; kill $$SIPI_PID 2>/dev/null; exit 1; fi; \
   ```
   Note: Use an endpoint known to return 200 under the test config (verify which endpoint is appropriate — `/server/test/knora.json` or a simple IIIF info.json).

### Chunk 9: Update developer documentation

**Files:** `docs/src/development/developing.md`, `docs/src/development/ci.md`

1. In `developing.md` "Writing Tests" section:
   - Add `test/unit/filenamehash/` and `test/unit/shttps/` to the unit test component list
   - Add subsection for Rust e2e tests (`test/e2e-rust/`), run command, `--test-threads=1` requirement
   - Add subsection for Hurl HTTP contract tests (`test/hurl/`), run command, list the 5 remaining test files and their purpose (file access, Lua endpoints, SQLite API, video knora.json, missing sidecar)
2. In `ci.md`:
   - Add mention of nightly fuzz workflow, link to `fuzzing.md`

### Chunk 10: Add path traversal prevention test

**File:** `test/e2e-rust/tests/iiif_compliance.rs`

```rust
#[test]
fn path_traversal_rejected() {
    let srv = server();
    for path in &[
        "/unit/%2E%2E%2F%2E%2E%2Fetc%2Fpasswd/full/max/0/default.jpg",
        "/unit/..%2F..%2Fetc%2Fpasswd/full/max/0/default.jpg",
    ] {
        let resp = client()
            .get(format!("{}{}", srv.base_url, path))
            .send()
            .unwrap_or_else(|e| panic!("GET {} failed: {}", path, e));
        let status = resp.status().as_u16();
        let body = resp.text().unwrap_or_default();
        assert!(
            [400, 403, 404].contains(&status),
            "path traversal should be rejected: {} returned {}",
            path, status
        );
        // Verify response body doesn't contain file content
        assert!(
            !body.contains("root:"),
            "path traversal may have leaked file content for {}",
            path
        );
    }
}
```

### Chunk 11: Minor C++ cleanup (optional)

**Files:** `test/unit/filenamehash/main.cpp`, `test/unit/shttps/main.cpp`

Replace standalone `main.cpp` boilerplate with linking `gtest_main`:
- In CMakeLists: `target_link_libraries(${TEST_NAME} gtest_main libsipi_testable)` instead of `libgtest`
- Remove `main.cpp` files
- Only if `gtest_main` is already available as a target — check `test/CMakeLists.txt` for the GoogleTest setup

## Acceptance Criteria

- [x] Fuzz corpus accumulates across nightly runs (verify with manual dispatch + check artifact contents)
- [x] No contradictory content-type assertions — both Hurl and Rust tests agree
- [x] `SipiFilenameHash` memory bugs have a tracking issue with references in test comments (DEV-6002)
- [x] `#[ignore]` tests have tracking issues (DEV-6003, DEV-6004)
- [x] `cargo test -- --test-threads=1` compiles without unused dep warnings and all tests pass
- [x] No duplicate tests asserting the same URL + status in `iiif_compliance.rs`
- [x] No Hurl files that are 100% covered by Rust e2e tests
- [x] All `file(GLOB ...)` in test CMakeLists use `CONFIGURE_DEPENDS`
- [x] `docs/src/development/developing.md` mentions Rust e2e, Hurl, and all unit test components
- [x] `docs/src/development/ci.md` mentions fuzz workflow
- [x] Path traversal test exists and passes
- [x] CI nix-gcc job has one `nix develop` invocation for Rust+Hurl tests
- [x] Hurl test uses readiness loop instead of `sleep 2`
- [x] `dawidd6/action-download-artifact` verified as actively maintained with acceptable security posture
- [x] After Chunk 5, ≥100 Rust e2e tests pass AND all insta snapshots match (verification gate)
- [x] Golden insta snapshots committed for info.json structure and key IIIF response headers
- [ ] Codecov thresholds documented as a conscious policy decision (revisit after stabilization)

## Dependencies & Risks

- **Chunk 2 (content-type)** requires a running sipi build to verify actual behavior — must build first
- **Chunk 5 (parameterize tests)** is the largest code change; refactoring 1529-line file carries risk of introducing test regressions
- **Chunk 6 (Hurl removal)** depends on Chunk 2 resolution (can't remove `info_json.hurl` until content-type is correct)
- **Chunk 11 (gtest_main)** may not work if `gtest_main` isn't available as a CMake target — skip if not
- **`dawidd6/action-download-artifact`** is a third-party action — check it's maintained and trusted before adding
- **Codecov thresholds** were lowered in this PR (range 85→70, patch 85%→80% with 5% threshold). This is a policy decision — the effective floor for new code is now 75%. Revisit once the new test suite stabilizes; consider tightening back to 80..100 range and 85% patch target

## Execution Order

1. Chunk 2 — content-type (unblocks Chunk 6)
2. Chunk 4 — Rust quality (mechanical, low risk)
3. Chunk 5 — parameterize tests (large but isolated)
4. Chunk 6 — remove Hurl overlap (depends on 2)
5. Chunk 1 — fuzz corpus (CI, independent)
6. Chunk 7 — CMake CONFIGURE_DEPENDS (one-liner per file)
7. Chunk 8 — CI efficiency
8. Chunk 9 — docs
9. Chunk 10 — path traversal test
10. Chunk 3 + 11 — GitHub issues + optional gtest_main

## Success Metrics

- All CI jobs pass (nix-gcc, zig-static matrices)
- Fuzz corpus grows across consecutive nightly runs
- `iiif_compliance.rs` reduced from ~1529 lines to ~900-1000 lines
- Zero duplicate test scenarios between Hurl and Rust e2e
- Golden insta snapshots capture current server behavior as a stable migration baseline
- No behavior changes introduced during deduplication (verified by snapshot match)

## References & Research

- Existing plans: `dasch-specs/specs/2026-03-08-sipi-test-coverage/01-feat-sipi-test-coverage-plan.md`
- Composite action learning: `dasch-specs/learnings/configuration-errors/github-actions-composite-action-main-ref-pr-isolation.md`
- HEAD response learning: `dasch-specs/learnings/logic-errors/sipi-cache-auto-creation-head-request-empty-response.md`
- Multi-arch CI learning: `dasch-specs/learnings/design-decisions/multi-arch-static-build-ci-docker-native-per-arch.md`
- dawidd6/action-download-artifact: https://github.com/dawidd6/action-download-artifact
- IIIF Image API 3.0 spec: https://iiif.io/api/image/3.0/
