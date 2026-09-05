---
title: "Sipi: Deterministic ICC Profile Creation Timestamps"
date: 2026-04-29
author: "Ivan Subotic"
status: reviewed
linear: DEV-6333
related:
  - DEV-6328 (PR 0 of the Nix-native build — discovered the non-determinism)
  - Plan 04 (Sipi Nix-native build — drives the bit-exact regression-gate motivation)
repositories:
  - sipi
---

# Sipi: Deterministic ICC Profile Creation Timestamps — Implementation Plan

## 0. Status & execution tracking

**Execution happens off the Linear issue, not this file.** Check Linear [DEV-6333](https://linear.app/dasch/issue/DEV-6333/sipi-deterministic-icc-profile-creation-timestamps-for-bit-exact) for current status, sub-tasks, blockers, and review activity.

| Source of truth | What lives here |
|---|---|
| **This plan file** (`05-feat-icc-deterministic-timestamps-plan.md`) | Design, approach, acceptance criteria, sources. Stable once `status: reviewed`; changes only via another review cycle. |
| **Linear DEV-6333** | Execution status, status transitions (Backlog → In Progress → In Review → Done), comments, sub-issues, PR links, blockers, review feedback. |

The Linear issue carries a copy of this plan in its description for convenience. The plan file is canonical for the design; Linear is canonical for the work-in-flight state.

## 1. Context

PR 0 of the Sipi Nix-native build migration ([DEV-6328 / sipi#586](https://github.com/dasch-swiss/sipi/pull/586), currently on feature branch `feature/dev-6328-sipi-nix-native-build-pr-0-drop-zig-static-path-test`, not yet merged to `main`) introduced an image-encode bit-exactness approval baseline at `test/approval/image_encode_baseline_test.cpp`. While capturing goldens, every JPEG/PNG output and every JP2-decoded output differed by exactly one byte across consecutive runs of the same binary. `cmp -l` traced every diff to the **seconds field of the ICC profile creation-date header** (bytes 24–35 of any embedded ICC profile: six big-endian `uInt16Number` fields — year/month/day/hour/min/sec — per ICC.1:2022 §4.2).

The non-determinism originates in **lcms2 profile creation, not serialization**: `cmsCreateProfilePlaceholder` (src/cmsio0.c) calls `_cmsGetTime` (src/cmsplugin.c) which wraps `time(NULL)` + `gmtime`. Every `cmsCreate*Profile*` entry point — `cmsCreate_sRGBProfile`, `cmsCreateGrayProfileTHR`, `cmsCreateLab4Profile`, `cmsCreateRGBProfileTHR` — feeds through this placeholder. The leak surfaces at `cmsSaveProfileToMem`, when the freshly-stamped header bytes are written to a buffer for the codec. lcms2 has no `SOURCE_DATE_EPOCH` support — Debian Bug #814883's request for `cmsSetHeaderCreationDateTime` was **rejected** upstream ([Little-CMS #71](https://github.com/mm2/Little-CMS/issues/71)). The blessed workaround is to overwrite bytes 24–35 directly in the serialized buffer.

SipiImage funnels every codec-bound ICC profile through one chokepoint: **`SipiIcc::iccBytes()`** (`src/metadata/SipiIcc.cpp:203, 216`). Verified callers: `SipiIOPng.cpp:552`, `SipiIOJpeg.cpp:1227`, `SipiIOTiff.cpp:1649`, six branches in `SipiIOJ2k.cpp` (lines 1019/1026/1042/1049/1060/1088), and `SipiImage.cpp:325` (preservation-metadata only, not codec-bound). All other `cmsSaveProfileToMem` callsites (in copy constructor and `operator=`) round-trip in memory and never reach a codec. A single normalization inside `iccBytes()` therefore covers every emission path regardless of how the profile was constructed.

PR 0 shipped TIFF-only goldens (5 tests, ~2.3 MB) because TIFF outputs from JPEG/TIFF inputs preserve the source ICC profile verbatim and don't trip the lcms2 path. JPEG/PNG/JP2-decode outputs need lcms2 to materialize a profile, and that's where the timestamp leaks. **Industry consensus** (libvips, ImageMagick, OpenJPEG, libjxl) is to use pixel-equivalence or strip metadata; SipiImage's existing TIFF goldens prove byte-exactness is feasible when lcms2's date is normalized, so we extend rather than abandon.

**Production behaviour is preserved.** The fix is opt-in via the `SOURCE_DATE_EPOCH` environment variable (the [reproducible-builds.org standard](https://reproducible-builds.org/specs/source-date-epoch/)). When unset (production), `iccBytes()` returns lcms2's wall-clock-stamped bytes unchanged. When set (tests, set per-test via CMake `set_tests_properties(... PROPERTIES ENVIRONMENT ...)`), the date is overwritten with the supplied epoch and the Profile ID (bytes 84–99) is zeroed. No production code path is altered.

## 2. Goals & Non-Goals

### Goals

1. **Deterministic ICC profile bytes for every codec-bound emission**, opt-in via `SOURCE_DATE_EPOCH`. Implemented as a single normalization inside `SipiIcc::iccBytes()`. When the env var is set, bytes 24–35 (creation date) are overwritten with the supplied epoch and bytes 84–99 (Profile ID) are zeroed. When unset, bytes are returned unchanged — production is bit-identical to today.
2. **Extend the approval baseline** to bit-exactly cover JPEG, PNG, and JP2-decode → TIFF outputs (the formats blocked in PR 0). Target: 12–15 total approval tests, with `SOURCE_DATE_EPOCH=946684800` (2000-01-01T00:00:00Z) injected via CMake `set_tests_properties`.
3. **JP2-encode determinism check**: include at least one `*ToJp2` golden to verify Kakadu's `jp2_colour::init()` embeds the (already-normalized) ICC bytes verbatim. The plan's chokepoint guarantees the *input* bytes to Kakadu are deterministic; the encode test confirms Kakadu doesn't reframe them.

### Non-Goals

- **Production-side determinism.** Production builds keep emitting wall-clock-stamped ICC headers — downstream consumers reading `cmsGetHeaderCreationDateTime` are unaffected.
- Upstreaming `SOURCE_DATE_EPOCH` support to lcms2 (rejected upstream; not worth a fork).
- Defensive runtime assertions on Profile ID. lcms2 issue [#181](https://github.com/mm2/Little-CMS/issues/181) shows lcms2 may rewrite tags during re-serialization, so externally-sourced profiles can present non-zero IDs through the round-trip. Scrub unconditionally when the flag is on rather than asserting.
- Computing MD5 Profile IDs ourselves.
- Pixel-tolerance / SSIM-based testing — bit-exactness is achievable and stricter.
- Stripping ICC profiles entirely from outputs.

## 3. Approach

### 3.1 The helper

Add a single helper in `src/metadata/SipiIcc.cpp`, invoked from inside `SipiIcc::iccBytes(unsigned int&)` immediately after `cmsSaveProfileToMem` populates the buffer. The helper is a no-op unless `SOURCE_DATE_EPOCH` is set in the environment.

```cpp
namespace Sipi {
namespace {
constexpr cmsUInt32Number kIccDateTimeOffset  = 24;
constexpr cmsUInt32Number kIccDateTimeLength  = 12;
constexpr cmsUInt32Number kIccProfileIdOffset = 84;
constexpr cmsUInt32Number kIccProfileIdLength = 16;

void put_be16(unsigned char *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xff; }

// Cached on first call via thread-safe magic-static initialisation.
// Re-checking the env var on every emit is wasteful and would risk
// inconsistent goldens if it changes mid-run.
//
// Note: reproducible-builds.org spec recommends erroring on malformed
// values. SIPI deliberately silently falls back (returns nullopt) — the
// fix is test-only, and a SIPI binary aborting because someone in the
// shell exported a bogus SOURCE_DATE_EPOCH would be a regression.
std::optional<time_t> read_source_date_epoch() noexcept
{
  static const std::optional<time_t> cached = []() -> std::optional<time_t> {
    const char *e = std::getenv("SOURCE_DATE_EPOCH");
    if (e == nullptr || *e == '\0') return std::nullopt;
    char *end = nullptr;
    errno = 0;
    long long v = std::strtoll(e, &end, 10);
    if (errno != 0 || end == e || *end != '\0' || v < 0) return std::nullopt;
    return static_cast<time_t>(v);
  }();
  return cached;
}
}// namespace

// Overwrite the ICC profile creation-date header (bytes 24-35) and zero the
// Profile ID (bytes 84-99) when SOURCE_DATE_EPOCH is set. No-op otherwise.
// Mutates the buffer in place. Never throws — production must not fail
// just because lcms2 stamped a fresh time.
//
// The Profile ID is zeroed because lcms2 may round-trip a non-zero ID
// from an externally-authored input profile (Little-CMS issue #181); a
// non-zero ID baked over a scrubbed date is meaningless. Production is
// unaffected because production never sets SOURCE_DATE_EPOCH.
void normalize_icc_header_for_reproducibility(unsigned char *buf, size_t len) noexcept
{
  auto epoch = read_source_date_epoch();
  if (!epoch.has_value()) return;// production path: leave bytes untouched
  if (len < kIccProfileIdOffset + kIccProfileIdLength) return;// malformed; nothing safe to do

  std::tm tm{};
  gmtime_r(&*epoch, &tm);// SOURCE_DATE_EPOCH is UTC by spec
  unsigned char *p = buf + kIccDateTimeOffset;
  put_be16(p +  0, static_cast<uint16_t>(tm.tm_year + 1900));
  put_be16(p +  2, static_cast<uint16_t>(tm.tm_mon + 1));
  put_be16(p +  4, static_cast<uint16_t>(tm.tm_mday));
  put_be16(p +  6, static_cast<uint16_t>(tm.tm_hour));
  put_be16(p +  8, static_cast<uint16_t>(tm.tm_min));
  put_be16(p + 10, static_cast<uint16_t>(tm.tm_sec));

  std::memset(buf + kIccProfileIdOffset, 0, kIccProfileIdLength);
}
}// namespace Sipi
```

The chokepoint integration is one line in `SipiIcc::iccBytes(unsigned int& len)`:

```cpp
unsigned char *SipiIcc::iccBytes(unsigned int &len)
{
  unsigned char *buf = nullptr;
  len = 0;
  if (icc_profile != nullptr) {
    if (!cmsSaveProfileToMem(icc_profile, nullptr, &len))
      throw SipiError("cmsSaveProfileToMem failed");
    buf = new unsigned char[len];
    if (!cmsSaveProfileToMem(icc_profile, buf, &len)) {
      delete[] buf;// pre-existing leak fix; ship as part of this change
      throw SipiError("cmsSaveProfileToMem failed");
    }
    normalize_icc_header_for_reproducibility(buf, len);// <-- new
  }
  return buf;
}
```

The `std::vector<unsigned char>` overload calls the raw overload and is automatically covered.

### 3.2 Implementation steps

1. **Verify the chokepoint** (one grep, expected output known):
   ```bash
   grep -rn "iccBytes(" src/ include/ shttps/
   ```
   Expected: 5 callers (`SipiImage.cpp:325`, `SipiIOPng.cpp:552`, `SipiIOJpeg.cpp:1227`, `SipiIOTiff.cpp:1649`, six branches in `SipiIOJ2k.cpp`) plus the two definitions in `SipiIcc.cpp` and two declarations in `include/metadata/SipiIcc.h`. No emission path bypasses `iccBytes()`. The other `cmsSaveProfileToMem` callsites in `SipiIcc.cpp` (copy ctor `:50/:52`, ctor-from-cmsHPROFILE `:69/:71`, `operator= :191/:193`) are in-memory clones that round-trip through `cmsOpenProfileFromMem` — they don't reach codecs and need no wrapping.

2. **Add the helper and call it inside `iccBytes()`**. One file changed (`src/metadata/SipiIcc.cpp`); new includes (`<optional>`, `<cstring>`, `<cstdlib>`, `<ctime>`, `<cerrno>`); ~30 LOC including the env-var parser. POSIX `gmtime_r` is used (already part of the SIPI codebase via Logger and existing `cmsGetHeaderCreationDateTime` callsite at `SipiIcc.cpp:373`); macOS and Linux both supply it.

3. **Unit-test the helper** in `test/unit/sipiicc/` (new directory):
   - `SOURCE_DATE_EPOCH` unset → buffer unchanged.
   - `SOURCE_DATE_EPOCH=946684800` → bytes 24–35 = `07D0 0001 0001 0000 0000 0000` (2000-01-01T00:00:00Z), bytes 84–99 = zero.
   - Synthetic profile with pre-existing non-zero Profile ID → ID zeroed when flag is set.
   - Truncated buffer (< 100 bytes) → no-op (no throw).
   - Idempotency: running the helper twice produces the same bytes.
   - `SOURCE_DATE_EPOCH=invalid` → no-op (parse fails silently, per reproducible-builds.org spec).

4. **Extend the approval baseline** in `test/approval/image_encode_baseline_test.cpp`. Re-add the JPEG, PNG, and JP2 tests blocked in PR 0. Target 10 additional LFS-tracked goldens (<500 KB each via downscaling). Existing fixtures in `test/_test_data/images/unit/` (verified): `gray_with_icc_another.jpg`, `gray_with_icc.jpg`, `gray_with_icc.jp2`, `cmyk_lossy.jp2`, `cmyk.tif`, `cielab.tif`. Suggested coverage:

   | Test name | Input fixture | Output | Path exercised |
   |---|---|---|---|
   | `JpegFullToJpegDownscaled` | `gray_with_icc_another.jpg` | `.approved.jpg` | libjpeg encode + lcms2 ICC emit |
   | `JpegFullToPng` | `gray_with_icc_another.jpg` | `.approved.png` | libpng encode path |
   | `J2kRegionToTiff` | `gray_with_icc.jp2` | `.approved.tif` | Kakadu decode + libtiff encode + lcms2 ICC emit |
   | `J2kRegionToJpeg` | `gray_with_icc.jp2` | `.approved.jpg` | Kakadu decode + libjpeg encode + lcms2 ICC emit |
   | `J2kRegionToJp2` | `gray_with_icc.jp2` | `.approved.jp2` | **Kakadu encode determinism check (Goal 3)** |
   | `CmykTiffToJpeg` | `cmyk.tif` | `.approved.jpg` | CMYK → sRGB lcms2 colour-conversion + libjpeg encode |
   | `CielabTiffToJpeg` | `cielab.tif` | `.approved.jpg` | CIELab → sRGB lcms2 colour-conversion + libjpeg encode |
   | `JpegRotatedToJpeg` | `gray_with_icc_another.jpg` | `.approved.jpg` | rotation + libjpeg encode |
   | `PngRoundTrip` | _new fixture_ | `.approved.png` | libpng decode + libpng encode + lcms2 ICC emit |
   | `TiffToPng` | `cielab.tif` (or `cmyk.tif`) | `.approved.png` | libtiff decode + libpng encode |

   **Fixture gap:** no PNG-with-embedded-ICC exists in `test/_test_data/images/unit/`. Generate one as part of this PR (e.g. `convert gray_with_icc.jpg gray_with_icc.png` with explicit ICC preservation, or `oxipng --strip none` on a converted source). LFS-track the result. The `PngRoundTrip` test is the only one that requires the new fixture.

5. **Inject `SOURCE_DATE_EPOCH` for the approval test only.** The existing `test/approval/CMakeLists.txt` registers the test under `${TEST_NAME} = sipi.approvaltests` (verified — `add_test(NAME ${TEST_NAME} ...)` and `set_property(TEST ${TEST_NAME} PROPERTY LABELS approval)`). Add:
   ```cmake
   set_tests_properties(${TEST_NAME} PROPERTIES
     ENVIRONMENT "SOURCE_DATE_EPOCH=946684800")  # 2000-01-01T00:00:00Z UTC
   ```
   Other tests (under different `add_test` names) leave the env var unset and exercise production behaviour. Document the value choice (arbitrary SIPI-test convention; not an industry standard).

6. **Update `test/approval/CHANGELOG.approval.md`**: explain that JPEG/PNG/JP2 goldens are byte-stable only when `SOURCE_DATE_EPOCH` is injected; without it the binary emits wall-clock-stamped ICC headers (production behaviour). A maintainer running `sipi.approvaltests` directly without the env var should expect `.received.*` files for the timestamp-sensitive tests.

7. **Document the invariant** in `include/metadata/SipiIcc.h` (note: `.h`, not `.hpp`) above the `iccBytes()` declarations: when `SOURCE_DATE_EPOCH` is set, returned bytes have normalized creation-date and zeroed Profile ID; otherwise lcms2's wall-clock bytes are returned verbatim. Cross-reference `test/approval/CHANGELOG.approval.md`.

8. **Update project-level documentation.** Five files outside `src/` and `test/`:

   1. **`sipi/CLAUDE.md`** — extend the existing "Build reproducibility invariant" section (or add a new "ICC determinism invariant" callout). State: (a) `SipiIcc::iccBytes()` is the single chokepoint that converts `cmsHPROFILE` → bytes for codec consumption; any new format handler must route through it; (b) approval tests run with `SOURCE_DATE_EPOCH` injected by CMake; production never sets it. This propagates the invariant to every future contributor.

   2. **`sipi/docs/src/development/testing-strategy.md`** — the authoritative testing-strategy doc per `sipi/CLAUDE.md`. Add: approval tests (`test/approval/`) are byte-exact only with `SOURCE_DATE_EPOCH` set. Document the rationale (lcms2 timestamp leak; wall-clock production behaviour preserved).

   3. **`sipi/docs/src/development/developing.md`** — running tests. Add: when running `sipi.approvaltests` directly outside of `ctest`, the developer must export `SOURCE_DATE_EPOCH=946684800` first, otherwise expect `.received.*` files for ICC-touching tests. `ctest -L approval` injects the env var automatically.

   4. **New ADR `sipi/docs/adr/0002-icc-profile-determinism-test-only.md`** — capture the architectural decision: production keeps wall-clock-stamped ICC creation dates; tests gate determinism behind `SOURCE_DATE_EPOCH`. Follow the `0001-shttps-as-strangler-fig-target.md` style (~10 lines: decision statement, rationale, alternative considered + rejected). When implementing, use the available ADR-creation skill if present in the contributor's tooling; otherwise copy the structure of `0001-*`.

   5. **`sipi/UBIQUITOUS_LANGUAGE.md`** — add a small "Reproducibility" subsection (or extend the "Format handling" section) with two terms:
      - **ICC normalization** — the byte-level rewrite of bytes 24–35 (creation date) and 84–99 (Profile ID) inside `SipiIcc::iccBytes()`, gated by `SOURCE_DATE_EPOCH`. Aliases to avoid: ICC scrubbing, ICC stripping (those imply removing profiles, not normalizing them).
      - **Reproducibility flag** — `SOURCE_DATE_EPOCH` env var. Aliases to avoid: deterministic mode, test-only mode (the env var is the contract; "modes" obscure that).

### 3.3 Verification

```bash
# Approval tests (env var injected by CMake set_tests_properties).
just nix-build && cd build && ctest -L approval --output-on-failure

# Determinism re-check: re-run twice, no .received.* files should appear.
cd build && SOURCE_DATE_EPOCH=946684800 ./test/approval/sipi.approvaltests \
  --gtest_filter='ImageEncodeBaseline.*'
SOURCE_DATE_EPOCH=946684800 ./test/approval/sipi.approvaltests \
  --gtest_filter='ImageEncodeBaseline.*'  # second run
ls test/approval/approval_tests/*.received.* 2>/dev/null && echo FAIL || echo OK

# Production path is untouched: without the env var, JPEG/PNG/JP2 outputs
# embed wall-clock timestamps (expected; this is what production users see).
cd build && ./test/approval/sipi.approvaltests \
  --gtest_filter='ImageEncodeBaseline.JpegFullToJpegDownscaled'
# Expected: .received.jpg differs from .approved.jpg in bytes 24-35 of ICC.

# Helper unit tests.
cd build && ctest -R sipi_icc_normalize --output-on-failure

# Pre-fix verification (TIFF "already deterministic" claim from PR 0):
cd build && for i in 1 2; do
  ./test/approval/sipi.approvaltests \
    --gtest_filter='ImageEncodeBaseline.JpegToTiffDownscaled'
done && ls test/approval/approval_tests/*.received.* 2>/dev/null \
  && echo "TIFF NOT deterministic — investigate" || echo "TIFF confirmed deterministic"
```

## 4. Acceptance Criteria

- [ ] `nix build .#dev` succeeds with the extended approval suite (12–15 tests) on macOS-aarch64 and linux-{x86_64,aarch64}.
- [ ] With `SOURCE_DATE_EPOCH=946684800` injected (CMake `set_tests_properties`), re-running `sipi.approvaltests` twice in a row produces zero `.received.*` files for `ImageEncodeBaseline.*`.
- [ ] **Production unchanged:** without `SOURCE_DATE_EPOCH`, the same binary emits wall-clock-stamped ICC headers (verifiable: same input run twice → 1-byte diff in the seconds field).
- [ ] `iccBytes()` is the only path that converts a `cmsHPROFILE` to bytes for codec consumption, and `normalize_icc_header_for_reproducibility` is called inside it exactly once before `return`. Verified by:
  ```bash
  grep -rn "iccBytes(" src/ include/ shttps/
  grep -n "normalize_icc_header_for_reproducibility" src/metadata/SipiIcc.cpp
  ```
- [ ] Unit test `sipi_icc_normalize_test` passes:
  - `SOURCE_DATE_EPOCH` unset → buffer unchanged
  - `SOURCE_DATE_EPOCH=946684800` → bytes 24–35 = `07D0 0001 0001 0000 0000 0000`, bytes 84–99 zeroed
  - non-zero Profile ID input → ID zeroed when flag is set (no throw)
  - truncated buffer → no-op (no throw, no crash)
  - idempotent: helper run twice produces identical bytes
  - malformed `SOURCE_DATE_EPOCH` (non-numeric) → no-op (no throw)
- [ ] Existing TIFF goldens from PR 0 unchanged after the fix (`git diff test/approval/approval_tests/Image*.approved.tif` empty).
- [ ] At least one `*ToJp2` golden present and byte-deterministic across two consecutive runs (with `SOURCE_DATE_EPOCH` injected). The two valid implementations are: (a) Kakadu's `jp2_colour::init()` embeds the normalized ICC bytes verbatim; (b) Kakadu reframes the bytes but still emits a deterministic JP2 — see §6 fallback.
- [ ] `include/metadata/SipiIcc.h` header comment + `test/approval/CHANGELOG.approval.md` updated.
- [ ] Project-level docs updated: `sipi/CLAUDE.md` (chokepoint + env-var invariant), `sipi/docs/src/development/testing-strategy.md` (approval-test gating), `sipi/docs/src/development/developing.md` (developer instructions for direct `sipi.approvaltests` runs), new ADR `sipi/docs/adr/0002-icc-profile-determinism-test-only.md` (architectural decision), `sipi/UBIQUITOUS_LANGUAGE.md` (ICC normalization, Reproducibility flag).

## 5. Effort

~2 days. ~30 LOC of C++ (helper + parser + one call inside `iccBytes`) + ~10 new LFS-tracked approval goldens (with one new PNG-with-ICC fixture) + 5 small documentation files (CLAUDE.md addition, testing-strategy update, developing-doc note, new ADR ~10 lines, UBIQUITOUS_LANGUAGE.md entries). The chokepoint design eliminates per-codec wrapping; most engineering effort is now in fixture sourcing, JP2-encode verification, and propagating the invariant through contributor-facing docs.

## 6. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Kakadu's `jp2_colour::init()` reframes ICC bytes (rewrites tags, recomputes Profile ID) → JP2-encode goldens drift | Medium | Medium | Goal 3's `J2kRegionToJp2` golden surfaces this on the first CI run. If reframing happens, fall back to a JP2 ICC-only test that strips wrapper bytes and compares the inner ICC payload |
| Missing an ICC emission path (a future codec is added that doesn't go through `iccBytes()`) | Low | Medium | The chokepoint is enforced by code review of any new format handler. AC requires `grep -rn "iccBytes(" src/ include/ shttps/` to be the complete picture |
| Downstream production consumers rely on the ICC creation date | Low | Low | Production behaviour is unchanged (helper is a no-op without `SOURCE_DATE_EPOCH`). No regression possible |
| Profile bytes truncated or corrupted by in-place mutation | Low | High | Bounds check (`len < kIccProfileIdOffset + kIccProfileIdLength → no-op`); unit tests with synthetic profiles; existing TIFF goldens validate end-to-end |
| Thread-safety of the helper | Low | Medium | Helper is pure: reads env-var (memoized via thread-safe magic-static), mutates the caller-owned buffer only, no shared writable state |
| `SOURCE_DATE_EPOCH` value chosen by another tool in the test environment differs from `946684800` | Low | Medium | CMake `set_tests_properties(... ENVIRONMENT ...)` sets the env var explicitly per-test; ctest replaces the parent value rather than appending. Document in `CHANGELOG.approval.md` |
| Existing pre-fix `delete[] buf` leak on the second `cmsSaveProfileToMem` failure path inside `iccBytes(unsigned int&)` | Low | Low | Fixed opportunistically as part of step 2 (one-line change; orthogonal to the deterministic-ICC work but reviewed in the same PR — or split into a separate fix-prefixed commit on the same branch if maintainer prefers a clean history) |

## 7. Out of Scope

1. **Production-side determinism.** Production builds keep emitting wall-clock-stamped ICC headers. This plan is test-infrastructure-only.
2. Upstreaming `SOURCE_DATE_EPOCH` support to lcms2 — rejected upstream (Debian #814883, Little-CMS #71), not worth a fork.
3. Computing MD5 Profile IDs ourselves — would require choosing a deterministic seed; defer until a real consumer needs Profile IDs.
4. Stripping ICC profiles entirely from outputs — loses colour-management for downstream consumers.
5. Migrating to libjpeg-turbo or other dep upgrades — independent concerns.

## 8. Dependencies

- **Lands after sipi#586** (PR 0 of the Nix-native migration). As of 2026-04-29 PR 0 is on feature branch `feature/dev-6328-sipi-nix-native-build-pr-0-drop-zig-static-path-test`, **not yet merged to `main`**. The TIFF-only baseline (5 LFS-tracked goldens) serves as the foundation; this plan extends it. Sequencing rationale: shipping the helper before PR 0's approval-test infrastructure exists would be unverifiable.
- **CMake ≥ 3.28** for `set_tests_properties(... ENVIRONMENT ...)` per-test injection — already required by the project.
- No external nixpkgs version pins required.

## 9. Sources

Research that informed this plan (eng:research:best-practices-researcher report, 2026-04-29):

- [reproducible-builds.org — `SOURCE_DATE_EPOCH` specification](https://reproducible-builds.org/specs/source-date-epoch/) — the standard env-var protocol, parse rules, and unset/malformed semantics adopted here
- [CMake `ENVIRONMENT` test property](https://cmake.org/cmake/help/latest/prop_test/ENVIRONMENT.html) — per-test env-var injection, replaces (does not append to) parent environment
- [ICC.1:2022 §4.2 — Profile header layout](https://www.color.org/specification/ICC.1-2022-05.pdf) — definitive spec for `dateTimeNumber` at offset 24 (six big-endian `uInt16Number`) and the Profile ID at offset 84 (16 bytes; MD5 over header zeros bytes 44-47/64-67/84-99 during input)
- [Debian Bug #814883](https://www.mail-archive.com/debian-bugs-dist@lists.debian.org/msg1483752.html) — `cmsSetHeaderCreationDateTime` request rejected upstream
- [Little-CMS issue #71](https://github.com/mm2/Little-CMS/issues/71) — upstream rationale for the rejection; Marti Maria recommends overwriting the serialized header directly
- [Little-CMS issue #181](https://github.com/mm2/Little-CMS/issues/181) — round-trip behaviour: lcms2 may rewrite tags during re-serialization, motivating the unconditional Profile-ID zero rather than an assertion
- Little-CMS `lcms2.h`, `src/cmsio0.c`, `src/cmsplugin.c` — header structure and `_cmsGetTime` source
- [libjxl issue #2725](https://github.com/libjxl/libjxl/issues/2725) — discussion of ICC preservation strategies in another reference image library
- libvips, ImageMagick, OpenJPEG, libjxl test strategies — pixel-equivalence / metadata-strip patterns (none normalize ICC dates; all chose strip-or-tolerance rather than this plan's approach)
