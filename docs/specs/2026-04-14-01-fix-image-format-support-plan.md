---
title: "fix: Support 1-bit bilevel TIFF and broaden JPEG format handling"
type: fix
date: 2026-04-14
author: "Claude"
status: reviewed
repositories:
  - name: sipi
linear:
  - DEV-6249
  - DEV-6250
  - DEV-6257
---

# fix: Support 1-bit bilevel TIFF and broaden JPEG format handling

## Linear Issues

This plan implements the fixes tracked by these Linear issues. **The implementation PR description must reference all three issues so they auto-close on merge.**

- [DEV-6249](https://linear.app/dasch/issue/DEV-6249/bug-1-bit-bilevel-bitmap-tiff-images-rejected-with-not-supported-error) — bug: 1-bit bilevel (bitmap) TIFF images rejected with "not supported" error
- [DEV-6250](https://linear.app/dasch/issue/DEV-6250/bug-jpeg-files-from-heritage-collections-fail-to-process-in-sipi) — bug: JPEG files from heritage collections fail to process in sipi
- [DEV-6257](https://linear.app/dasch/issue/DEV-6257/bug-jpeg-cmyk-images-miss-app14-adobe-transform-inversion) — bug: JPEG CMYK images miss APP14 Adobe transform inversion

**PR footer template** (paste into the implementation PR body):

```
Closes DEV-6249
Closes DEV-6250
Closes DEV-6257
```

## Overview

Sipi explicitly rejects 1-bit bilevel (bitmap) TIFF images despite having the conversion infrastructure already implemented. Additionally, certain JPEG files from heritage collections fail to process. This plan addresses both the immediate failures (22 TIFFs and 2 JPEGs) and three targeted JPEG robustness improvements: YCCK colorspace handling, CMYK APP14 Adobe transform inversion, and resilient metadata parsing. Other known JPEG gaps (12-bit depth, ExtendedXMP) are documented in the References section but **out of scope** for this plan — they are tracked for future work.

## Problem Statement / Motivation

A batch of 22 high-resolution 1-bit bilevel TIFF scans (1800 DPI, CanoScan 9000F, Adobe Photoshop CC) and 2 JPEG files from a cultural heritage collection cannot be ingested through sipi. These are legitimate archival images that sipi must handle as an IIIF-compatible preservation server.

The TIFF rejection is at `SipiIOTiff.cpp:1192` — a hard `throw` in a switch case for `bps == 1`. The conversion functions (`one2eight<T>()` at line 439 and `cvrt1BitTo8Bit()` at line 2171) already exist but are unreachable due to this early rejection.

The JPEG failures have not yet been diagnosed. Research reveals additional JPEG handler gaps that affect real-world files from heritage collections where images span decades of scanning software.

**Image characteristics (all 22 TIFFs):**
- BitsPerSample: 1 (bilevel/bitmap)
- SamplesPerPixel: 1 (grayscale)
- Compression: LZW (21 files), None (1 file — `7-27_KV40_FN473`)
- PhotometricInterpretation: MINISWHITE (0 = white, 1 = black)
- Organization: Stripped (not tiled) — tiled path fix is out of scope
- Dimensions: range from 2244x2906 to 17448x9624
- Metadata: XMP, Photoshop data, ICC profile reference (sRGB)

**JPEG characteristics (2 files):**
- 404x201, 8-bit RGB baseline DCT, sRGB ICC profile
- APP13 (Photoshop/IPTC with German umlauts) before APP1 (EXIF) + APP1 (XMP)
- Created by Adobe Photoshop CS (2008)
- Exact failure mode unknown — diagnosis required

## Proposed Solution

The solution is sequenced so that Phase 0 (fixtures + failing tests) comes first for TDD discipline, then the `--json` diagnostic tooling ships first **among production code changes** — unblocking the JPEG diagnosis that follows.

### Part A: Structured JSON CLI Output for Local Debuggability (first production deliverable)

Add a `--json` CLI flag that emits a structured JSON report (success or error) to stdout, mirroring the information currently sent to Sentry via `capture_image_error()`. RDU and other consumers run sipi locally without a Sentry DSN — the structured error context currently disappears silently. With `--json`, the same `ImageContext` + error envelope goes to stdout instead, enabling both easier bug reporting and programmatic integration. Shipping this first means the JPEG reproduction in Part C produces structured output that the repro test can assert on directly. **The flag applies to CLI (`--file`) mode only; it is explicitly ignored in server (`--config`) mode** — server-side errors continue to go through Sentry and HTTP responses as today.

### Part B: 1-bit Bilevel TIFF Support

Remove the early rejection gate and let the existing `read_standard_data<uint8_t>()` conversion path handle 1-bit images. Fix pre-existing bugs in the read path that would cause buffer overflows when 1-bit images are processed with IIIF region (ROI) requests.

### Part C: JPEG Fix and Targeted Robustness

Run sipi against the failing JPEGs with `--json` to capture a structured failure report, use that to drive the repro test, then fix the root cause. In addition, address three specific JPEG handler gaps: (1) YCCK colorspace (currently throws), (2) CMYK APP14 Adobe transform inversion (currently inverts incorrectly for Photoshop-produced files), and (3) metadata parsing failures being fatal to the whole read (make them non-fatal via per-block try/catch).

## TDD Approach

**Write failing repro tests before writing any fix.** Each bug gets a test that fails today and will pass after the fix. No fix is merged without its repro test — this prevents silent regressions and makes the PR self-documenting.

Ordering rule for every bug in this plan:

1. **Red** — write a test (unit or integration) that exercises the failing file or scenario and asserts the expected correct behavior. Verify it fails on `main`.
2. **Green** — apply the minimal fix that makes the test pass. Verify it does.
3. **Refactor** — clean up adjacent code (remove dead paths, apply modernization per the C++ style guide) while keeping the test green.

**Test count and categories.** The plan produces **13 tests total**, split into two categories:

- **10 regression tests** exercise existing bugs. They must be authored first, run against `main`, and observed to fail before any fix lands. These enforce the "red, then green" TDD discipline.
- **3 feature-contract tests** cover the new `--json` flag (Part A). They cannot fail on `main` because the feature does not exist yet. They must be authored in the same PR as the feature (Phase 1) and used to drive the implementation TDD-style.

**Regression tests (10) — fail on `main`, pass after fix:**

| # | Test name | Target bug | Part / Linear | Fixture | Expected failure on main |
|---|-----------|-----------|------|---------|--------------------------|
| R1 | `Tiff1BitLzwMinisWhiteReadsAs8BitTest` | 1-bit rejection | B / DEV-6249 | `bilevel_lzw_miniswhite.tif` | Throws "Images with 1 bit/sample not supported" |
| R2 | `Tiff1BitUncompressedReadsAs8BitTest` | 1-bit rejection | B / DEV-6249 | `bilevel_none_miniswhite.tif` | Throws "Images with 1 bit/sample not supported" |
| R3 | `Tiff1BitMinisBlackInvertsCorrectlyTest` | Photometric handling | B / DEV-6249 | `bilevel_lzw_minisblack.tif` | Throws (reached via same gate) |
| R4 | `Tiff1BitRoiDoesNotCorruptMemoryTest` | memcpy offset buffer overflow | B / DEV-6249 | `bilevel_roi_test.tif` with `roi_y=64` | Buffer overflow under ASan once gate is removed |
| R5 | `Tiff8BitBufferSizeIsCorrectTest` | Switch fall-through (ps=2 for 8-bit) | B / DEV-6249 | any 8-bit TIFF | Allocates 2x needed bytes — detectable via instrumented allocator |
| R6 | `Jpeg_35_2421d_ReadsSuccessfullyTest` | JPEG read failure | C / DEV-6250 | `35-2421d-o.jpg` and `35-2421d-r.jpg` (both variants as sub-cases) | Fails with the actual error captured during diagnosis |
| R7 | `JpegYcckColorspaceReadsTest` | YCCK rejection | C / DEV-6250 | synthesized YCCK JPEG | Throws "Unsupported JPEG colorspace JCS_YCCK" |
| R8 | `JpegCorruptIptcStillReadsImageTest` | Metadata-failure-is-fatal | C / DEV-6250 | JPEG with intentionally corrupted IPTC | Fails entire read today |
| R9 | `JpegCmykPhotoshopApp14InversionTest` | CMYK APP14 inversion missing | C / DEV-6257 | Photoshop-produced CMYK JPEG (has APP14 with `transform=0`) | Color values are inverted vs reference output |
| R10 | `JpegCmykRawNoApp14NotInvertedTest` | CMYK APP14 inversion missing (negative case) | C / DEV-6257 | "Raw" CMYK JPEG (no APP14 marker) | Renders correctly on `main`; test pins the no-inversion branch so the fix doesn't regress it |

**Feature-contract tests (3) — authored during Phase 1, no "fail on main" expectation:**

| # | Test name | Target | Part | Fixture | Asserts |
|---|-----------|--------|------|---------|---------|
| F1 | `CliJsonOutputSuccessContainsMetadataTest` | `--json` success payload | A | any valid image | `status: "ok"`, `image.*` populated, valid JSON |
| F2 | `CliJsonOutputErrorContainsImageContextTest` | `--json` error payload | A | a failing fixture from R-tests | `status: "error"`, `phase`, `error_message`, populated `image.*` |
| F3 | `CliJsonOutputStdoutIsSingleJsonDoc` | single-doc contract (no log chatter) | A | **synthetic JPEG with intentionally malformed XMP** (must read successfully but fire `log_warn` on the XMP parse) | stdout parses as exactly one JSON document (nothing before `{` or after matching `}`); the warning text appears on stderr |

**Synthetic fixtures.** A small C++ generator (built and run once when the test data is first prepared) writes the bilevel TIFFs by calling libtiff directly; the resulting `.tif` files are committed under `test/_test_data/images/bilevel/`. The generator lives at `test/unit/sipiimage/fixtures/generate_bilevel_tiffs.cpp` and is also committed so the fixtures are reproducible — but it is not invoked at every build (no runtime ImageMagick dependency in CI).

**Build order within the PR:** fixtures → failing tests → Phase 1 `--json` CLI flag (shipped first so it is available for Phase 4) → Phase 2 TIFF prerequisite bug fixes → Phase 3 TIFF enablement → Phase 4 JPEG diagnosis using `--json` to capture the failure envelope (which may spawn *additional* tests based on what the reproduction reveals) → Phase 5 JPEG gaps → Phase 6 final regression sweep → Phase 7 documentation updates.

## Technical Considerations

### Architecture impacts

- **No new abstractions needed.** The 1-bit to 8-bit conversion already exists in `one2eight<T>()` (per-scanline, on-the-fly). The fix is primarily removing a gate and fixing surrounding bugs.
- **Dead code cleanup.** After enabling the `read_standard_data` path for 1-bit, the post-read `cvrt1BitTo8Bit()` at lines 1264/1270 becomes dead code (`img->bps` is already set to 8 at line 1212 before that check runs). Remove it to avoid confusion.
- **Output format roundtrip.** After reading, a 1-bit TIFF becomes 8-bit grayscale with only 0 and 255 values. The TIFF writer's auto-detection at line 1508 will re-encode as 1-bit CCITT Fax4, changing the compression from the original LZW. This is acceptable for archival workflows.

### Performance implications

- **Memory expansion.** 1-bit to 8-bit is an 8x memory increase. Largest image (17448x7785) expands from ~17 MB packed to ~136 MB as 8-bit. The existing per-scanline conversion in `one2eight<T>()` keeps peak memory to one scanline of each representation, not the full image — the expansion happens in the `inbuf` vector which is `roi_w * roi_h * nc` uint8_t entries.
- **Memory budget.** The `SipiMemoryBudget` system should account for the decoded 8-bit size (width x height x 1 channel x 1 byte), not the 1-bit packed size. The `getDim()` function already reads `bps` correctly for 1-bit images.

### Security considerations

- **Transfer function buffer overflow (line 1110).** When `bps=1`, `1 << bps` = 2, allocating only 6 shorts for the transfer function buffer. If a malformed TIFF includes a `TRANSFERFUNCTION` tag with more entries, this is a heap overflow. Guard the transfer function path to skip when `bps <= 4` (bilevel/4-bit images don't meaningfully use transfer functions).
- **Input validation.** Continue to rely on libtiff for TIFF structural validation. The `TIFFReadScanline` call handles decompression and returns packed bits — sipi only needs to unpack.

## Implementation Approach

**TDD ordering reminder:** every phase below begins with the test(s) from the TDD Approach table above. Only advance to the production code change after you have a failing test that pins the behavior.

### Phase 0: Build fixtures and failing repro tests (prerequisite for all other phases)

- Add a fixture generator at `test/unit/sipiimage/fixtures/generate_bilevel_tiffs.cpp` that writes the 4 synthetic bilevel TIFFs listed in the TDD table using libtiff directly (width, height, bps=1, SAMPLESPERPIXEL=1, photometric, compression all set explicitly).
- Add the failing JPEGs `35-2421d-o.jpg` and `35-2421d-r.jpg` to `test/_test_data/images/jpeg/` with a short README explaining their provenance. Both files are covered by `Jpeg_35_2421d_ReadsSuccessfullyTest` (R6) as parameterized sub-cases.
- Acquire two CMYK JPEG fixtures for R9 / R10: one Photoshop-produced (saved as CMYK from Photoshop, will contain APP14 with `transform=0`) and one "raw" CMYK JPEG without APP14 (e.g. produced by ImageMagick's `convert -colorspace CMYK`). Place under `test/_test_data/images/jpeg/cmyk/`.
- Build the F3 fixture: a synthetic small JPEG (e.g. 64x64 RGB) with an intentionally **malformed APP1 XMP segment** — the JPEG must read successfully (image decodes) but the XMP parser must fail and emit `log_warn("Failed to parse XMP metadata from JPEG")`. Generate via a small Python script using PIL plus manual byte-injection of a corrupted XMP packet, or by hand-editing a known-good JPEG in a hex editor and committing the result. Place under `test/_test_data/images/jpeg/malformed_xmp.jpg`. This fixture is consumed by F3 only; it has no R-test counterpart (F3 is not a "fail on main" regression test).
- Write the **10 regression tests** (R1–R10 from the TDD table). Confirm each fails on `main` with the expected error (or, for overflow/ASan tests, under the sanitizer build; for R10 the test should pass on `main` and serve as a regression guard against the Phase 5.2 fix over-applying inversion).
- Note: the 3 feature-contract tests (F1–F3) are authored as part of Phase 1 alongside the `--json` implementation, not here — they have no pre-existing behavior to pin. Phase 0 only stages their fixtures.
- Commit this phase as a single commit titled `test: add failing regression tests for DEV-6249, DEV-6250, DEV-6257` so the red-then-green progression is visible in the history.

### Phase 1: Structured JSON CLI output (`--json` flag)

**Goal:** Expose the existing `ImageContext` + error data as structured JSON on stdout when sipi is run from the CLI, so that environments without a Sentry DSN (RDU local, CI, ad-hoc debugging) still get the full diagnostic payload. This phase ships first because Phase 4 (JPEG diagnosis) consumes its output.

**1.1 CLI flag**

Add a boolean flag in `src/sipi.cpp` argument parsing (where `-F/--format` is defined near line 509). Capture the returned `Option*` so it can be used for mutual-exclusion declarations in 1.4:

```cpp
bool optJsonOutput = false;
auto *jsonOpt = sipiopt.add_flag("--json", optJsonOutput,
  "Emit a structured JSON report (success or error) to stdout instead of human-readable messages. "
  "Useful for programmatic consumers and for debugging when no Sentry DSN is configured.");
// jsonOpt->excludes(...) calls happen in 1.4 after --salsah and --query are also captured.
```

This pattern requires the corresponding `add_flag` calls for `--salsah` and `--query` to also capture their `Option*` returns (`auto *salsahOpt = ...`, `auto *queryOpt = ...`) — adjust those existing call sites if they currently discard the return value.

**Why `--json` and not `--format json`:** `-F/--format` is already bound to the output *image* format (line 509). Using a distinct boolean keeps CLI contracts unambiguous. If a second report format (YAML, protobuf) is ever needed, it can be added later as `--report-format` without breaking `--json`.

**1.2 JSON schema**

Mirror the existing `Sipi::ImageContext` struct plus an error envelope. Use jansson (already a dependency).

Success payload:

```json
{
  "status": "ok",
  "mode": "cli",
  "input_file": "/path/to/35-2421d-o.jpg",
  "output_file": "/tmp/test_output.jp2",
  "output_format": "jp2",
  "file_size_bytes": 26688,
  "image": {
    "width": 404,
    "height": 201,
    "channels": 3,
    "bps": 8,
    "colorspace": "sRGB",
    "icc_profile_type": "sRGB",
    "orientation": "TOPLEFT"
  }
}
```

Error payload:

```json
{
  "status": "error",
  "mode": "cli",
  "phase": "read",
  "error_message": "Images with 1 bit/sample not supported in file ...",
  "input_file": "/path/to/7-13_KV40_FN551_bitmap_2019.tif",
  "output_file": "/tmp/out.jp2",
  "output_format": "jp2",
  "file_size_bytes": 4768164,
  "image": {
    "width": 8040,
    "height": 9624,
    "channels": 1,
    "bps": 1,
    "colorspace": "MINISWHITE",
    "icc_profile_type": "",
    "orientation": "TOPLEFT"
  }
}
```

Top-level keys match `ImageContext`; the nested `image` object groups decoded image properties; `phase` is one of `read | convert | write | cli_args` (the last is for parameter-validation errors that fire before any image is loaded — see Phase 1.4). For `cli_args` errors the `image` object is **omitted entirely** rather than emitted with zeroed fields, since no image was loaded; consumers should treat the absence of `image` as "no image processing was attempted." For all other phases, missing/unset values stay as empty strings or `0` (never `null`) to keep consumers simple.

**`request_uri`** from the `ImageContext` struct is intentionally omitted in CLI mode — it is only populated during HTTP request handling and is reserved for potential future server-side use of this emitter (see Phase 7.3).

**Jansson API style.** Use `json_pack` / `json_pack_ex` for constructing the fixed top-level schema — it is more concise than manual `json_object_set_new` calls and catches format-string errors at parse time. Reserve the manual builder API only for any optional extension blocks (Phase 1.2.1) where field presence is conditional.

**No schema versioning.** This plan does not introduce a `schema_version` field. The `--json` output is for local debugging and ad-hoc consumers; the cost of a version field exceeds its value when there is exactly one version. If a future change ever needs version negotiation, a `schema_version` key can be added then.

**Diagnosability check — TIFF case is already covered by this schema.**

The 1-bit TIFF failure is fully diagnosable from the JSON above without any schema extension:

- `error_message` contains the literal string `"Images with 1 bit/sample not supported in file ..."` — names the root cause directly
- `phase: "read"` — localizes the failure to the read path
- `image.bps: 1` — confirms bilevel depth
- `image.channels: 1` + `image.colorspace: "MINISWHITE"` — confirms bilevel photometric
- `image.width` / `image.height` — shows the image was header-parsed before rejection
- `input_file` + `file_size_bytes` — identifies the exact file

**No additional TIFF-specific fields are required for DEV-6249.** The repro test `Tiff1BitLzwMinisWhiteReadsAs8BitTest` asserts all of the above, and the presence of `"1 bit/sample"` in `error_message` serves as the regression guard.

**1.2.1 Extending the schema if Phase 4 reveals the default is insufficient**

If Phase 4 finds that the default payload does not carry enough information to diagnose the JPEG failure (e.g. the error message is a generic "Exiv2 parse error" with no APP-segment context), add new keys at the root or under a namespaced object (e.g. `jpeg_diagnostics`). No backwards-compatibility ceremony is needed — there is no schema version, no external committed consumers, and the format is for local debugging. Keep additions minimal and document them in the schema reference page (Phase 7.3).

If Phase 4 needs them, plausible additions for the JPEG case include the marker list, per-block metadata parse status (IPTC/EXIF/XMP/ICC ok-or-failed), the APP14 transform flag, and whether ExtendedXMP was detected. Add only what's actually needed once the reproduction reveals the gap.

**1.3 Emitter**

Add a small helper in `include/SipiReport.h` (new file) that takes an `ImageContext`, an optional error message/phase, and a `std::ostream&`. This mirrors the Sentry emitter's shape so future contributors recognize the pattern:

```cpp
namespace Sipi {
void emit_json_report(std::ostream &out,
                      const ImageContext &ctx,
                      std::optional<std::string> error_message = std::nullopt,
                      std::optional<std::string> phase = std::nullopt);
}
```

**1.4 Wiring in `sipi.cpp`**

The flag is **CLI-only** — it has effect only in `--file` mode. Server mode (`--config`) continues to use the existing Sentry + HTTP-response path; a `--json` passed alongside `--config` is silently ignored (document this in the `--help` text). The 8 `capture_image_error` call sites in `SipiHttpServer.cpp` are **not** touched by this plan.

*Error paths.* Where each `Sipi::capture_image_error(...)` is currently called in CLI mode (`src/sipi.cpp` lines 1027, 1033, 1139, 1145, 1182, 1188), also call `emit_json_report(std::cout, sentry_ctx, err.what(), <phase>)` when `optJsonOutput` is true — where `<phase>` is one of `"read"`, `"convert"`, or `"write"` matching the existing call site (the fourth value `"cli_args"` is reserved for the pre-image-load helper described below). Sentry reporting stays as-is — the two are complementary, not mutually exclusive. When there is no DSN, Sentry calls no-op and JSON is the only output.

*Success path.* On successful completion in CLI mode, add **two** new calls before `return EXIT_SUCCESS;`:

```cpp
Sipi::populate_from_image(sentry_ctx, img);  // fill in final width/height/bps from output image
if (optJsonOutput) {
  Sipi::emit_json_report(std::cout, sentry_ctx);  // no error, no phase
}
```

Today, `populate_from_image` is only called in catch blocks. The new success-path call is the missing piece.

*Pre-image-load parameter errors* at `src/sipi.cpp:967-968` (unsupported output extension), `994-995` (invalid size), and `1001-1002` (invalid scale) currently exit with `EXIT_FAILURE` **without** calling `capture_image_error`. Under `--json`, these paths must emit a minimal error JSON as well — at least `{"status": "error", "mode": "cli", "phase": "cli_args", "error_message": "..."}` — so the single-document contract holds for every non-zero exit. The `image` object is omitted because no image was loaded. Add a small helper `emit_json_cli_arg_error(err_msg)` at the top of `sipi.cpp` and call it from each of these three sites when `optJsonOutput` is true.

*Exit codes* are unchanged: `0` on success, non-zero on failure. JSON goes to `stdout`; all log lines go to `stderr` (see 1.5). This keeps the contract shell-pipeable: `sipi --json ... | jq '.image.bps'`.

*Mutual exclusion with conflicting stdout writers.* Two existing flags also write to stdout and conflict with the single-document contract:

- `--salsah` (`src/sipi.cpp:1194`) writes `img.getNx() << " " << img.getNy()` to stdout after successful conversion.
- `--query` (`src/sipi.cpp:867`) dumps `std::cout << img << std::endl` (human-readable info).

Declare `--json` mutually exclusive with both using CLI11 — note the calls go on the captured `Option*` (`jsonOpt`), not on the `bool` (`optJsonOutput`):

```cpp
jsonOpt->excludes(salsahOpt)->excludes(queryOpt);
```

A clear error at parse time is preferable to a corrupted stdout. Document this exclusion in the `--help` text.

**1.5 Redirect log output to stderr when `--json` is active**

Today, `src/Logger.cpp:130-138` routes `log_info` and `log_warn` to **stdout** in CLI mode (only `log_err` goes to stderr). Active `log_warn` sites that fire on the files this plan targets:

- `src/formats/SipiIOJpeg.cpp:684` — `log_warn("Failed to parse XMP metadata from JPEG")` — fires on the DEV-6250 JPEGs (they have XMP data)
- `src/formats/SipiIOJpeg.cpp:1242` — incomplete ICC write warning
- `src/formats/SipiIOJ2k.cpp:144` — Kakadu warning messages during JP2 output

Any of these writing to stdout breaks the `CliJsonOutputStdoutIsSingleJsonDoc` contract (F3).

**Fix:** Add a `Logger::set_json_mode(bool)` setter (encapsulated; preferred over a global flag because the Logger already owns its own state and we keep mutability inside the class). When set to true, route every log level — `log_info`, `log_warn`, **and** `log_err` — to stderr. Call `Logger::set_json_mode(true)` from `sipi.cpp` immediately after CLI11 parses `--json`. This is a small change in the Logger (a member bool plus one branch in the routing logic) plus one call site.

**Thread-safety note.** Like the existing `g_cli_mode` flag in the Logger, `set_json_mode` is intended to be set **once at startup, before any logging happens, and never re-set**. Server mode (`--config`) does not call `set_json_mode` at all (`--json` is rejected via mutual exclusion with `--config`'s side flags or simply ignored — see Phase 1.4). Concurrent reads of an unsynchronized `bool` are safe under x86-64/ARM64 memory models for this initialize-once pattern; document this constraint in the setter's docstring.

Rationale for sending even `log_err` to stderr under `--json`: keeping a uniform policy is simpler than a per-level routing table, and a consumer parsing the error JSON from stdout does not need the human-readable error log competing with it.

**Contract:** With `--json`, stdout contains **exactly one** JSON document (terminated by a trailing newline). All other diagnostic output is on stderr. The F3 test MUST use a fixture that exercises at least one `log_warn` path (the DEV-6250 JPEG is an ideal choice once Phase 4 lands a fix — before that fix, a synthetic JPEG with malformed XMP) to prove the routing works end-to-end.

**1.6 Tests (feature-contract tests F1–F3 from the TDD table)**

- **F1 `CliJsonOutputSuccessContainsMetadataTest`** — invoke sipi CLI on a known-good image with `--json`, parse stdout, assert `status == "ok"` and populated `image.*` values.
- **F2 `CliJsonOutputErrorContainsImageContextTest`** — invoke sipi CLI on one of the R-test failing fixtures with `--json`, parse stdout, assert `status == "error"`, correct `phase`, and populated `image.*` as far as the failure allowed.
- **F3 `CliJsonOutputStdoutIsSingleJsonDoc`** — invoke sipi CLI on a fixture that **triggers `log_warn`** (see 1.5), parse stdout, assert exactly one JSON document (nothing before `{`, nothing after the matching `}`); assert that the warning text appeared on stderr instead.
- Hurl does not cover CLI. Use Rust e2e (`test/e2e-rust/`) or a dedicated CLI harness for all three.

### Phase 2: Fix pre-existing bugs in `read_standard_data` (prerequisite for Phase 3)

These bugs exist today but are dormant because 1-bit is rejected before reaching them. They must be fixed first.

**2.1 Fix switch fall-through at `SipiIOTiff.cpp:1190-1200`**

```cpp
// BEFORE (buggy — case 8 falls through to case 16, ps is always 2)
int ps;
switch (img->bps) {
case 1: {
  std::string msg = "Images with 1 bit/sample not supported in file " + filepath;
  throw Sipi::SipiImageError(msg);
}
case 8:
  ps = 1;
case 16:
  ps = 2;
}

// AFTER
int ps;
switch (img->bps) {
case 1:  // 1-bit is converted to 8-bit by read_standard_data
case 8:
  ps = 1;
  break;
case 16:
  ps = 2;
  break;
default:
  throw Sipi::SipiImageError("Unsupported bits/sample (" + std::to_string(img->bps)
    + ") in file " + filepath);
}
```

**2.2 Fix memcpy destination offset bugs in `read_standard_data` case 1 paths**

In the compressed + contig path (line 638), case 1 uses `nc * i * roi_w` but should use `nc * (i - roi_y) * roi_w` to match case 8 (line 645). The same bug exists in the uncompressed + contig path (line 570). These cause **buffer overflow** when `roi_y > 0`.

Fix all case 1 (and case 4, case 12) `memcpy` calls in `read_standard_data` to use the `(i - roi_y)` offset pattern consistent with case 8/16:

| Path | Line | Current offset | Fixed offset |
|------|------|---------------|--------------|
| Uncompressed contig, case 1 | 570 | `nc * i * roi_w` | `nc * (i - roi_y) * roi_w` |
| Uncompressed contig, case 4 | 574 | `nc * i * roi_w` | `nc * (i - roi_y) * roi_w` |
| Compressed contig, case 1 | 638 | `nc * i * roi_w` | `nc * (i - roi_y) * roi_w` |
| Compressed contig, case 4 | 642 | `nc * i * roi_w` | `nc * (i - roi_y) * roi_w` |

**2.3 Guard transfer function allocation against low bps**

At line 1110, add a guard. Be explicit about supported values rather than using `>=` — the surrounding code only handles 8 and 16 bps, and an explicit list documents intent:

```cpp
// Skip transfer function for non-{8,16}-bit images (meaningless for bilevel,
// buffer-unsafe for arbitrary low values like 1, 4)
if (img->bps == 8 || img->bps == 16) {
  auto *tfunc = new unsigned short[3 * (1 << img->bps)];
  // ... existing transfer function code ...
}
```

### Phase 3: Enable 1-bit TIFF reading

**3.1 The switch fix from Phase 2.1 already enables 1-bit** — case 1 falls through to case 8 with `ps = 1; break;`. No throw, no separate handling needed.

**3.2 Remove dead code: `cvrt1BitTo8Bit` call sites**

At lines 1264 and 1270, the `if (img->bps == 1)` checks are now dead code because `img->bps` was set to 8 at line 1212. Remove the checks (keep the ICC assignment):

```cpp
case PhotometricInterpretation::MINISBLACK: {
  // cvrt1BitTo8Bit removed — read_standard_data already converts 1-bit to 8-bit
  img->icc = std::make_shared<SipiIcc>(icc_GRAY_D50);
  break;
}
case PhotometricInterpretation::MINISWHITE: {
  // cvrt1BitTo8Bit removed — read_standard_data already converts 1-bit to 8-bit
  img->icc = std::make_shared<SipiIcc>(icc_GRAY_D50);
  break;
}
```

Consider also removing the `cvrt1BitTo8Bit()` function definition (lines 2171-2220) and the `sll` variable at line 1174 if no other code paths reference them.

### Phase 4: JPEG diagnosis and fix

Phase 1 (`--json` flag) is a prerequisite — diagnosis uses the structured output it produces.

**4.1 Reproduce the JPEG failure with structured output**

```bash
make nix-build   # or make zig-build-local
./build/sipi --json --file /path/to/35-2421d-o.jpg /tmp/out.jp2 \
  > failure.json 2> failure.log
cat failure.json | jq .
cat failure.log   # stderr — contains log_warn lines and human error text
```

Capture both streams — `failure.json` contains the structured payload, `failure.log` contains any `log_warn` / `log_err` lines that explain what went wrong at a level libjpeg/Exiv2 chose to report. The JSON payload contains the `phase` (`read` | `convert` | `write` | `cli_args`), the exact `error_message`, and the `image` context populated as far as the failure point allowed (omitted entirely for `cli_args`). This JSON is the authoritative artifact for the repro test — copy its relevant fields into `Jpeg_35_2421d_ReadsSuccessfullyTest`'s expected-failure assertions.

If the server path needs to be exercised too, also run sipi as a server and request the image via IIIF URL; server-side errors still go through `capture_image_error` and benefit from the same `ImageContext` population.

**4.2 Likely failure points to investigate (ordered by probability)**

1. **IPTC parsing failure in `parse_photoshop`** (line 447): `SipiIptc` constructor calls `Exiv2::IptcParser::decode()` which throws `SipiError("No valid IPTC data!")` on failure. The IPTC contains German umlauts — check if the Exiv2 version handles the encoding correctly.
2. **Exiv2 library exception during EXIF/XMP parsing**: Older Photoshop CS (2008) files may have non-standard metadata structures.
3. **APP13 before APP1 ordering**: Unlikely to cause issues since the marker loop processes in order and uses null guards, but verify.

The `phase` field from `failure.json` will immediately distinguish "read" (metadata/decoder) from "convert" (pipeline) from "write" (output encoding) — narrowing investigation to the correct code path without needing gdb or stack traces.

**4.3 Fix based on diagnosis**

- If IPTC parsing: wrap `parse_photoshop` resource parsing in try/catch per resource (lines 445-468), log warnings for unparseable resources instead of failing the entire read.
- If EXIF parsing: add try/catch around `SipiExif` construction at line 612.
- If encoding-specific: add charset detection for IPTC data.

Once the fix lands, re-run `./build/sipi --json --file ...` and confirm `status` flips from `"error"` to `"ok"`. The repro test now passes.

### Phase 5: Address known JPEG handler gaps

These are independent improvements that harden sipi for heritage collections.

**5.1 YCCK colorspace support** (`SipiIOJpeg.cpp:745-748`)

Replace the throw with proper handling. YCCK is decoded by libjpeg-turbo to CMYK internally, so map it to the existing `SEPARATED` (CMYK) path:

```cpp
case JCS_YCCK: {
  // libjpeg-turbo converts YCCK to CMYK internally; subsequent inversion
  // handling is shared with the CMYK path (see 5.2).
  img->photo = PhotometricInterpretation::SEPARATED;
  break;
}
```

**5.2 CMYK inversion handling (APP14 Adobe transform flag)**

libjpeg-turbo's CMYK output is **already inverted** (Photoshop convention) when the Adobe APP14 marker declares `transform=0` (CMYK) or `transform=2` (YCCK). The existing CMYK path (`JCS_CMYK` case at `SipiIOJpeg.cpp:737-740`) does not read this flag and therefore does not know whether to re-invert before ICC conversion. Two concrete changes:

1. In the marker-walk loop (`SipiIOJpeg.cpp:598-712`), detect `APP0+14` (Adobe marker) and read the transform byte (offset 11 in the segment). Store it on `SipiImage` as `app14_transform` (new field, uint8_t, default `255` = "no APP14"). This is a small additive change.
2. After scanline reading for `JCS_CMYK` and `JCS_YCCK` (both map to `SEPARATED`), invert (`v = 255 - v` for 8-bit, `v = 65535 - v` for 16-bit) before the existing ICC conversion step **iff libjpeg-turbo's output is in CMYK polarity that needs reversal** — i.e., when `app14_transform == 0` (Adobe "Unknown/CMYK") and the JPEG has 4 components, or when `app14_transform == 2` (YCCK, libjpeg-turbo produces inverted CMYK). For `app14_transform == 1` (YCbCr → RGB) and `app14_transform == 255` (no APP14, raw CMYK), do **not** invert. R10 (`JpegCmykRawNoApp14NotInvertedTest`) pins this latter branch.

The inversion already being present on disk does not correspond to Photoshop's convention, so the test fixture needs both a Photoshop-produced CMYK JPEG and a "raw" (no-APP14) CMYK JPEG to prove the branch selection works.

**5.3 Resilient metadata parsing** (defense in depth)

Wrap each metadata parsing block in the marker loop (lines 598-712) with individual try/catch so that a failure in IPTC parsing doesn't prevent the image from being read:

```cpp
} else if (marker->marker == JPEG_APP0 + 13) {
  if (strncmp("Photoshop 3.0", (char *)marker->data, 14) == 0) {
    try {
      parse_photoshop(img, (char *)marker->data + 14, (int)marker->data_length - 14);
    } catch (const SipiError &e) {
      log_warn("Failed to parse Photoshop metadata: %s", e.what());
    }
  }
}
```

Apply the same pattern to APP1 EXIF parsing (line 612) and APP1 XMP parsing (line 620-686, which already has a catch but only for `SipiError`, not `SipiImageError`).

**Tech-debt note.** The C++ style guide (`docs/src/development/cpp-style-guide.md`) prefers `std::expected<T, E>` over exceptions for fallible parsing operations. The minimal try/catch fix above is pragmatic for this bug-fix PR and matches the existing surrounding code; refactoring `SipiIptc`, `SipiExif`, and `SipiXmp` constructors to return `std::expected` is a follow-up worth doing but is **out of scope** for this plan. Add a `TODO(SipiReport-style-guide)` comment near each catch block so a future cleanup PR can find them.

### Phase 6: Final testing sweep

The fixtures and unit/ROI/metadata tests were already authored in Phase 0 as failing repros — by this point they should all be green. This phase is the final verification pass.

**6.1 Re-run every test from the TDD table** — all 13 (R1–R10 regression + F1–F3 feature) must pass.

**6.2 Add E2E / Hurl coverage**

- Hurl test: request a 1-bit TIFF through the IIIF endpoint with various region/size/rotation/quality combinations.
- Hurl test: request the previously-failing JPEGs through the IIIF endpoint.
- Rust e2e test for the `--json` CLI contract against both success and failure fixtures.

**6.3 Regression sweep**

- `make nix-test` — unit tests (including ApprovalTests snapshots)
- `make hurl-test` — HTTP contract tests
- `make rust-test-e2e` — end-to-end
- `make test-smoke` — smoke tests against the Docker image

**6.4 Manual validation against the real failing files**

Run sipi against each of the 22 bitmap TIFFs and both JPEGs from `/Users/subotic/Downloads/FailingImages/`, producing JP2 outputs. Visually spot-check a handful of outputs against the originals. This is the final "it works on real data" check before merge.

### Phase 7: Documentation

Every user-visible change in this plan must land with matching docs. No "doc follow-up PR" — docs ship in the same PR as the code, consistent with the eng commit conventions.

**7.1 CLI reference** — `docs/src/guide/sipi.md`

Document the new `--json` flag in the CLI options section. Include:
- Flag syntax and default behavior (disabled → existing human-readable output)
- A one-line purpose (structured JSON to stdout, used by RDU and programmatic consumers)
- Pointer to the JSON schema reference (7.3)
- Example invocation with `jq` pipeline, e.g. `sipi --json --file input.jpg out.jp2 | jq '.status'`

**7.2 Operations guide** — `docs/src/guide/running.md`

Add a short section explaining when to use `--json`:
- Local debugging when no Sentry DSN is configured (the primary RDU case)
- CI pipelines that assert on image properties
- Scripts that consume sipi output

**7.3 JSON schema reference** — new file `docs/src/guide/json-output.md`

Standalone reference documenting the `--json` payload. Contents:
- Success payload: field table with type, semantics, emission conditions
- Error payload: same table plus `phase` (`read` | `convert` | `write` | `cli_args`) and `error_message` semantics
- Explicit note: the `request_uri` field from the internal `ImageContext` struct is **omitted** from CLI `--json` output (it is reserved for potential future server-side use)
- Brief note that the schema is not versioned today (one consumer, ad-hoc debugging use); future additions will be additive — see Phase 1.2.1 in the implementation plan
- Current extension objects if any were added in Phase 4 (e.g. `jpeg_diagnostics`)
- Worked examples: one success case, one TIFF-rejection failure, one post-fix TIFF success

Link this page from `sipi.md` (the CLI reference) and from the top-level `index.md`. Register the new page in `docs/mkdocs.yml` — add it to the `nav:` section under the `Guide` group so it appears in the generated site navigation.

**7.4 Testing strategy feature matrix** — `docs/src/development/testing-strategy.md`

Edits target specific rows by their content (line numbers are fragile — content anchors survive edits):

- In the feature matrix, find the row containing `1-bit TIFF (bi-level)` and change it from `| :x: GAP | — | May fail on color conversion |` to `| :white_check_mark: | unit + hurl | MINISWHITE and MINISBLACK, LZW and uncompressed |`.
- Add a new row: `| JPEG YCCK colorspace | :white_check_mark: | unit | Was throw; now decoded via CMYK path |`
- Add a new row: `| JPEG CMYK with APP14 (Photoshop) — inverted before ICC | :white_check_mark: | unit | DEV-6257 |`
- Add a new row: `| JPEG CMYK without APP14 (raw) — not inverted | :white_check_mark: | unit | DEV-6257 negative case |`
- Add a new row: `| JPEG with APP13 before APP1 + non-ASCII IPTC | :white_check_mark: | unit | Heritage collection regression |`
- Add a new row: `| CLI \`--json\` output contract | :white_check_mark: | unit + rust-e2e | success + error payloads, single-document stdout |`
- Find the summary bullet starting `**Format edge cases** (6 gaps):` and: (a) decrement the number to reflect the resolved gaps, (b) remove `1-bit TIFF` from the list, (c) remove any JPEG-related wording that the diagnosis fix has resolved (decide per Phase 4 outcome).

**7.5 Release notes** — `docs/src/release-notes/index.md`

release-please auto-generates the CHANGELOG from Conventional Commits, so we do not edit the release notes by hand. Confirm during the PR that every commit on the branch uses the right prefix (`feat:` for the `--json` flag, `fix:` for the TIFF and JPEG bug fixes, `test:` for fixtures) so the auto-generated entry is correct and attributes are grouped sensibly.

**7.6 Downstream callout** — `docs/src/development/downstream-dependencies.md`

If this file tracks downstream consumers (RDU, dsp-api), add a short note that RDU can now consume `--json` for local error diagnostics. Keep it to two or three lines; the detailed schema lives in 7.3.

**7.7 README / index**

Check `docs/src/index.md` and the repo root `README.md` for any format-support blurbs that claim or imply "no 1-bit TIFF" or list JPEG color spaces — update to reflect the new support.

## Acceptance Criteria

**TDD discipline**

- [ ] All 10 regression tests (R1–R10) were authored **before** any production code change. R1–R9 were observed to fail on `main` (or under the ASan sanitizer build for overflow); R10 was confirmed to pass on `main` and serves as a regression guard against the Phase 5.2 CMYK fix over-inverting non-APP14 files.
- [ ] All 10 regression tests pass after their corresponding fix
- [ ] All 3 feature-contract tests (F1–F3) were authored as part of Phase 1 and pass once the `--json` flag is implemented
- [ ] Phase 0 (fixtures + R1–R10 regression tests) is a single reviewable commit

**Part A — `--json` CLI output**

- [ ] `sipi --json --file <good.jpg> <out.jp2>` emits a single JSON document to stdout with `status: "ok"` and a populated `image` object
- [ ] `sipi --json --file <bad.tif> <out.jp2>` emits a single JSON document with `status: "error"`, `phase`, `error_message`, and the `ImageContext` populated as far as the failure allowed
- [ ] Pre-image-load parameter errors (unsupported output extension, invalid size/scale) also emit a minimal error JSON with `phase: "cli_args"` (the `image` object is omitted in this case)
- [ ] Exit code is `0` on success and non-zero on failure regardless of `--json`
- [ ] stdout contains exactly **one** JSON document when `--json` is set; **no** `log_info`/`log_warn`/`log_err` output appears on stdout
- [ ] The F3 test uses a fixture that exercises at least one `log_warn` path, proving logger stderr routing works end-to-end
- [ ] `--json` is mutually exclusive with `--salsah` and `--query` (CLI11 rejects combinations at parse time)
- [ ] `--json` has no effect when combined with `--config` (server mode) — documented in `--help`
- [ ] The JSON schema matches the `ImageContext` fields currently sent to Sentry, minus `request_uri` which is reserved for future server use

**Part B — 1-bit bilevel TIFF**

- [ ] All 22 bitmap TIFFs from FailingImages folder process successfully (read + convert to JP2)
- [ ] 1-bit TIFFs render correctly through IIIF pipeline (region, size, rotation, quality)
- [ ] ROI extraction from 1-bit TIFFs works correctly (no buffer overflow — verified under ASan)
- [ ] Metadata (EXIF, XMP, IPTC) is preserved through the conversion pipeline
- [ ] Both MINISWHITE and MINISBLACK photometric interpretations handled
- [ ] Both LZW and uncompressed variants handled
- [ ] Switch fall-through bug at `SipiIOTiff.cpp:1190-1200` is fixed (throw at 1192, fall-through in cases 8/16)
- [ ] Transfer function buffer (line 1110) is guarded against low bps values

**Part C — JPEG**

- [ ] Both JPEGs from FailingImages/problems/ (`35-2421d-o.jpg` and `35-2421d-r.jpg`) process successfully
- [ ] The JPEG repro test was driven by the `--json` output of a real reproduction (see Phase 4.1)
- [ ] YCCK JPEG colorspace is handled instead of throwing (Phase 5.1)
- [ ] CMYK APP14 Adobe transform flag is read and inversion applied correctly for both Photoshop-produced and raw CMYK JPEGs (Phase 5.2)
- [ ] Metadata parsing failures in JPEG are logged as warnings, not fatal errors (Phase 5.3)

**Regression**

- [ ] Existing unit, Hurl, Rust e2e, and smoke test suites all pass without regression
- [ ] Manual spot-check of converted outputs against originals (Phase 6.4) looks correct

**Documentation**

- [ ] `--json` flag documented in `docs/src/guide/sipi.md`
- [ ] Operational use of `--json` documented in `docs/src/guide/running.md`
- [ ] New `docs/src/guide/json-output.md` exists with schema reference and worked examples
- [ ] `docs/src/development/testing-strategy.md` feature matrix updated: 1-bit TIFF row flipped to covered, new rows added for JPEG YCCK, JPEG APP13-before-APP1, and `--json` contract
- [ ] All commits on the branch use correct Conventional Commit prefixes (`feat:`, `fix:`, `test:`, `docs:`) so release-please generates sensible release notes
- [ ] `docs/src/index.md` and repo `README.md` no longer claim unsupported formats that are now supported

## Dependencies & Risks

**Dependencies:**
- Access to the 22 failing TIFF files and 2 JPEG files for manual validation
- Working build environment (Nix or Zig toolchain)

**Risks:**

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| JPEG failure root cause is in Exiv2 library, not sipi | Medium | High | Wrap metadata parsing in try/catch to make it non-fatal regardless of root cause |
| memcpy offset fix introduces regression in existing 4-bit or 12-bit paths | Low | Medium | Those paths have the same bug pattern — fixing them is net-positive. Run full test suite. |
| Large 1-bit images exhaust memory under concurrent load | Medium | Medium | Memory budget system already tracks decoded size. Verify budget uses 8-bit size, not 1-bit. |
| `cvrt1BitTo8Bit` removal breaks an untested code path | Low | Low | Grep for all callers before removing. The function is only called from the two lines being removed. |
| New `app14_transform` field on `SipiImage` not initialized in TIFF/PNG/JP2 read paths | Low | Medium | Default-initialize to `255` ("no APP14") in the `SipiImage` constructor so non-JPEG formats are unaffected. Add an explicit init test in `SipiImageConstructorTest`. |
| New `app14_transform` field silently dropped during `SipiImage` copy/move/assignment | Low | High | `SipiImage` defines explicit copy ctor, move ctor, copy assign, move assign — all four must be updated to copy/move the new field. Add a `SipiImageCopyPreservesApp14TransformTest` to lock this in. Failure mode is silent: a copied image loses its inversion flag and renders incorrectly downstream. |
| CMYK inversion fix over-applies and inverts non-APP14 CMYK files | Low | Medium | R10 (`JpegCmykRawNoApp14NotInvertedTest`) is a regression guard authored before the fix. Confirms the no-inversion branch is preserved. |

## Success Metrics

- All 24 originally failing images (22 TIFFs + 2 JPEGs) process successfully end-to-end
- YCCK JPEGs convert correctly (no longer throws)
- CMYK JPEGs from Photoshop render with correct color polarity (APP14 inversion applied), and raw-CMYK JPEGs continue to render correctly (no over-application)
- `--json` flag produces a single, valid JSON document on stdout for every CLI invocation, success or failure
- Zero regressions in existing test suite
- 1-bit TIFF coverage gap in `docs/src/development/testing-strategy.md` (the row containing "1-bit TIFF (bi-level)") is resolved
- All three Linear issues (DEV-6249, DEV-6250, DEV-6257) close on PR merge

## References & Research

### Key code locations

- `SipiIOTiff.cpp:1190-1200` — rejection gate (primary fix)
- `SipiIOTiff.cpp:439-449` — `one2eight<T>()` conversion function (already working)
- `SipiIOTiff.cpp:512-697` — `read_standard_data<T>()` with case 1 branches
- `SipiIOTiff.cpp:570,574,638,642` — memcpy offset bugs to fix
- `SipiIOTiff.cpp:1110` — transfer function buffer overflow risk
- `SipiIOTiff.cpp:1261-1273` — enclosing block for post-read ICC assignment; the dead `cvrt1BitTo8Bit` calls are specifically at lines 1264 and 1270
- `SipiIOTiff.cpp:2171-2220` — `cvrt1BitTo8Bit()` function (dead code after fix)
- `SipiIOJpeg.cpp:404-473` — `parse_photoshop` (IPTC/EXIF from APP13)
- `SipiIOJpeg.cpp:598-712` — full marker-walk loop in `read()` (APP1 EXIF/XMP at 605-686, APP2 ICC at 687, APP13 Photoshop at 702-706; **APP14 detection added here in Phase 5.2**)
- `SipiIOJpeg.cpp:737-740` — `JCS_CMYK` case (Phase 5.2 adds inversion here when `app14_transform` is set)
- `SipiIOJpeg.cpp:745-748` — YCCK rejection
- `SipiIOJpeg.cpp:722` — hardcoded `bps = 8`
- `include/SipiImage.hpp` — gains a new `uint8_t app14_transform` field. Encoding (per Adobe APP14 spec): `255` = no APP14 marker present (default); `0` = "Unknown / CMYK" (Adobe's term — when the JPEG has 4 components, libjpeg-turbo produces inverted CMYK; with 3 components, RGB); `1` = YCbCr (libjpeg-turbo converts to RGB); `2` = YCCK (libjpeg-turbo converts to inverted CMYK). Inversion in Phase 5.2 applies for `app14_transform == 0 && nc == 4` and `app14_transform == 2`. It does **not** apply for `1` (RGB) or `255` (raw CMYK with no APP14 declaration).

### Existing specs and learnings

- `dasch-specs/specs/2026-03-23-sipi-jpeg-crash-fix/` — C++ exception-through-C UB pattern (same pattern applies to TIFF callbacks)
- `dasch-specs/specs/2026-03-03-sipi-bilinn-segfault/` — boundary clamping patterns for image processing
- `dasch-specs/specs/2026-03-14-sipi-production-hardening/` — input validation requirements
- `dasch-specs/specs/2026-03-21-sipi-memory-budget-semaphore/` — memory estimation for format handlers
- `sipi/docs/src/development/testing-strategy.md:635` — 1-bit TIFF listed as test coverage gap

### External references

- ITU-T T.81 JPEG Standard — encoding modes, color spaces, APP markers
- libjpeg-turbo 3.0+ — 12-bit and lossless support, YCCK handling
- TIFF 6.0 Specification — bilevel image handling, MINISWHITE/MINISBLACK semantics
- libtiff `TIFFReadScanline` — returns packed bits for 1-bit images (8 pixels per byte)

### JPEG format variants inventory (from research)

Key gaps in sipi's JPEG handler relevant to heritage collections:

| Variant | Status in sipi | Priority |
|---------|---------------|----------|
| Baseline DCT (SOF0) | Supported | — |
| Progressive (SOF2) | Supported | — |
| Grayscale | Supported | — |
| RGB / YCbCr | Supported | — |
| CMYK (4-component) | Supported but APP14 inversion missing | **Fix in this plan (Phase 5.2, DEV-6257)** |
| **YCCK** | Throws error | **Fix in this plan (Phase 5.1, DEV-6250)** |
| 12-bit JPEG | Hardcoded to 8 (`img->bps = 8`) | Low (rare, needs libjpeg-turbo 3.0+) |
| ExtendedXMP (multi-segment) | TODO in code (line 618) | Low |
| Arithmetic coding | Supported by libjpeg-turbo | — |
| Missing EOI marker | Handled by libjpeg-turbo | — |
| JFIF + EXIF coexistence | Handled | — |
| **Metadata parsing failures** | **Fatal — crashes entire read** | **High — fix in this plan** |
