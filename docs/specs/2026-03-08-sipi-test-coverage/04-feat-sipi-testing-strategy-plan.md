---
title: "feat: Define and implement sipi testing strategy"
type: feat
date: 2026-03-10
author: "subotic"
status: reviewed
repositories:
  - name: sipi
  - name: dasch-specs
---

# Define and implement sipi testing strategy

## Overview

Define a comprehensive, documented testing strategy for sipi that maps the IIIF Image API 3.0 specification, sipi's extensions (Lua scripting, cache, CLI, knora integration), and Rust migration readiness onto a concrete testing pyramid. The strategy is documented in `docs/src/development/testing-strategy.md` and becomes the authoritative reference for AI reviewers and human contributors. A gap analysis against the current test suite produces an implementation plan to close coverage holes.

## Problem Statement

Sipi has accumulated tests organically across five frameworks (GoogleTest, pytest, Rust e2e, Hurl, libFuzzer) without a unifying strategy document. This creates several problems:

1. **No authoritative spec coverage matrix** — nobody knows which IIIF spec requirements have tests and which don't
2. **No pyramid definition** — it's unclear whether a new test should be a C++ unit test, Rust e2e test, Hurl contract test, or snapshot test
3. **Rust migration risk** — without a strategy, the migration will either lose test coverage or duplicate effort
4. **AI reviewer blind spot** — reviewers can't enforce test strategy because none is documented
5. **Current coverage (24.3% → ~35% estimated after PR #519)** still has major gaps in IIIF compliance, error handling, and security

The blog post ["Comprehensive Rust Testing Guide"](https://blog.blackwell-systems.com/posts/rust-testing-comprehensive-guide/) provides a well-structured framework for Rust testing patterns (unit/integration/doc/property-based/snapshot/fuzz) that maps well onto sipi's needs.

## Proposed Solution

### The Testing Pyramid

Four layers, from fastest/narrowest to slowest/broadest:

```
                    ┌─────────┐
                    │  Fuzz   │  Continuous (nightly CI)
                    │ Testing │  Finds crashes & edge cases
                ┌───┴─────────┴───┐
                │   E2E Contract   │  Rust harness + Hurl
                │   Tests (HTTP)   │  Tests the API contract
            ┌───┴─────────────────┴───┐
            │   Integration / Snapshot │  insta golden baselines
            │   Tests                  │  Regression detection
        ┌───┴─────────────────────────┴───┐
        │         Unit Tests               │  GoogleTest (C++, frozen)
        │         + Rust unit tests        │  New: Rust #[test] + proptest
        └──────────────────────────────────┘
```

**Distribution target (post-Rust-migration steady state):** ~50% unit, ~30% e2e contract, ~15% snapshot/integration, ~5% fuzz. Current distribution (~47% unit, ~52% e2e) is inverted because the C++ codebase lacks Rust unit tests; as migration progresses and Rust `#[test]` modules grow, the ratio will shift toward the target.

### Layer Definitions

#### Layer 1: Unit Tests (fastest, most numerous)

**Purpose:** Test individual functions and parsers in isolation.

| Sublayer | Framework | Location | When to use |
|----------|-----------|----------|-------------|
| C++ unit (frozen) | GoogleTest | `test/unit/` | Maintain existing. Do NOT add new suites. |
| Rust unit (new) | `#[test]` + `proptest` | Future `src/` modules | During Rust migration: inline `#[cfg(test)]` modules |
| Rust property-based (new) | `proptest` | Future `src/` modules | Parsers, serializers, roundtrip invariants |

**What belongs here:**
- IIIF URL parsing (region, size, rotation, quality, format)
- Filename hashing
- Configuration parsing
- HTTP header parsing, URL encoding/decoding
- Image metadata extraction
- Any pure function with well-defined inputs/outputs

**C++ freeze policy:** Existing GoogleTest suites (98 tests across 8 components) are maintained but not expanded. Bug fixes in existing tests are allowed. No new `test/unit/` directories.

#### Layer 2: Snapshot / Golden Baseline Tests

**Purpose:** Detect unintended output changes via approved golden baselines.

| Framework | Location | When to use |
|-----------|----------|-------------|
| `insta` (Rust) | `test/e2e-rust/tests/snapshots/` | info.json structure, HTTP headers, response metadata |
| ApprovalTests (C++, frozen) | `test/approval/` | Image conversion metadata (existing only) |

**What belongs here:**
- Full info.json structure (field names, types, values)
- HTTP response header sets (content-type, CORS, Link)
- knora.json response structure
- Image metadata fingerprints (EXIF tags, XMP fields, ICC profile name) — golden baselines prevent silent metadata drift during code changes or format handler updates
- Any complex output where field-by-field assertion is fragile

**Pattern:** Use `insta::assert_json_snapshot!` with redactions for dynamic fields (`id`, timestamps).

#### Layer 3: E2E Contract Tests (HTTP-level)

**Purpose:** Test sipi's HTTP API contract — the behavior visible to clients. These tests survive the Rust migration because they test the contract, not the implementation.

| Sublayer | Framework | Location | When to use |
|----------|-----------|----------|-------------|
| Complex flows | Rust (`reqwest`) | `test/e2e-rust/tests/` | Multi-step workflows, response body inspection, uploads |
| Simple contracts | Hurl | `test/hurl/` | Status codes, headers, redirects — no response body logic |

**What belongs here:**
- IIIF Image API 3.0 compliance (ALL testable requirements)
- Content negotiation (Accept header → Content-Type)
- CORS (preflight, origin echo, wildcard)
- Authentication/authorization (401, 403)
- Error handling (400, 404, 500, 501)
- File upload and retrieval
- Lua endpoint contracts
- Cache behavior (hit/miss via headers or metrics)
- Video/non-image file handling
- CLI mode testing (via process spawn + file output verification)
- Range requests
- Concurrent request handling

**Division of labor — Rust vs Hurl:**

| Use Rust when... | Use Hurl when... |
|------------------|------------------|
| Need to inspect response body (JSON, image bytes) | Only checking status code + headers |
| Multi-step flow (upload then fetch) | Single request/response |
| Need golden baseline (insta snapshot) | Simple assertion (status, header value) |
| Need to compute something (checksum, dimension) | Declarative assertion suffices |
| Need test setup/teardown (create files, etc.) | No setup needed |

#### Layer 4: Fuzz Testing (continuous, nightly)

**Purpose:** Find crashes, memory safety issues, and edge cases in parsers and input handlers.

| Framework | Location | When to use |
|-----------|----------|-------------|
| libFuzzer (C++) | `fuzz/` | IIIF URI parser, HTTP request parser |
| `cargo-fuzz` / `proptest` (Rust, future) | Future crate `fuzz/` | After Rust migration of parsers |

**What belongs here:**
- IIIF URI parser (`parse_iiif_uri`)
- HTTP request parsing
- Image format header parsing
- Any function that processes untrusted input

**Corpus management:** CI uploads corpus artifacts; `make fuzz-corpus-update` merges CI corpus into seed corpus.

### IIIF Image API 3.0 Coverage Matrix

The following matrix maps every testable IIIF spec requirement to its test status. This is the authoritative coverage reference.

#### Info.json (Section 5)

| Requirement | Status | Test | Notes |
|-------------|--------|------|-------|
| `@context` field present and correct | ✅ | `info_json_context` | |
| `id` matches base URI | ✅ | `info_json_id_contains_base_uri` | |
| `type` = `ImageService3` | ✅ | `info_json_type_imageservice3` | |
| `protocol` = `http://iiif.io/api/image` | ✅ | `info_json_protocol` | |
| `profile` = `level2` | ✅ | `info_json_profile_level2` | |
| `width` and `height` integers | ✅ | `info_json_dimensions_match_lena512` | |
| `sizes` array with valid dimensions | ✅ | `info_json_sizes_have_valid_dimensions` | |
| `tiles` with scaleFactors | ✅ | `info_json_tiles_have_scale_factors` | |
| `extraFormats` | ✅ | `info_json_extra_formats` | |
| `preferredFormats` | ✅ | `info_json_preferred_formats` | |
| `extraFeatures` (17 features) | ✅ | `info_json_all_17_extra_features` | |
| Golden baseline snapshot | ✅ | `info_json_golden_snapshot` | insta |
| Header snapshot (CT, CORS, Link) | ✅ | `info_json_headers_snapshot` | insta |
| Content-Type without Accept | ✅ | `info_json_content_type_default` | `application/json` |
| Content-Type with Accept: ld+json | ✅ | `jsonld_media_type_with_accept` | |
| Link header on default request | ✅ | `jsonld_default_has_link_header` | |
| Canonical Link header | ✅ | `canonical_link_header` | |
| Profile Link header | ❌ IGNORED | `profile_link_header` | DEV-6003: sipi bug |
| X-Forwarded-Proto HTTPS rewrite | ✅ | `info_json_x_forwarded_proto_https` | |
| Required fields structural check | ✅ | `info_json_has_required_fields` | Validates all top-level fields present |
| Structural: sizes array exists | ✅ | `info_json_has_sizes_array` | Existence check (complements value check) |
| Structural: tiles array exists | ✅ | `info_json_has_tiles_array` | Existence check (complements value check) |
| Structural: extraFeatures exists | ✅ | `info_json_has_extra_features` | Existence check (complements content check) |

#### Region (Section 4.1)

| Requirement | Status | Test | Notes |
|-------------|--------|------|-------|
| `full` | ✅ | `full_iiif_url_returns_image` | |
| `square` | ✅ | `region_square` | |
| `pct:x,y,w,h` | ✅ | `region_percent` | |
| `x,y,w,h` (pixel) | ✅ | `region_pixel`, `region_pixel_offset` | |
| Overflow → crop at edge | ✅ | `region_beyond_bounds_is_cropped` | |
| Start beyond image → error | ✅ | `region_start_beyond_image` | |
| Zero width → 400 | ✅ | `region_zero_width` | |
| Invalid syntax → error | ✅ | `region_invalid_syntax` | |
| Region + size combination | ✅ | `size_after_region` | |
| Region + rotation combination | ✅ | `rotation_after_region` | |
| Region crop (specific) | ✅ | `iiif_region_crop` | Named crop operation test |
| Region dimension verification | ❌ GAP | — | Need to verify output dimensions match requested region |

#### Size (Section 4.2)

| Requirement | Status | Test | Notes |
|-------------|--------|------|-------|
| `max` | ✅ | `full_iiif_url_returns_image` | implicit |
| `w,` (width) | ✅ | `size_by_width` | |
| `,h` (height) | ✅ | `size_by_height` | |
| `w,h` (exact) | ✅ | `size_exact` | |
| `!w,h` (best fit) | ✅ | `size_best_fit` | |
| `pct:n` | ✅ | `size_percent` | |
| `^` upscaling | ✅ | `size_upscaling` | |
| No upscale beyond original | ✅ | `size_no_upscale_beyond_original` | |
| Invalid syntax → error | ✅ | `size_invalid_syntax` | |
| Output dimension verification | ❌ GAP | — | Need to decode image and verify actual pixel dimensions |
| `^max` upscale to limits | ❌ GAP | — | Not tested |
| `^,h` (height-only upscale) | ❌ GAP | — | Not tested |
| `^w,h` (exact with upscale) | ❌ GAP | — | Not tested |
| `^!w,h` (confined upscale) | ❌ GAP | — | Not tested |
| `^pct:n` (upscale percent) | ❌ GAP | — | Not tested |

#### Rotation (Section 4.3)

| Requirement | Status | Test | Notes |
|-------------|--------|------|-------|
| `0` (no rotation) | ✅ | `full_iiif_url_returns_image` | implicit |
| `90` | ✅ | `iiif_rotation_90` | |
| `180` | ✅ | `rotation_180` | |
| `270` | ✅ | `rotation_270` | |
| Arbitrary (e.g. 22.5) | ✅ | `rotation_arbitrary` | |
| `!0` (mirror only) | ✅ | `mirror_rotation` | |
| `!180` (mirror + rotate) | ✅ | `mirror_plus_180` | |
| Invalid → error | ✅ | `rotation_invalid` | |
| Rotation output verification | ❌ GAP | — | Need to verify actual rotation applied (image dimensions swap for 90/270) |

#### Quality (Section 4.4)

| Requirement | Status | Test | Notes |
|-------------|--------|------|-------|
| `default` | ✅ | `full_iiif_url_returns_image` | implicit |
| `color` | ✅ | `quality_color` | |
| `gray` | ✅ | `quality_gray` | |
| `bitonal` | ✅ | `quality_bitonal` | |
| Invalid → error | ✅ | `quality_invalid` | |
| `extraQualities` in info.json | ❌ GAP | — | Sipi supports color/gray/bitonal but may not emit extraQualities |

#### Format (Section 4.5)

| Requirement | Status | Test | Notes |
|-------------|--------|------|-------|
| `jpg` + Content-Type | ✅ | `format_jpg_content_type` | |
| `png` + Content-Type | ✅ | `format_png_content_type` | |
| `tif` + Content-Type | ✅ | `format_tiff_content_type` | |
| `jp2` + Content-Type | ✅ | `format_jp2_content_type` | |
| Unsupported (gif, pdf, webp, bmp) | ✅ | `unsupported_formats_rejected` | |

#### CORS (Section 7.1)

| Requirement | Status | Test | Notes |
|-------------|--------|------|-------|
| Info.json ACAO without Origin | ✅ | `cors_info_json_without_origin` | |
| Info.json ACAO with Origin | ✅ | `cors_info_json_with_origin` | |
| Image ACAO with Origin | ✅ | `cors_image_with_origin` | |
| Image ACAO without Origin | ✅ | `cors_image_without_origin` | |
| OPTIONS preflight | ✅ | `cors_preflight` | |

#### HTTP Behavior (Section 7)

| Requirement | Status | Test | Notes |
|-------------|--------|------|-------|
| Base URI → redirect to info.json | ✅ | `base_uri_redirect` | |
| HEAD request | ✅ | `head_iiif_image_empty_body` | |
| 401 unauthorized | ✅ | `deny_unauthorized_image` | |
| 404 not found | ✅ | `id_random_gives_404` | |
| Path traversal rejected | ✅ | `path_traversal_rejected` | |
| Incomplete URL → error | ✅ | `id_incomplete_iiif_url` | |
| Malformed URL → error | ✅ | `id_malformed_iiif_url` | |
| Empty identifier → error | ✅ | `invalid_iiif_url_empty_identifier` | |
| HEAD returns headers | ✅ | `head_request_returns_headers` | Verifies header presence (vs empty body check) |
| Missing file → 404 | ✅ | `returns_404_for_missing_file` | Server-level 404 (complements IIIF 404) |
| HTTP 304 conditional requests | ❌ GAP | — | Sipi sends Last-Modified/Cache-Control but If-Modified-Since not tested |
| Operation ordering (Region→Size→Rot→Qual→Fmt) | ❌ GAP | — | No test verifies transformation order is correct |
| Fractional percent regions (e.g. pct:0.5,...) | ❌ GAP | — | Only integer percent tested |
| IIIF Auth API | N/A | — | Explicitly excluded; sipi uses custom auth via Lua, not IIIF Auth spec |

#### Identifier (Section 3)

| Requirement | Status | Test | Notes |
|-------------|--------|------|-------|
| Encoded slash `%2F` | ✅ | `id_escaped_slash_decoded` | |
| Encoded `#` (`%23`) | ❌ IGNORED | `id_escaped` | DEV-6004: sipi bug |
| Subdirectory identifier | ✅ | via server tests | |
| Non-ASCII identifiers | ❌ GAP | — | Not tested |
| ARK/URN identifiers | ❌ GAP | — | Not tested (may not apply) |

#### Sipi Extensions (non-IIIF)

| Feature | Status | Test Location | Notes |
|---------|--------|---------------|-------|
| File upload (TIFF→JP2) | ✅ | `upload.rs` | |
| Upload knora.json | ✅ | `upload.rs` | |
| Upload JPEG with comment block | ✅ | `upload.rs` | |
| Video knora.json metadata | ✅ | `server.rs` | |
| Lua test_functions endpoint | ✅ | `server.rs` | |
| Lua mediatype endpoint | ✅ | `server.rs` | |
| Lua mimetype_func endpoint | ✅ | `server.rs` | |
| Lua knora_session_cookie | ✅ | `server.rs` | |
| Lua orientation endpoint | ✅ | `server.rs` | |
| Lua exif_gps endpoint | ✅ | `server.rs` | |
| Lua read_write endpoint | ✅ | `server.rs` | |
| SQLite API | ✅ | `server.rs` + Hurl | |
| Missing sidecar handling | ✅ | `server.rs` + Hurl | |
| Concurrent request handling | ✅ | `server.rs` | |
| File access allowed/denied | ✅ | `server.rs` | `file_access_allowed`, `file_access_denied` |
| Knora.json validation | ✅ | `server.rs` | `knora_json_image_required_fields`, `knora_json_image_dimensions`, `knora_json_csv_file`, `knora_json_nonexistent_file` |
| Upload edge cases | ✅ | `upload.rs` | `upload_odd_file` (non-standard file types) |
| Video metadata extensions | ✅ | `server.rs` | `video_knora_json_checksums`, `video_knora_json_x_forwarded_proto` |
| Small-file range requests | ✅ | `range_requests.rs` | 7 tests: no-range, first/middle/last byte, open-ended, sequential |
| Cache hit/miss verification | ❌ GAP | — | No tests verify cache metrics or behavior |
| CLI mode (file conversion) | ❌ GAP | — | No tests for `sipi --file` mode |
| Prometheus metrics endpoint | ❌ GAP | — | No tests for `/metrics` |
| SSL/TLS endpoints | ❌ GAP | — | No Rust tests for HTTPS (Python tests have partial coverage) |
| Large-file range requests (10MB+) | ❌ GAP | — | Small-file range tests exist in `range_requests.rs` (7 tests); large-file (10MB+) tests are Python-only |
| Image dimension verification | ❌ GAP | — | Tests check status codes but not actual output dimensions |
| EXIF preservation through IIIF pipeline | ❌ GAP | — | No test verifies EXIF survives region/size/rotation transforms |
| XMP preservation through IIIF pipeline | ❌ GAP | — | No test verifies XMP survives transforms |
| ICC profile preservation/conversion | ❌ GAP | — | C++ unit tests exist for ICC round-trips but no HTTP-level test |
| IPTC metadata preservation | ❌ GAP | — | No e2e test; only linked in C++ build |
| SipiEssentials round-trip | ❌ GAP | — | Custom metadata (origname, mimetype, checksum, ICC backup) not tested via HTTP |
| CLI conversion metadata fidelity | ❌ GAP | — | `sipi --file` conversions should preserve metadata; untested |
| MIME consistency check (`/api/mimetest`) | ❌ GAP | — | Python has 6 tests, not ported |
| Thumbnail generation (`/make_thumbnail`) | ❌ GAP | — | Python-only |
| Convert from binaries (`/convert_from_binaries`) | ❌ GAP | — | Python-only |
| Temp directory cleanup | ❌ GAP | — | Python `test_clean_temp_dir`, not ported |
| Restricted image size reduction | ❌ GAP | — | Python tests restrict→128x128, Rust only tests 401 |
| 4-bit palette PNG upload | ❌ GAP | — | Python-only |
| Cache API routes (`/api/cache`) | ❌ GAP | — | Defined in test config, no tests |
| Favicon endpoint | ❌ GAP | — | Handler exists in SipiHttpServer, no tests |
| Memory safety (ASan/LSan) | ❌ GAP | — | Only fuzz harness uses sanitizers; unit/e2e tests run unsanitized |
| Thread safety (TSan) | ❌ GAP | — | Concurrent request handling untested for data races |
| Performance regression detection | ❌ GAP | — | No latency thresholds, no load testing, no request duration metrics |
| Corrupt/truncated image handling | ❌ GAP | — | No test serves a corrupt JP2 or truncated TIFF — should return 500, not crash |
| Lua route handler errors | ❌ GAP | — | No test triggers a Lua runtime error in a route handler — should return 500 gracefully |
| Zero-byte / empty file upload | ❌ GAP | — | No test uploads an empty file — should fail gracefully |
| Invalid server config startup | ❌ GAP | — | No test starts sipi with missing/invalid config keys |
| Double-encoded URL handling | ❌ GAP | — | `%252F` (double-encoded slash) behavior untested |
| Extremely long URL / header | ❌ GAP | — | No test sends URLs exceeding reasonable limits — partially covered by fuzz |
| JWT validation edge cases | ❌ GAP | — | JWT library (`shttps/jwt.h`) supports decode/verify but no test sends expired, `alg:none`, or tampered tokens |
| Image decompression bomb | ❌ GAP | — | No pixel limit on decoded images — a small file decompressing to huge dimensions can OOM the server |
| Upload size enforcement | ❌ GAP | — | `max_post_size` (300M default) enforced in `Connection.cpp` but never tested via HTTP |
| CRLF header injection | ❌ GAP | — | Identifiers with `%0d%0a` could inject response headers — no sanitization test |
| Cache key collision | ❌ GAP | — | Cache uses canonical IIIF URL as key — no test verifies different params produce distinct entries |
| Error message information disclosure | ❌ GAP | — | Error responses may leak internal filesystem paths or server version — untested |
| Slowloris / connection exhaustion | ❌ GAP | — | No test verifies server remains responsive under slow-client or connection-flood conditions |
| `parseSizeString` edge cases | ❌ GAP | — | Pure function parsing "300M", "1G", "-1" has zero tests — empty string, invalid input, overflow untested |
| Deprecated config key migration | ❌ GAP | — | `cachedir→cache_dir`, `cachesize→cache_size` migration logic with both-specified error is untested |
| CLI argument overrides | ❌ GAP | — | `--serverport`, `--sslport`, `--loglevel`, `--threads`, `--imgroot` override config file values — never tested |
| Empty jwt_secret behavior | ❌ GAP | — | `jwt_secret` defaults to empty string — untested whether JWT auth is silently disabled or insecure |
| Invalid Lua config syntax | ❌ GAP | — | No test loads a syntactically invalid Lua config file — should fail cleanly, not crash |
| Config with nonexistent paths | ❌ GAP | — | Config with nonexistent `imgroot`, `cachedir`, or `initscript` paths — untested startup behavior |
| SImage Lua API coverage | ❌ GAP | — | 12 Lua image methods (crop, scale, rotate, watermark, dims, write, send, etc.) tested only via black-box HTTP routes, never directly verified for correctness |
| Lua JWT round-trip | ❌ GAP | — | `server.generate_jwt` / `server.decode_jwt` Lua functions untested — JWT created in Lua should decode back correctly |
| Lua UUID utility round-trip | ❌ GAP | — | `server.uuid_to_base62` / `server.base62_to_uuid` inverse correctness untested |
| Lua `server.http` outbound | ❌ GAP | — | Lua scripts can make arbitrary outbound HTTP requests via libcurl — no test verifies error handling for unreachable hosts or timeout behavior |
| Lua error propagation to HTTP | ❌ GAP | — | When a C function called from Lua throws an exception, does it propagate as 500? Only covered by generic `lua_route_error_handling` gap |
| HTTP keep-alive | ❌ GAP | — | Keep-alive implemented with configurable timeout but no test sends multiple requests on same TCP connection |
| Chunked transfer encoding | ❌ GAP | — | `ChunkReader` class handles chunked requests but never tested — no test sends `Transfer-Encoding: chunked` |
| Connection: close header | ❌ GAP | — | No test verifies server honors `Connection: close` and terminates the connection |
| Thread pool exhaustion | ❌ GAP | — | No test verifies server behavior when all worker threads are busy (queuing? rejection? timeout?) |
| Graceful shutdown | ❌ GAP | — | SIGTERM handler calls `server.stop()` but no test verifies in-flight requests complete before process exits |
| Multi-page TIFF `@page` e2e | ❌ GAP | — | Parser extracts page number (7 unit tests) but no e2e test verifies `image.tif@3` returns correct page. CLI `--pagenum` marked "NYI for tif" |
| CMYK→sRGB through IIIF pipeline | ❌ GAP | — | CMYK conversion tested at unit level but not via HTTP — upload CMYK TIFF, request as JPEG, verify correct color conversion |
| CIELab through IIIF pipeline | ❌ GAP | — | CIELab conversion tested at unit level but not via HTTP — visual correctness through the full pipeline untested |
| 16-bit depth through IIIF pipeline | ❌ GAP | — | 16-bit PNG/TIFF conversions tested at unit level but not via HTTP — bit depth handling through IIIF transforms untested |
| Progressive JPEG handling | ❌ GAP | — | No test verifies sipi correctly reads progressive (interlaced) JPEG images — common in web content |
| TIFF with JPEG compression | ❌ GAP | — | Unit test `TiffJpegAutoRgbConvert` is commented out as BROKEN (YCrCb autoconvert via JPEGCOLORMODE_RGB). Known bug, untested |
| 1-bit TIFF (bi-level) | ❌ GAP | — | No test for monochrome/fax TIFF images — may fail silently on color space conversion |
| Watermark application via HTTP | ❌ GAP | — | Watermark tested at unit level (image comparison) but no e2e test verifies watermarked image bytes differ from non-watermarked |
| Restrict + watermark combined | ❌ GAP | — | "restricted_watermarked" identifier triggers both size reduction and watermark — untested combination |
| Watermark cache key separation | ❌ GAP | — | Watermarked images use `cannonical_watermark=1` cache key — no test verifies separate cache entries |
| CLI watermark mode | ❌ GAP | — | `sipi --watermark` CLI option untested |
| Concurrent cache writes (same key) | ❌ GAP | — | Two simultaneous requests for the same uncached image — cache `block_file` mechanism untested under contention |
| Cache eviction during active reads | ❌ GAP | — | LRU eviction removes a file while another thread is serving it — potential read error or crash |
| Concurrent file uploads | ❌ GAP | — | Two simultaneous uploads to same endpoint — potential temp file collision or race |
| Lua state thread isolation | ❌ GAP | — | Each request gets own Lua state but server global table is shared — no test for state leakage between concurrent requests |
| Cache disabled mode (`cache_size=0`) | ❌ GAP | — | Config supports `cache_size=0` to disable caching — no test verifies server works without cache (all requests served from source) |
| Cache LRU purge under size limit | ❌ GAP | — | `purge()` evicts oldest entries down to 80% low-water mark when `max_cache_size` exceeded — completely untested |
| Cache nfiles limit enforcement | ❌ GAP | — | `cache_nfiles` (default 200) triggers purge when file count exceeded — no test verifies count-based eviction |
| Keep-alive timeout enforcement | ❌ GAP | — | `keep_alive_timeout` (20s default) should close idle connections — no test verifies idle connection is terminated after timeout |
| Memory under sustained load (OOM) | ❌ GAP | — | **Production issue:** continuous image harvesting exhausts container memory. `SipiImage` allocates `nx*ny*nc*bps/8` bytes per request with NO pixel limit or memory cap. With `nthreads=4` and large images, peak memory can exceed Docker container limits |
| Concurrent large image decode memory | ❌ GAP | — | Multiple simultaneous requests for large images each allocate full pixel buffers plus intermediate transform buffers — no test measures peak RSS under concurrent large-image load |
| Image decode memory accounting | ❌ GAP | — | No mechanism to limit total decode memory across all threads — no test verifies server behavior when aggregate pixel buffer size approaches system memory limits |
| Intermediate buffer accumulation | ❌ GAP | — | Each transform (crop, rotate, scale, color convert) allocates a new `byte[]` buffer before freeing the old one — peak memory is ~2x image size per transform step. No test measures transform pipeline peak memory |
| Cache as memory pressure relief | ❌ GAP | — | Cache should reduce memory pressure by serving pre-rendered files without decoding — no test verifies that cache-hit responses avoid full image decode allocation |

### Gap Summary

"Covered" counts spec requirements with at least one passing test. "Gaps" are requirements with no test or only a failing/ignored test. Individual test functions (107 total) often cover multiple requirements.

| Category | Covered | Gaps | Coverage |
|----------|---------|------|----------|
| Info.json fields | 22 | 1 (profile Link — sipi bug) | 96% |
| Region parameters | 11 | 1 (dimension verify) | 92% |
| Size parameters | 9 | 6 (dimension verify, ^max, ^,h, ^w,h, ^!w,h, ^pct) | 60% |
| Rotation parameters | 8 | 1 (dimension verify) | 89% |
| Quality parameters | 5 | 1 (extraQualities field) | 83% |
| Format parameters | 5 | 0 | 100% |
| CORS | 5 | 0 | 100% |
| HTTP behavior | 10 | 3 (304, operation order, fractional pct) | 77% |
| Identifiers | 2 | 3 (non-ASCII, ARK/URN, bug) | 40% |
| Sipi extensions | 19 | 76 (metadata, error handling, safety, performance, security, config, Lua API, connection, format edge cases, watermark, concurrency, resource limits, memory/OOM, Python-only) | 20% |
| **Total** | **96** | **92** | **51%** |

**Notes:**
- 2 of the 92 gaps are sipi bugs with `#[ignore]`'d tests (DEV-6003, DEV-6004), not missing coverage
- 6 metadata gaps (EXIF, XMP, ICC, IPTC, SipiEssentials, CLI metadata) are high-priority — metadata drift is silent and caught late
- 6 error handling gaps (corrupt images, Lua errors, empty uploads, config, double-encoding, long URLs) — these are crash/hang risks in production
- 7 security gaps (JWT, decompression bombs, upload limits, CRLF injection, cache poisoning, info disclosure, slowloris) — these protect against targeted attacks
- 6 configuration gaps (parseSizeString, deprecated keys, CLI overrides, jwt_secret, invalid Lua, nonexistent paths) — config parsing is almost entirely untested
- 5 Lua API gaps (SImage methods, JWT round-trip, UUID round-trip, HTTP client errors, error propagation) — Lua API tested only as black-box HTTP
- 5 connection handling gaps (keep-alive, chunked transfer, Connection: close, thread pool exhaustion, graceful shutdown) — the HTTP server layer has zero direct tests
- 6 format edge case gaps (CMYK/CIELab/16-bit through IIIF pipeline, progressive JPEG, TIFF-JPEG compression, 1-bit TIFF) — unit tests exist but HTTP-level pipeline untested
- 4 concurrency gaps (cache write contention, eviction during read, parallel uploads, Lua state isolation) — `SipiCache` has mutexes and `blocked_files` but zero contention testing
- 4 resource limit gaps (cache disabled mode, LRU purge correctness, nfiles limit, keep-alive timeout) — cache eviction and connection timeout logic exist but are completely untested
- 5 memory/OOM gaps (sustained load growth, concurrent large decode, decode accounting, intermediate buffer accumulation, cache as relief) — **production OOM issue:** no pixel limit, no memory cap, each request allocates nx*ny*nc*bps/8 bytes unbounded
- 4 watermark gaps (HTTP-level application, restrict+watermark combo, cache key separation, CLI watermark) — unit-tested but not through HTTP
- 3 cross-cutting gaps (ASan, TSan, performance) are build/measurement infrastructure, not new test functions
- Extension gap count includes Python-only features critical to preserve during Rust migration
- 107 individual test functions cover the 96 requirements (many tests cover multiple requirements)

## Technical Approach

### Architecture

The testing strategy sits above the individual test frameworks as a governing document. It defines:

1. **Where each test type lives** (directory mapping)
2. **When to use each layer** (decision tree)
3. **What the coverage matrix looks like** (IIIF spec mapping)
4. **How tests evolve during Rust migration** (freeze/migrate/new policies)

### Test Decision Tree

```
New test needed?
├── Is it testing a pure function/parser?
│   ├── C++ component not yet migrated → maintain existing GoogleTest (no new suites)
│   └── Rust component → #[test] + proptest for property-based
├── Is it testing HTTP API behavior?
│   ├── Simple status/header check (no body logic, no setup) → Hurl
│   └── Any of: body inspection, multi-step flow, snapshot, file setup → Rust e2e
├── Is it regression detection for complex output?
│   └── insta snapshot (JSON structure, headers) — this is a Rust e2e test with insta
├── Is it testing untrusted input handling?
│   └── Fuzz test (libFuzzer or cargo-fuzz)
├── Is it testing image output correctness?
│   └── Rust e2e with `image` crate decode + dimension/checksum verification
└── Does it need filesystem setup or custom server config?
    └── Rust e2e with tempfile + custom SipiServer::start config
```

**Clarifications:**
- "Image output correctness" tests are a specialization of e2e contract tests, not a separate layer
- Snapshot tests (`insta`) live inside Rust e2e test files — they are e2e tests that use snapshot assertions
- Tests needing cache verification require a cache-enabled server config (use `sipi.test-config.lua` which has cache configured)
- Python e2e tests (`test/e2e/`) still run in CI but are frozen — no new Python tests. See Phase 5 for deprecation timeline

### Cross-Cutting: Memory Safety (Sanitizer Builds)

Memory leaks and undefined behavior are not a separate pyramid layer but a **build variant** that runs existing tests with compiler instrumentation. This is critical for sipi as a long-running C++ server where leaks accumulate.

**Current state:** Only the fuzz harness uses `-fsanitize=address`. Unit tests and e2e tests run without any memory safety instrumentation. Known leak: `SipiFilenameHash::operator=` (DEV-6002).

**Sanitizer stack:**

| Sanitizer | Catches | Flag | Overhead |
|-----------|---------|------|----------|
| ASan (AddressSanitizer) | Buffer overflow, use-after-free, double-free, memory leaks (via LSan) | `-fsanitize=address` | ~2x slowdown |
| UBSan (UndefinedBehaviorSanitizer) | Integer overflow, null deref, misaligned access | `-fsanitize=undefined` | ~1.2x slowdown |
| TSan (ThreadSanitizer) | Data races, deadlocks | `-fsanitize=thread` | ~5-15x slowdown |

**Prerequisite — standardize on Clang across all build environments:**
Currently GCC is used in two places: (1) the Dockerfile via `build-essential`, and (2) the Nix `default` devShell on Linux (`flake.nix` line 98: `default = if isDarwin then clang else gcc`). Switching both to Clang:

1. **Dockerfile:** Replace `build-essential` with `clang`, set `CC=clang CXX=clang++`
2. **flake.nix:** Change `default = if pkgs.stdenv.isDarwin then clang else gcc` to `default = clang`

This simplifies sanitizer integration (Clang's ASan/UBSan/TSan are more mature), aligns all build environments (Docker, Nix CI, Nix local, Zig all use Clang), and eliminates "builds on GCC but warns on Clang" discrepancies.

**Strategy:**
1. Switch Dockerfile from GCC to Clang (prerequisite)
2. Add CMake option `ENABLE_SANITIZERS` → sets `-fsanitize=address,undefined` on all targets
3. Nightly CI job: run unit tests + start sipi with sanitizers, run e2e suite, check for leaks at shutdown
4. Not in PR CI (too slow) — sanitizer builds are 2-3x slower than normal
5. TSan as optional nightly variant (concurrent request handling is a known risk area)

**Rust migration relevance:** As components migrate to Rust, memory safety is guaranteed by the compiler. Sanitizers become less necessary but remain valuable for C++↔Rust FFI boundaries during the transition period.

### Cross-Cutting: Performance Regression Detection

Performance regressions in image processing are silent — they don't break functional tests but degrade user experience. Sipi currently has no performance measurement infrastructure.

**Current state:** Prometheus metrics cover only cache counters/gauges. No request duration histogram, no benchmark suite, no CI-based performance regression detection.

**Strategy (three tiers):**

| Tier | What | Tool | When |
|------|------|------|------|
| Smoke latency | E2e tests assert response time < threshold | Rust `Instant::now()` in e2e tests | PR CI (fast, catches gross regressions) |
| Load baseline | Throughput measurement against standard workload | `wrk` or `hey` (HTTP load tester) | Nightly CI (measures req/s, p50/p99 latency) |
| Component benchmarks | Micro-benchmarks for parsers, image decode | `criterion` (Rust, future) | Post-Rust-migration, nightly |

**Smoke latency thresholds (proposed):**
- Info.json request: < 50ms
- 512×512 JPEG delivery (cache miss): < 500ms
- 512×512 JPEG delivery (cache hit): < 100ms
- These are NOT strict SLAs — they catch gross regressions (10x slowdown), not subtle changes

**Server-side enhancement (future):** Add `sipi_request_duration_seconds` Prometheus histogram to `SipiMetrics.h`. This enables monitoring in production and more precise CI measurement.

**Rust migration relevance:** `criterion` benchmarks become available once parsers and image handling are in Rust. During migration, e2e latency tests validate the Rust implementation doesn't regress against C++ performance.

### Implementation Phases

#### Phase 1: Document the Strategy

- [ ] Create `docs/src/development/testing-strategy.md` with the full pyramid, layer definitions, decision tree, IIIF coverage matrix, and `last_reviewed: YYYY-MM-DD` header for staleness tracking
- [ ] Add `testing-strategy.md` to mkdocs nav in `docs/mkdocs.yml` (after "Fuzz Testing", before "Downstream Dependencies")
- [ ] Update `CLAUDE.md` to reference testing-strategy.md from the Testing section
- [ ] Update `docs/src/development/reviewer-guidelines.md` to include testing strategy compliance as a review checkpoint

#### Phase 2: Close IIIF Spec Gaps (E2E Contract)

New tests in `test/e2e-rust/tests/iiif_compliance.rs`:

- [ ] `size_upscale_max` — test `^max` returns larger-than-original (or 501 if unsupported)
- [ ] `size_upscale_height` — test `^,h` height-only upscale
- [ ] `size_upscale_exact` — test `^w,h` exact dimensions with upscale
- [ ] `size_upscale_confined` — test `^!w,h` confined upscale
- [ ] `size_upscale_percent` — test `^pct:150` returns 150% of original (or 501)
- [ ] `region_dimension_verification` — decode returned image, verify pixel dimensions match requested region
- [ ] `size_dimension_verification` — decode returned image, verify output dimensions match requested size
- [ ] `rotation_dimension_verification` — verify 90° rotation swaps width/height
- [ ] `operation_ordering` — verify region→size→rotation order (e.g. crop then scale, not scale then crop)
- [ ] `fractional_percent_region` — test `pct:0.5,0.5,99.0,99.0`
- [ ] `conditional_request_304` — send `If-Modified-Since` with cached timestamp, expect 304
- [ ] `extra_qualities_in_info_json` — verify `extraQualities` field lists supported qualities
- [ ] `id_non_ascii` — test URL-encoded non-ASCII characters in identifier
- [ ] Fix `region_zero_width` — current test asserts 200, IIIF spec says SHOULD return 400. Either fix test or document non-compliance.

**Image decode dependency:** Add `image` crate to `test/e2e-rust/Cargo.toml` as `[dependencies]` (project convention — all test-only crates use `[dependencies]` since the entire crate is a test harness):
```toml
[dependencies]
image = { version = "0.25", default-features = false, features = ["jpeg", "png", "tiff"] }
```

#### Phase 3: Close Sipi Extension Gaps

Port remaining Python-only features and fill extension coverage holes:

New tests across `test/e2e-rust/tests/`:

- [ ] `cache_metrics` in new `tests/cache.rs` — make requests, verify `/metrics` shows cache counters change
- [ ] `cache_api_routes` in `tests/cache.rs` — test GET/POST `/api/cache` routes
- [ ] `cli_file_conversion` in new `tests/cli.rs` — spawn `sipi --file` for JP2 decode/encode round-trips (port 2 Python tests: `test_iso_15444_4_decode_jp2`, `test_iso_15444_4_round_trip`)
- [ ] `prometheus_metrics` in `tests/server.rs` — GET `/metrics`, verify Prometheus format and key gauges
- [ ] `ssl_endpoints` in `tests/server.rs` — HTTPS requests, verify `id` uses `https://` scheme in info.json
- [ ] `large_file_range_requests` in `tests/range_requests.rs` — extend existing tests with 10MB file tests
- [ ] `metadata_preservation_upload` in `tests/upload.rs` — upload image with EXIF/XMP/ICC, download, verify metadata preserved
- [ ] `metadata_iiif_pipeline` in `tests/iiif_compliance.rs` — request image through IIIF pipeline (region+size+rotation), verify EXIF/XMP/ICC survive transforms. Use `insta` golden baseline for metadata to catch silent drift.
- [ ] `metadata_format_conversion` in `tests/upload.rs` — upload TIFF with full metadata, retrieve as JPEG/PNG/JP2, verify metadata preserved across format boundaries
- [ ] `metadata_essentials_roundtrip` in `tests/upload.rs` — verify SipiEssentials (original filename, mimetype, data checksum) survive upload→store→retrieve cycle via knora.json fields
- [ ] `cli_metadata_fidelity` in `tests/cli.rs` — `sipi --file` conversion preserves EXIF/XMP/ICC (golden baseline output)
- [ ] `mime_consistency` in `tests/server.rs` — test `/api/mimetest` endpoint (6 Python test cases)
- [ ] `thumbnail_generation` in `tests/server.rs` — test `/make_thumbnail` + `/convert_from_binaries`
- [ ] `restricted_image_reduction` in `tests/server.rs` — verify restricted image returned at reduced size (not just 401)
- [ ] `upload_4bit_palette_png` in `tests/upload.rs` — 4-bit palette PNG with alpha channel
- [ ] `temp_directory_cleanup` in `tests/server.rs` — verify old temp files are cleaned
- [ ] `corrupt_image_handling` in `tests/iiif_compliance.rs` — serve a truncated/corrupt image file, expect 500 (not crash). Use a deliberately truncated JP2 test fixture.
- [ ] `lua_route_error_handling` in `tests/server.rs` — trigger a Lua runtime error in a route handler, verify 500 response (not crash or hang)
- [ ] `empty_file_upload` in `tests/upload.rs` — upload a zero-byte file, verify graceful error
- [ ] `double_encoded_url` in `tests/iiif_compliance.rs` — request with `%252F` (double-encoded slash), verify correct handling
- [ ] `invalid_config_startup` in new `tests/config.rs` — spawn sipi with invalid config (malformed Lua syntax), verify clean error message and non-zero exit
- [ ] `config_nonexistent_paths` in `tests/config.rs` — start sipi with nonexistent `imgroot` or `cachedir`, verify graceful error (not crash)
- [ ] `config_deprecated_key_migration` in `tests/config.rs` — start sipi with old `cachedir` key, verify it works; start with both `cachedir` and `cache_dir`, verify error
- [ ] `config_cli_overrides` in `tests/config.rs` — start sipi with `--serverport`, `--loglevel` CLI flags, verify they override config file values (check via `/server` endpoint or port binding)
- [ ] `config_empty_jwt_secret` in `tests/security.rs` — start sipi with empty `jwt_secret`, verify JWT-authenticated endpoints behave correctly (reject or accept-without-auth)
- [ ] `parse_size_string_edge_cases` in `tests/config.rs` — test `parseSizeString` indirectly by starting sipi with `max_post_size = '0'`, `'-1'`, `'abc'` and verifying behavior. Note: direct unit testing of `parseSizeString` deferred until Rust migration exposes it as a testable Rust function
- [ ] `lua_image_crop_verify` in `tests/server.rs` — invoke a Lua route that crops an image via `SImage:crop()`, download result, verify dimensions match requested crop. Requires a new test Lua script in `test/_test_data/scripts/`
- [ ] `lua_image_scale_verify` in `tests/server.rs` — invoke Lua route that scales via `SImage:scale()`, verify output dimensions
- [ ] `lua_image_rotate_verify` in `tests/server.rs` — invoke Lua route that rotates via `SImage:rotate()`, verify dimension swap for 90°
- [ ] `lua_jwt_round_trip` in `tests/server.rs` — invoke Lua route that generates JWT via `server.generate_jwt` and decodes it back via `server.decode_jwt`, verify payload preservation
- [ ] `lua_uuid_round_trip` in `tests/server.rs` — invoke Lua route that generates UUID, converts to base62 and back, verify round-trip identity
- [ ] `lua_http_client_error_handling` in `tests/server.rs` — invoke Lua route that uses `server.http` to fetch an unreachable URL, verify graceful error (500 with message, not crash)
- [ ] `http_keep_alive` in new `tests/connection.rs` — open TCP connection, send two sequential requests on same connection, verify both responses received correctly. Use raw TCP (not reqwest, which manages connections automatically)
- [ ] `chunked_transfer_upload` in `tests/connection.rs` — send request with `Transfer-Encoding: chunked` body, verify server processes it correctly
- [ ] `connection_close_header` in `tests/connection.rs` — send request with `Connection: close`, verify server closes the TCP connection after response
- [ ] `graceful_shutdown` in `tests/connection.rs` — start sipi, send SIGTERM during an in-flight request, verify response completes (not truncated). Mark `#[ignore]` if timing-sensitive in CI
- [ ] `multipage_tiff_page_selection` in `tests/iiif_compliance.rs` — create multi-page TIFF test fixture, request `image.tif@2/full/max/0/default.jpg`, verify returned image differs from page 1. If NYI, document as known limitation and mark `#[ignore]`
- [ ] `cmyk_through_iiif_pipeline` in `tests/iiif_compliance.rs` — upload CMYK TIFF (fixture exists: `cmyk.tif`), request as JPEG via IIIF, decode and verify 3-channel sRGB output (not 4-channel CMYK)
- [ ] `cielab_through_iiif_pipeline` in `tests/iiif_compliance.rs` — upload CIELab TIFF, request as JPEG via IIIF, verify successful conversion (no 500 error)
- [ ] `16bit_through_iiif_pipeline` in `tests/iiif_compliance.rs` — upload 16-bit PNG, request as JPEG via IIIF, verify 8-bit output (JPEG only supports 8-bit)
- [ ] `progressive_jpeg_input` in `tests/iiif_compliance.rs` — create or source a progressive JPEG test fixture, request via IIIF, verify successful decode and response
- [ ] `tiff_jpeg_compression_input` in `tests/iiif_compliance.rs` — upload TIFF with JPEG compression (existing fixture: `tiffJpegScanlineBug.tif`), verify behavior. If known broken, document and mark `#[ignore]` with tracking issue
- [ ] `watermark_applied_via_http` in `tests/server.rs` — request identifier matching "watermarked" pattern, download image, verify it differs from non-watermarked version of same image (byte comparison or image decode difference)
- [ ] `restrict_plus_watermark` in `tests/server.rs` — request "restricted_watermarked" identifier, verify both size reduction (128x128) AND watermark applied
- [ ] `watermark_cache_separation` in `tests/cache.rs` — request same image with and without watermark, verify both responses cached separately (different `/metrics` cache_files count)
- [ ] `concurrent_cache_writes_same_key` in `tests/cache.rs` — send 10+ parallel requests for the same uncached image, verify no corruption (all responses identical bytes) and exactly 1 cache file created (tests `blocked_files` mutex in `SipiCache`)
- [ ] `cache_eviction_during_read` in `tests/cache.rs` — fill cache to capacity, then simultaneously request a cached image while uploading new images to trigger eviction; verify the in-flight response completes successfully (no truncated body or 500)
- [ ] `concurrent_file_uploads` in `tests/upload.rs` — send 10+ parallel POST uploads simultaneously, verify all succeed or fail gracefully (no crashes, no partial writes visible to other requests)
- [ ] `lua_state_thread_isolation` in `tests/server.rs` — send parallel requests that each set a Lua global variable to a unique value, verify responses reflect the correct per-request value (no cross-request Lua state leakage)
- [ ] `cache_disabled_mode` in `tests/cache.rs` — start sipi with `cache_size='0'`, request an image, verify 200 response and `/metrics` shows 0 cache files (every request served from source)
- [ ] `cache_lru_purge_correctness` in `tests/cache.rs` — start sipi with very small `cache_size` (e.g. '1M') and `cache_nfiles=5`, request 10+ different images to exceed limits, verify `/metrics` shows cache_files dropped to ~80% of limit after purge and `cache_evictions_total > 0`
- [ ] `cache_nfiles_limit` in `tests/cache.rs` — start sipi with `cache_nfiles=3`, request 5 different images, verify `/metrics` cache_files never exceeds 3 (purge triggered on file count)
- [ ] `keepalive_timeout_enforcement` in `tests/connection.rs` — open a keep-alive connection, send one request, idle for longer than `keep_alive_timeout`, verify connection is closed by server (attempt second request fails)
- [ ] `sustained_load_memory_growth` in new `tests/resource_limits.rs` — send 100+ sequential requests for large images (use existing test fixtures), monitor `/metrics` and assert no unbounded memory growth (RSS via `/proc/self/status` or similar); mark `#[ignore]` for CI if too slow
- [ ] `concurrent_large_image_decode` in `tests/resource_limits.rs` — send `nthreads` simultaneous requests for the largest test image, verify all succeed (no OOM crash) and server remains responsive afterward
- [ ] `cache_hit_avoids_decode` in `tests/cache.rs` — request an image twice, verify second request (cache hit) responds faster and does not trigger image decode (check via `/metrics` cache_hits counter increment without cache_misses increment)
- [ ] `transform_pipeline_memory` in `tests/resource_limits.rs` — request large image with region+size+rotation+quality transforms, verify server completes without crash; this exercises the worst-case memory path (multiple intermediate buffers)
- [ ] `jwt_expired_token` in new `tests/security.rs` — send request with expired JWT, expect 401 (not 500 or crash)
- [ ] `jwt_alg_none_bypass` in `tests/security.rs` — send JWT with `alg: none` and no signature, expect rejection (common JWT vulnerability)
- [ ] `jwt_tampered_payload` in `tests/security.rs` — modify JWT payload without re-signing, verify signature validation rejects it
- [ ] `decompression_bomb_rejection` in `tests/security.rs` — upload a small image crafted to decompress to extreme dimensions (e.g. 100000×100000), verify server rejects or handles gracefully without OOM
- [ ] `upload_size_enforcement` in `tests/upload.rs` — send POST body exceeding `max_post_size`, verify 413 or appropriate error (tests `Connection.cpp` enforcement)
- [ ] `crlf_header_injection` in `tests/security.rs` — request identifier containing `%0d%0a` (CRLF), verify no response header injection
- [ ] `cache_key_isolation` in `tests/cache.rs` — make requests with different IIIF parameters, verify each produces a distinct cache entry (no cache poisoning via canonical collision)
- [ ] `error_no_path_disclosure` in `tests/security.rs` — trigger a server error (e.g. missing file), verify response body does not leak internal filesystem paths
- [ ] `slowloris_resilience` in `tests/security.rs` — open connection, send partial request headers slowly, verify server doesn't hang (may need timeout; mark `#[ignore]` if flaky in CI)

#### Phase 4: Property-Based Testing Foundation (Rust Migration Prep)

Prepare the infrastructure for property-based testing that will be used during Rust migration:

- [ ] Add `proptest` to `test/e2e-rust/Cargo.toml` as `[dependencies]` (project convention — entire crate is test-only)
- [ ] Create `tests/proptest_iiif_uri.rs` — property-based tests for IIIF URL parsing:
  - Valid URIs always return 200 or known error codes
  - Random region/size/rotation combinations never crash the server
  - URL encoding/decoding round-trips
- [ ] Add `rstest` for parameterized test patterns (reducing boilerplate in status-code tests)
- [ ] Switch Dockerfile from GCC to Clang: replace `build-essential` with `clang`, set `CC=clang CXX=clang++`
- [ ] Switch Nix default devShell to Clang on Linux: `flake.nix` line 98, change `default = clang` (remove platform conditional)
- [ ] Add `ENABLE_SANITIZERS` CMake option: `-fsanitize=address,undefined` on all targets when enabled
- [ ] Add `make nix-test-sanitized` target: build with sanitizers, run unit tests, check for leaks
- [ ] Add nightly CI job: sanitizer build + e2e test suite (sipi started with ASan, verify clean shutdown)
- [ ] Triage existing ASan findings (expect `SipiFilenameHash::operator=` leak, potentially others)
- [ ] Add smoke latency assertions to key e2e tests: info.json < 50ms, image delivery < 500ms (cache miss), < 100ms (cache hit)
- [ ] Add nightly CI job: `wrk` load test against standard workload, record throughput baseline in CI artifacts
- [ ] (Optional) Add `sipi_request_duration_seconds` Prometheus histogram to `SipiMetrics.h` for production monitoring

#### Phase 5: Python Test Deprecation

Systematically retire Python e2e tests once Rust parity is confirmed per test file:

- [ ] Create parity checklist: for each Python test function, identify its Rust equivalent
- [ ] Verify parity: both suites pass in CI for 2+ weeks with no Rust-only regressions
- [ ] Remove `test/e2e/test_01_conversions.py` after `tests/cli.rs` covers both conversion tests (`test_iso_15444_4_decode_jp2`, `test_iso_15444_4_round_trip`)
- [ ] Remove `test/e2e/test_02_server.py` after `tests/server.rs` covers all 32 test functions
- [ ] Remove `test/e2e/test_03_iiif.py` after IIIF validator integration (or explicit exclusion). Note: this file's single test calls an external IIIF validator binary and is effectively a no-op when the binary is unavailable.
- [ ] Remove `test/e2e/test_04_range_requests.py` after `tests/range_requests.rs` covers large-file cases
- [ ] Remove `test/e2e/conftest.py` and `requirements.txt` when all Python tests are retired
- [ ] Remove `make nix-test-e2e` Python target from Makefile (keep `make rust-test-e2e`)
- [ ] Update CI workflow to drop Python test step

**Special case — `test_iiif_auth_api`:** This Python test (~180 lines) exercises Lua custom auth flows (cookie-based sessions, IIIF Auth-like endpoints). While IIIF Auth is excluded from the strategy, the custom Lua auth contract is production-critical and must be ported before `test_02_server.py` removal.

**Gate:** No Python test file is removed until its Rust counterpart covers every test scenario from that file. A tracking checklist with per-function mapping is required before removal.

### CI Integration

| Phase | Make Target | Runs In | Notes |
|-------|-----------|---------|-------|
| Existing | `make nix-test` | PR CI | C++ unit tests via ctest |
| Existing | `make rust-test-e2e` | PR CI | Rust e2e tests (includes snapshots) |
| Existing | `make hurl-test` | PR CI | Hurl contract tests |
| Existing | `make nix-test-e2e` | PR CI | Python e2e (until deprecated) |
| Phase 2 | `make rust-test-e2e` | PR CI | New IIIF tests land in existing target |
| Phase 3 | `make rust-test-e2e` | PR CI | Extension tests in same target |
| Phase 4 | `make rust-test-e2e` | PR CI | proptest runs with default 256 cases |
| Phase 4 | — | Nightly | proptest with 10000 cases (optional) |
| Existing | fuzz.yml | Nightly | libFuzzer corpus growth |
| Phase 4 | `make nix-test-sanitized` | Nightly | ASan+UBSan unit tests, leak check |
| Phase 4 | — | Nightly | ASan e2e: start sipi with sanitizers, run full e2e suite |
| Phase 4 | — | PR CI | Smoke latency assertions in e2e tests (< 500ms thresholds) |
| Phase 4 | — | Nightly | `wrk` load test: throughput baseline recording |

## Alternative Approaches Considered

### 1. Spec-driven test generation

Auto-generate tests from a machine-readable IIIF spec. **Rejected:** The IIIF spec isn't available in a machine-readable test format, and auto-generated tests tend to be shallow (status code only) without semantic verification.

### 2. Full Python→Rust port before strategy

Port all Python e2e tests first, then define strategy. **Rejected:** This inverts the dependency — the strategy should guide the port, not the other way around. The port is already ~80% complete.

### 3. Separate test repo

Put all tests in a standalone repository that tests sipi as a black box. **Rejected:** Adds CI complexity, harder to keep in sync, and C++ unit tests must live in the repo.

### 4. Contract-testing framework (Pact)

Use consumer-driven contract testing. **Rejected:** Sipi is a standard (IIIF) implementation, not a service with negotiated contracts. The IIIF spec IS the contract.

## Acceptance Criteria

### Functional Requirements

- [ ] `docs/src/development/testing-strategy.md` exists with full pyramid, layer definitions, decision tree, IIIF coverage matrix, and Python deprecation plan
- [ ] Testing strategy is linked from `CLAUDE.md` and `reviewer-guidelines.md`
- [ ] All 92 identified gaps (90 true gaps + 2 sipi bugs) have either a test, a tracking issue, or an explicit "deferred" note with rationale
- [ ] `image` crate added for dimension verification tests
- [ ] At least 3 dimension-verification tests pass (region, size, rotation)
- [ ] `region_zero_width` discrepancy resolved (test matches spec, or non-compliance documented)
- [ ] IIIF Auth exclusion documented in strategy doc with rationale
- [ ] `insta` snapshot review workflow documented (how to run `cargo insta review`, when to accept changes)

### Non-Functional Requirements

- [ ] Strategy document is self-contained — a new contributor can understand the test architecture from it alone
- [ ] Strategy document contains: pyramid diagram, layer table, decision tree, full IIIF coverage matrix, extension coverage matrix, Python deprecation timeline
- [ ] IIIF coverage matrix is structured as a markdown table for easy grep/update
- [ ] Decision tree handles ambiguous cases with clarifying examples

### Quality Gates

- [ ] All existing tests still pass (`make nix-test && make rust-test-e2e && make hurl-test`)
- [ ] `cargo clippy --tests` clean
- [ ] `cargo fmt --check` clean
- [ ] New tests verify behavior (dimensions, content, structure), not just status codes

## Success Metrics

| Metric | Current | Target | Measurement |
|--------|---------|--------|-------------|
| IIIF spec coverage | 51% (96/188 requirements) | 90%+ | Coverage matrix in strategy doc |
| Rust e2e test count | 107 | 130+ | `cargo test -- --list \| grep test \| wc -l` |
| Extension test coverage | 20% (19/95 features) | 85%+ | Coverage matrix |
| Dimension-verifying tests | 0 | 5+ | Tests using `image` crate |
| Python-only test scenarios | ~20 (of 52 total Python tests, ~32 already covered by Rust) | 0 | Parity checklist (Phase 5) |
| Strategy doc exists | No | Yes | File at documented path |

## Dependencies & Prerequisites

- PR #519 (test coverage) must be merged first — this plan builds on it
- `image` crate availability (well-maintained, 50M+ downloads)
- `proptest` crate for property-based testing foundation
- Clang compiler standardization: Docker images + Nix Linux devShell (Phase 4 prerequisite for sanitizer work)

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `image` crate decode differs from sipi output | M | M | Use tolerance for dimension checks, not byte-exact comparison |
| Strategy doc becomes stale | M | H | AI reviewers check against it; add "last reviewed" date |
| Dimension verification flaky on CI | L | M | Use known test images with exact expected dimensions |
| proptest tests slow down CI | L | L | Limit proptest cases in CI, full suite in nightly |
| Strategy conflicts with migration decisions | L | H | Strategy explicitly defines freeze/migrate policies |
| Metadata drift undetected | H | H | Use `insta` golden baselines for metadata fingerprints; test metadata through IIIF pipeline, not just upload round-trip |
| Security gaps exploited before testing | M | H | Prioritize JWT validation and decompression bomb tests (Phase 3); `max_post_size` is already enforced but untested |
| OOM under sustained harvesting load | H | H | **Active production issue.** No pixel limit or memory cap on image decode — `nx*ny*nc*bps/8` bytes per request, unbounded. Add `resource_limits.rs` tests and consider `max_decode_pixels` config knob as mitigation |

## Resource Requirements

- **Phase 1 (Strategy doc):** ~2 hours — document writing, cross-referencing
- **Phase 2 (IIIF gaps):** ~6 hours — 14 new tests/fixes with `image` crate integration
- **Phase 3 (Extension gaps):** ~36 hours — 72 new tests across 13 test files + new Lua test scripts (metadata, error handling, security, config, Lua API, connection, format edge cases, watermark, concurrency, resource limits, memory/OOM)
- **Phase 4 (Proptest + sanitizers + perf):** ~7 hours — proptest infrastructure, Clang switch, sanitizer CMake option, perf baselines, nightly CI jobs
- **Phase 5 (Python deprecation):** ~4 hours — parity checklist, verification, removal

## Future Considerations

### Rust Migration Testing Path

When a C++ component is migrated to Rust:

1. **Before migration:** Ensure e2e contract tests cover the component's behavior
2. **During migration:** Write Rust unit tests (`#[test]`, `proptest`) for the new implementation
3. **After migration:** Existing e2e tests validate the Rust implementation matches C++ behavior
4. **Cleanup:** Remove corresponding C++ unit tests (they tested the old implementation)

The `insta` golden baselines are critical here — they capture exact C++ server behavior and will detect any Rust implementation drift.

### Doc Tests

Once sipi has Rust library code (post-migration), doc tests (`///` examples) become valuable for API documentation. The strategy should be updated to include a doc test layer.

### Benchmark Tests (criterion)

Phase 4 adds e2e smoke latency assertions and `wrk` load testing for coarse-grained regression detection. Fine-grained `criterion` micro-benchmarks for performance-critical paths (image decode, IIIF parse, ICC conversion) should be added during or after Rust migration when component code exists as Rust library crates.

## Documentation Plan

1. **New:** `docs/src/development/testing-strategy.md` — the primary deliverable
2. **Update:** `docs/src/development/developing.md` — link to strategy doc from "Writing Tests" section
3. **Update:** `CLAUDE.md` — reference testing-strategy.md in Testing section
4. **Update:** `docs/src/development/reviewer-guidelines.md` — add testing strategy compliance checkpoint

## Execution Order

1. Phase 1 (strategy doc) — must come first, defines everything else
2. Phase 2 (IIIF gaps) — highest value, closes spec compliance holes
3. Phase 3 (extension gaps) — broadens coverage
4. Phase 4 (proptest) — forward-looking investment
5. Phase 5 (Python deprecation) — runs after Phases 2-3 achieve Rust parity per file

## References & Research

### Internal References

- Previous coverage plan: `dasch-specs/specs/2026-03-08-sipi-test-coverage/01-feat-sipi-test-coverage-plan.md`
- IIIF compliance plan: `dasch-specs/specs/2026-03-08-sipi-test-coverage/02-feat-sipi-iiif-compliance-plan.md`
- Review findings plan: `dasch-specs/specs/2026-03-08-sipi-test-coverage/03-fix-sipi-test-review-findings-plan.md`
- Current test infrastructure: `sipi/test/` (unit, e2e, e2e-rust, hurl, smoke, approval, fuzz)
- IIIF endpoint implementation: `sipi/src/SipiHttpServer.cpp:849-858`

### External References

- Rust Testing Guide: https://blog.blackwell-systems.com/posts/rust-testing-comprehensive-guide/
- IIIF Image API 3.0: https://iiif.io/api/image/3.0/
- insta snapshot testing: https://insta.rs/
- proptest property-based testing: https://proptest-rs.github.io/proptest/
- image crate: https://docs.rs/image/

### Institutional Learnings

- `learnings/logic-errors/sipi-cache-auto-creation-head-request-empty-response.md` — HEAD request handling requires flush on all serve paths
- `learnings/test-failures/alphabetical-test-data-masks-ordering.md` — use non-alphabetical test data to catch ordering bugs
- `learnings/configuration-errors/github-actions-composite-action-main-ref-pr-isolation.md` — inline CI steps over composite actions
