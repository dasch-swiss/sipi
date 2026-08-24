---
title: "feat: Improve sipi test coverage with Rust e2e harness and systematic unit testing"
type: feat
date: 2026-03-08
author: "subotic"
status: implemented
repository: dasch-swiss/sipi
---

# Improve sipi test coverage with Rust e2e harness and systematic unit testing

## Overview

Systematically improve sipi's test coverage from 24.3% to 50-60% through a three-pronged approach: (1) a Rust-based e2e test harness that survives the planned Rust migration, (2) targeted C++ unit tests for pure-logic components, and (3) improved CI coverage enforcement. The plan prioritizes investment that compounds across the migration rather than dead-end C++ infrastructure.

## Problem Statement / Motivation

Sipi currently has 24.3% code coverage with significant blind spots:

- **shttps/ HTTP framework** (~398KB total): zero unit tests — the entire HTTP server, connection handling, parsing, JWT, and Lua-SQLite bridge are untested
- **SipiHttpServer.cpp** (1796 lines): zero direct tests — the core IIIF endpoint logic
- **SipiLua.cpp** (1949 lines): zero unit tests — all Lua bindings
- **Metadata handlers** (ICC 582, Essentials 213, XMP 166, Exif 144, IPTC 59 lines): zero unit tests
- **IIIF parsers** (SipiRotation, SipiQualityFormat, SipiIdentifier): zero unit tests
- **Fuzz testing**: placeholder only — the fuzz target never calls production code
- **ApprovalTests**: only a trivial `verify(42)` placeholder

The existing Python pytest e2e tests (`test/e2e/`) provide valuable integration coverage but are a dead-end investment: they cannot be reused after the Rust migration, the `conftest.py` has grown complex (587 lines of process management), and they depend on external tools (nginx, ImageMagick, psutil, ab).

**Coverage from the last PR (#516)**: 24.3% project, 30.6% diff — the Codecov target is 85-100% but enforcement is weak (1% threshold, no patch target).

## Proposed Solution

### Strategy: Invest forward, not backward

1. **Rust e2e harness** — write once, reuse after migration. Tests the HTTP API contract (same regardless of implementation language). Uses `reqwest` (blocking) + `insta` for snapshot testing.
2. **C++ unit tests for pure logic** — IIIF parsers, filename hash, metadata extraction. Small, isolated, high-value.
3. **Do NOT invest in**: C++ integration tests for `shttps/` or `SipiHttpServer` (both will be replaced during Rust migration). Let the Rust e2e harness cover these.

### Coverage methodology

**Denominator**: `src/` + `shttps/` + `include/` (header-defined inline functions), excluding `src/SipiLua.cpp` (Lua bindings, will be removed in Rust migration) and `src/Salsah.cpp` + `src/PhpSession.cpp` (legacy integrations).

**Target**: 60% line coverage from combined unit + e2e tests. E2e tests count — the sipi binary must be compiled with `--coverage` for both test types.

**Enforcement**: Codecov patch coverage at 80% for new/changed code. Project coverage floor with 1% drop threshold.

## Technical Considerations

### Build system: shared test library

The current pattern of re-listing 35+ source files per test binary (see `test/unit/sipiimage/CMakeLists.txt` lines 23-102) is the biggest friction for adding tests. **Create a `libsipi_testable` static library target** that compiles all production sources once, then link each test binary against it.

```cmake
# In test/CMakeLists.txt — new shared library
# IMPORTANT: Exclude src/sipi.cpp (contains main()) to avoid duplicate symbol errors
file(GLOB SIPI_SRC
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/*.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/*.cpp
    ${CMAKE_SOURCE_DIR}/src/handlers/*.cpp
    ${CMAKE_SOURCE_DIR}/src/iiifparser/*.cpp
    ${CMAKE_SOURCE_DIR}/shttps/*.cpp
)
list(REMOVE_ITEM SIPI_SRC ${CMAKE_SOURCE_DIR}/src/sipi.cpp)   # has main()
list(REMOVE_ITEM SIPI_SRC ${CMAKE_SOURCE_DIR}/shttps/Shttp.cpp) # manual test program

add_library(libsipi_testable STATIC ${SIPI_SRC})
target_include_directories(libsipi_testable PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/shttps
    # ... external dep include dirs
)
target_link_libraries(libsipi_testable PUBLIC
    jansson lcms2 exiv2 lua sentry prometheus_core
    # ... all external libraries
)
if (CODE_COVERAGE)
    target_link_libraries(libsipi_testable PUBLIC coverage_config)
endif()
```

Then each test binary simply links against `libsipi_testable`:

```cmake
target_link_libraries(${TEST_NAME} libgtest libsipi_testable)
```

### IIIF parser dependency coupling

The spec reviewer identified that `SipiIdentifier` depends on `shttps::urldecode` (from `Connection.cpp`), and `SipiRotation` depends on `shttps::Parsing::parse_float` (which requires libmagic). The `libsipi_testable` approach solves this — all dependencies are linked once. No need to mock or refactor these internal dependencies.

### Disabled `BangMaxdim` test

`test/unit/iiifparser/sipisize.cpp` lines 36-48 has a DISABLED test: "Breaks differently on different architectures in CI/docker. Probably indicates a bug." This should be investigated and fixed before adding more IIIF parser tests, as it may indicate a systemic issue in `SipiSize::MAXDIM` parsing.

### Rust toolchain provisioning

Add Rust to the Nix flake (`flake.nix`) for local development. In CI, use `dtolnay/rust-toolchain` action. The Rust e2e harness is a standalone crate that only needs to spawn the sipi binary — it does not link against C++ code.

### E2e coverage collection

The plan states e2e tests count toward coverage. For this to work:
1. The sipi binary must be built with `CODE_COVERAGE=ON` (already true for the `nix-build` target)
2. After the e2e test suite terminates sipi (via SIGTERM), the process must flush `.gcda` coverage data files. GCC's `--coverage` flag registers an `atexit` handler that writes `.gcda` on clean exit — `SIGTERM` with graceful shutdown should work, but `SIGKILL` will lose data.
3. The `gcovr` command must run AFTER both unit tests AND e2e tests complete, so it collects `.gcda` files from both.
4. The current Makefile separates `nix-test` (ctest) from `nix-test-e2e` (pytest). The `nix-coverage` target (line 168) runs `gcovr` which collects whatever `.gcda` files exist. Ensure the CI workflow runs: `nix-test` → `nix-test-e2e` → `nix-coverage` in that order.

### SSL/TLS testing

The Python e2e tests exercise HTTPS via `sipi_ssl_base_url` (port configured in `config.ini`). The Rust harness must also test TLS:
- Use `reqwest::Client::builder().danger_accept_invalid_certs(true)` for self-signed certs from `test/_test_data/certificate/`
- Port SSL-dependent tests (authentication, session cookies) to the Rust harness
- Hurl supports `--insecure` for self-signed certs

### nginx dependency

The Python `conftest.py` starts nginx alongside sipi for proxy/permission testing (lines 198-210). Options for the Rust harness:
- **Phase 3 approach**: Skip nginx-dependent tests initially. Most server tests don't require nginx — only permission/proxy tests do.
- **Later**: Add nginx lifecycle management to the Rust harness, or replace nginx-dependent tests with direct sipi requests that mock the permission endpoint.

### CI workflows

CI workflows exist on `origin/main` (`test.yml`, `docker-build.yml`, `publish.yml`). Coverage is collected on the `nix-gcc amd64` job via `gcovr` and uploaded to Codecov. The Rust e2e tests need a new CI job or integration into the existing test workflow.

## Implementation Approach

### Phase 1: Foundation (Week 1-2)

**1.1 Create `libsipi_testable` CMake target**

Extract the repeated source file lists from `test/unit/sipiimage/CMakeLists.txt` into a shared static library. Update all existing test CMakeLists.txt to link against it. Verify all existing tests still pass.

**1.2 Enhance Codecov configuration**

Update `.github/codecov.yml`:

```yaml
coverage:
  range: 70..100
  status:
    project:
      default:
        target: auto
        threshold: 1%
    patch:
      default:
        target: 80%
        threshold: 5%

ignore:
  - "fuzz/**"
  - "test/**"
  - "build/**"
  - "ext/**"
  - "vendor/**"
  - "scripts/**"
  - "config/**"
  - "src/SipiLua.cpp"
  - "src/Salsah.cpp"
  - "src/PhpSession.cpp"

parsers:
  gcov:
    branch_detection:
      conditional: yes
      loop: yes
      method: no
      macro: no
```

**1.3 Scaffold Rust e2e harness**

Create `test/e2e-rust/` with:

```
test/e2e-rust/
  Cargo.toml
  src/
    lib.rs           # SipiServer (process lifecycle), helpers
  tests/
    common/mod.rs    # Shared setup (port allocation, client)
    smoke.rs         # Basic: start server, GET /server/test.html, stop
```

The server harness must handle:
- Starting sipi binary with a test config (reuse `test/_test_data/config/sipi.fake-knora-test-config.lua`)
- Polling for readiness (TCP connect to port)
- Graceful shutdown (SIGTERM)
- Port allocation (atomic counter to avoid conflicts when tests run in parallel)
- Test data paths (relative to `test/_test_data/`)

**Cargo.toml dependencies:**

```toml
[dependencies]
reqwest = { version = "0.12", features = ["blocking", "json"] }
serde = { version = "1", features = ["derive"] }
serde_json = "1"
insta = { version = "1.46", features = ["json", "redactions"] }
tempfile = "3"
nix = { version = "0.29", features = ["signal"] }
```

**1.4 Add Makefile targets**

```makefile
rust-test-e2e:
	cd test/e2e-rust && SIPI_BIN=$(CURDIR)/build/sipi cargo test

# Hurl tests require a running sipi instance. Start one, run tests, stop it.
# All commands must run in one shell so kill can reach the backgrounded process.
hurl-test: build
	./build/sipi --config test/_test_data/config/sipi.fake-knora-test-config.lua & \
	  SIPI_PID=$$!; \
	  sleep 2; \
	  cd test/hurl && hurl --test --insecure --variable host=http://127.0.0.1:1024 *.hurl; \
	  RESULT=$$?; kill $$SIPI_PID 2>/dev/null; wait $$SIPI_PID 2>/dev/null; exit $$RESULT
```

**1.5 Add Rust toolchain to Nix flake**

Add `rustup`, `cargo`, and `rustc` to the flake's `devShell` packages. Add `dtolnay/rust-toolchain@stable` step to the CI test workflow.

### Phase 2: Quick Wins — C++ Unit Tests (Week 2-3)

**Note:** Phase 2 depends on Phase 1.1 (`libsipi_testable`). Phase 3 (Rust e2e) depends on Phase 1.3 (scaffold) but NOT on Phase 2. Phases 2 and 3 can run in parallel once their respective Phase 1 dependencies are complete.

These are pure-logic components with minimal dependencies, easy to test once `libsipi_testable` exists.

**2.1 IIIF parser tests: SipiRotation, SipiQualityFormat, SipiIdentifier**

Add `test/unit/iiifparser/sipirotation.cpp`, `sipiqualityformat.cpp`, `sipiidentifier.cpp`. Use parameterized tests (`TEST_P`) for comprehensive input coverage:

```cpp
struct RotationTestCase {
    std::string input;
    float expected_angle;
    bool expected_mirror;
};

class SipiRotationTest : public testing::TestWithParam<RotationTestCase> {};

// Note: SipiRotation::get_rotation(float &rot) returns bool (mirror)
// and sets the rotation angle via the output parameter
TEST_P(SipiRotationTest, ParsesCorrectly) {
    const auto& tc = GetParam();
    Sipi::SipiRotation rot(tc.input);
    float angle = 0.0f;
    bool is_mirror = rot.get_rotation(angle);
    EXPECT_FLOAT_EQ(angle, tc.expected_angle);
    EXPECT_EQ(is_mirror, tc.expected_mirror);
}

INSTANTIATE_TEST_SUITE_P(Rotations, SipiRotationTest, testing::Values(
    RotationTestCase{"0", 0.0f, false},
    RotationTestCase{"90", 90.0f, false},
    RotationTestCase{"!180", 180.0f, true},
    RotationTestCase{"22.5", 22.5f, false},
    RotationTestCase{"!0", 0.0f, true}
));
```

**2.2 Investigate and fix disabled `BangMaxdim` test** [x]

Reproduce the architecture-dependent failure, diagnose root cause, fix, and re-enable the test.

**2.3 SipiFilenameHash tests**

`SipiFilenameHash` is a pure computation (`hashval = ((hashval * seed) + c) % modval` over 26 chars). The constructor takes a single string path argument. Static `setLevels()`/`getLevels()` control directory depth. Test the hash function, `filepath()`, and `operator[]`:

```cpp
TEST(SipiFilenameHash, ConsistentHashing) {
    SipiFilenameHash::setLevels(3);
    SipiFilenameHash h1("test_image.jp2");
    SipiFilenameHash h2("test_image.jp2");
    EXPECT_EQ(h1.filepath(), h2.filepath());  // Same input → same path
}

TEST(SipiFilenameHash, DifferentInputsDifferentPaths) {
    SipiFilenameHash::setLevels(3);
    SipiFilenameHash h1("image_a.jp2");
    SipiFilenameHash h2("image_b.jp2");
    EXPECT_NE(h1.filepath(), h2.filepath());
}

TEST(SipiFilenameHash, HashCharsInRange) {
    SipiFilenameHash::setLevels(3);
    SipiFilenameHash h("some_file.tif");
    for (int i = 0; i < 6; ++i) {
        EXPECT_GE(h[i], 'A');
        EXPECT_LE(h[i], 'Z');
    }
}
```

**2.4 Template and SipiCommon tests**

Small files (Template.cpp: 1.5KB, SipiCommon.cpp: 265B) — quick to test fully.

### Phase 3: Rust E2E Test Migration (Week 3-5)

Port Python e2e tests to the Rust harness in priority order. Run Python and Rust tests in parallel until parity is verified.

**Division of responsibility:**
- **Hurl** (`test/hurl/`): Simple request/response contracts — status codes, redirects, CORS headers, 404/401 responses. ~50% of current Python tests are trivially expressed in Hurl.
- **Rust harness** (`test/e2e-rust/`): Complex flows — image upload+fetch, format conversion verification, multi-step workflows, image comparison, range requests.

**3.1 Port simple tests to Hurl**

```hurl
# test/hurl/not_found.hurl
GET http://{{host}}/file-should-be-missing-123
HTTP 404

# test/hurl/cors_preflight.hurl
OPTIONS http://{{host}}/knora/test.tif/full/max/0/default.jpg
Access-Control-Request-Method: GET
Origin: http://example.org
HTTP 200
[Asserts]
header "Access-Control-Allow-Origin" == "*"
header "Access-Control-Allow-Methods" contains "GET"
```

**3.2 Port server tests to Rust**

Port `test_02_server.py` tests to `tests/server.rs`:
- Server startup verification
- IIIF info.json (use `insta` snapshot with redactions for dynamic fields)
- Image upload and retrieval
- Authentication tests
- HEAD request handling (learned from PR #516 — ensure all serve paths send headers)

**3.3 Port conversion tests to Rust**

Port `test_01_conversions.py` — ISO 15444-4 JP2 decode/encode round-trips. Image comparison strategy: use `sha2` crate for checksum-based comparison (same strategy as the Python tests), with `insta` for response metadata snapshots.

**3.4 Port range request tests to Rust**

Port `test_04_range_requests.py` — HTTP range requests, multipart ranges, boundary conditions. These are pure HTTP tests, ideal for reqwest.

**3.5 IIIF validator integration**

Port `test_03_iiif.py` — run the IIIF validator against the server. Shell out to the validator binary from Rust.

**3.6 Lua scripting endpoint tests**

A significant portion of `test_02_server.py` exercises Lua scripting endpoints (`/test_functions`, `/test_mediatype`, `/test_mimetype_func`, `/test_knora_session_cookie`, `/test_clean_temp_dir`). These represent important integration surfaces that will change during the Rust migration. Port them to the Rust harness as HTTP contract tests — the Rust tests don't need to know it's Lua behind the endpoint, they just verify the HTTP contract.

**3.7 Parity verification**

Run both Python and Rust e2e suites in CI for 2-4 weeks. Compare:
- Same test scenarios covered (feature checklist)
- Same failures detected
- Coverage contribution comparable

Once parity is confirmed, deprecate the Python tests.

### Phase 4: Deepen Coverage (Week 5-7)

**4.1 Activate fuzz testing**

Replace the placeholder in `fuzz/handlers/iiif_handler_uri_parser_target.cpp`. The actual function signature is:

```cpp
// src/handlers/iiif_handler.cpp:45
auto parse_iiif_uri(const std::string &uri) noexcept -> std::expected<IIIFUriParseResult, std::string>
```

Updated fuzz target:

```cpp
#include "iiif_handler.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);
    // parse_iiif_uri is noexcept and returns std::expected — no crash expected
    // Fuzzer should find memory safety issues, not logic errors
    auto result = handlers::iiif_handler::parse_iiif_uri(input);
    // Force use of result to prevent optimizer from eliminating the call
    if (result.has_value()) {
        volatile auto type = result->request_type;
        (void)type;
    }
    return 0;
}
```

Add a second fuzz target for HTTP request parsing (`Connection::parse_request`).

**4.2 ApprovalTests for image output**

Use ApprovalTests.cpp for image format conversion regression testing. Approved outputs stored in `test/approval/approval_tests/`. Strategy: compare output file checksums rather than raw binary (avoids platform-specific byte differences):

```cpp
#include "ApprovalTests.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
// SHA-256 or similar hash for deterministic comparison

TEST(ImageConversion, TiffToJpegMetadata) {
    SipiImage img;
    // Existing tests use relative paths from build/<test-binary-dir>
    img.read("../../../../test/_test_data/images/unit/cielab.tif");

    auto tmp = std::filesystem::temp_directory_path() / "test_output.jpg";
    img.write("jpg", tmp.string());  // format key is "jpg" not "jpeg" (see SipiImage.cpp:36)

    // Verify output metadata (deterministic across platforms)
    SipiImage result;
    result.read(tmp.string());

    std::ostringstream metadata;
    metadata << "width: " << result.getNx() << "\n";
    metadata << "height: " << result.getNy() << "\n";
    metadata << "channels: " << result.getNc() << "\n";
    metadata << "bps: " << result.getBps() << "\n";

    ApprovalTests::Approvals::verify(metadata.str());
    std::filesystem::remove(tmp);
}
```

**Note on platform-dependent output**: JPEG quality and TIFF byte order may differ across platforms. Use checksum-based comparison with a tolerance, or restrict approval tests to a single reference platform (Linux amd64).

**4.3 Metadata extraction tests**

Add unit tests for `SipiEssentials`, `SipiExif`, `SipiXmp`, `SipiIptc`, `SipiIcc` using test images from `test/_test_data/images/unit/`. These modules extract metadata from images — test with known images and assert on extracted field values.

**4.4 Hash and Parsing utility tests**

Unit tests for `shttps/Hash.cpp` (hashing utilities) and `shttps/Parsing.cpp` (HTTP parsing helpers — `parse_float`, `parse_int`, URL encoding/decoding). These are pure functions, easy to test.

## Acceptance Criteria

- [ ] Project coverage reaches 50%+ (stretch: 60%) as reported by Codecov
- [ ] Codecov patch coverage enforced at 80% for new/changed code
- [ ] `libsipi_testable` CMake target exists and all test binaries link against it
- [ ] Rust e2e harness starts sipi, runs at least 20 tests, and stops sipi cleanly
- [ ] Hurl tests cover at least 15 simple HTTP contract scenarios
- [ ] IIIF parser unit tests exist for all 5 parsers (Region, Size, Rotation, QualityFormat, Identifier)
- [ ] Disabled `BangMaxdim` test is investigated and either fixed or documented with a tracking issue
- [ ] Fuzz target calls `parse_iiif_uri()` with real fuzzer input
- [ ] `make rust-test-e2e` and `make hurl-test` Makefile targets exist
- [ ] Rust toolchain available in Nix flake and CI workflow
- [ ] Python e2e tests still pass (no regression during transition)

## Dependencies & Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Sipi process management complexity in Rust | High | Prototype early (Phase 1.3), start minimal |
| `libsipi_testable` breaks existing test isolation | Medium | Verify all existing tests pass after refactor |
| Platform-dependent image output breaks ApprovalTests | Medium | Restrict approval tests to Linux amd64, use tolerance |
| Rust toolchain adds CI build time | Low | Rust e2e crate is small, compiles fast |
| Python e2e deprecation before Rust parity | High | Run both in parallel for 2-4 weeks, checklist verification |
| `BangMaxdim` bug deeper than expected | Medium | Time-box investigation to 1 day, document if not fixable |
| shttps dependency coupling makes "quick win" tests slow to compile | Medium | `libsipi_testable` solves this — compiles once |

## Success Metrics

| Metric | Current | Target | Measurement |
|--------|---------|--------|-------------|
| Project line coverage | 24.3% | 50-60% | Codecov project status |
| Patch coverage | 30.6% | 80%+ | Codecov patch status on PRs |
| Unit test suites | 6 | 12+ | Count of test binaries registered with CTest |
| E2e test count (Rust) | 0 | 30+ | `cargo test` output |
| Hurl test count | 0 | 15+ | `hurl --test` output |
| Fuzz targets (functional) | 0 | 2 | Targets that call production code |
| Time to add a new test suite | ~30 min (copy 100 lines of CMake) | ~5 min (link `libsipi_testable`) | Developer experience |

## References & Research

- Current coverage: [Codecov PR #516](https://app.codecov.io/gh/dasch-swiss/sipi/pull/516) — 24.3% project, 30.6% diff
- Existing Codecov config: `sipi/.github/codecov.yml`
- Coverage CMake config: `sipi/CMakeLists.txt:205-216`
- Test CMake structure: `sipi/test/CMakeLists.txt`
- Python e2e test manager: `sipi/test/e2e/conftest.py:67-183`
- Fuzz target placeholder: `sipi/fuzz/handlers/iiif_handler_uri_parser_target.cpp:9-14`
- Disabled BangMaxdim test: `sipi/test/unit/iiifparser/sipisize.cpp:36-48`
- Institutional learnings:
  - `learnings/logic-errors/sipi-cache-auto-creation-head-request-empty-response.md` — HEAD request handling requires flush() on all serve paths
  - `learnings/test-failures/alphabetical-test-data-masks-ordering.md` — use non-alphabetical test data
  - `learnings/configuration-errors/github-actions-composite-action-main-ref-pr-isolation.md` — inline CI steps over composite actions
- Hurl: https://hurl.dev/ — Rust-built HTTP test runner
- Insta: https://insta.rs/ — Rust snapshot testing
- GoogleTest parameterized tests: https://google.github.io/googletest/advanced.html
