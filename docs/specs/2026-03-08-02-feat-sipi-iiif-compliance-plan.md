---
title: "feat: IIIF Image API 3.0 full compliance"
type: feat
date: 2026-03-08
author: "subotic"
status: reviewed(5)
repository: dasch-swiss/sipi
---

# IIIF Image API 3.0 Full Compliance

## Overview

Build the **test infrastructure for full IIIF Image API 3.0 Level 2 compliance** (spec: https://iiif.io/api/image/3.0/). This plan creates a comprehensive Rust e2e test suite covering every required feature from the IIIF validator. Tests that reveal implementation gaps are marked `#[ignore]` with a TODO — fixing those gaps is follow-up work tracked separately.

**Sipi has two roles**, and this plan covers testing for both:

1. **IIIF Image API server** — serves images over HTTP with region/size/rotation/quality/format processing. Phases 1-8 cover this.
2. **CLI tool for the long-term preservation chain** — used by RDU (Research Data Unit) to prevalidate images before archival, and as a gatekeeper in the Archive Ingest process to validate every image entering the repository. This role requires that sipi preserves **all** image metadata (EXIF, ICC, XMP, IPTC, SipiEssentials) through read→process→write cycles, not just pixels. Phase 12 covers this.

**Scope exclusion: IIIF Auth API.** The Authentication specification is explicitly excluded. It has edge cases that cannot work correctly, and we don't use it. Auth-related tests from the Python e2e suite (`test_iiif_auth_api`) are not ported.

This plan also consolidates overlapping test layers (Hurl, `server.rs`, `iiif_compliance.rs`) identified by code review.

## Problem Statement / Motivation

Sipi claims **Level 2** IIIF Image API 3.0 compliance (`profile: "level2"`) and advertises 17 extra features in its `info.json`. However:

- **Our own tests verify only ~40% of Level 2 required features** — 8 of 20 required capabilities are untested in our Rust e2e suite
- **The external IIIF validator is opaque** — `test_03_iiif.py` delegates to `iiif-validate.py` which runs as a black box. If it fails, we don't know which specific feature broke
- **The validator is a dead-end dependency** — it requires Python, Pillow, ImageMagick, and python-magic. It won't survive the Rust migration
- **Test overlap exists** — `server.rs`, `iiif_compliance.rs`, and Hurl tests redundantly cover ~8 endpoints (status-code-only checks on the same URLs)
- **The IIIF validator defines 42 tests** across 7 categories — we replicate only 8 of them

### Official IIIF Validator Test List

Source: https://image-validator.iiif.io/list_tests (the authoritative online validator, replaces the older `iiif-validate.py` CLI tool)

| Category | Test Name | Description | Our Coverage |
|----------|-----------|-------------|:------------:|
| **Identifier** | `id_basic` | Image is returned | Yes |
| | `id_squares` | Correct image returned | No |
| | `info_json` | Check Image Information | Partial |
| | `id_escaped` | Escaped characters processed | No |
| | `id_error_escapedslash` | Forward slash gives 404 | No |
| | `id_error_random` | Random identifier gives 404 | Yes |
| | `id_error_unescaped` | Unescaped identifier gives 400 | No |
| **Region** | `region_pixels` | Region specified by pixels | No |
| | `region_percent` | Region specified by percent | Yes |
| | `region_error_random` | Random region gives 400 | No |
| **Size** | `size_wh` | Size specified by w,h | No |
| | `size_wc` | Size specified by w, | Yes |
| | `size_ch` | Size specified by ,h | Yes |
| | `size_percent` | Size specified by percent | No |
| | `size_bwh` | Size specified by !w,h | No |
| | `size_up` | Size greater than 100% | No |
| | `size_region` | Region at specified size | No |
| | `size_error_random` | Random size gives 400 | No |
| **Rotation** | `rot_full_basic` | Rotation by 90 degree values | No |
| | `rot_region_basic` | Rotation of region by 90 degree values | No |
| | `rot_full_non90` | Rotation by non 90 degree values | No |
| | `rot_region_non90` | Rotation of region by non 90 degree values | No |
| | `rot_mirror` | Mirroring | Yes |
| | `rot_mirror_180` | Mirroring plus 180 rotation | No |
| | `rot_error_random` | Random rotation gives 400 | No |
| **Quality** | `quality_color` | Color quality | No |
| | `quality_grey` | Gray/Grey quality | Yes |
| | `quality_bitonal` | Bitonal quality | No |
| | `quality_error_random` | Random quality gives 400 | No |
| **Format** | `format_jpg` | JPG format | Yes |
| | `format_png` | PNG format | No |
| | `format_gif` | GIF format | No |
| | `format_jp2` | JPEG2000 format | No |
| | `format_tif` | TIFF format | No |
| | `format_pdf` | PDF format | No |
| | `format_webp` | WebP format | No |
| | `format_error_random` | Random format gives 400 | No |
| **HTTP** | `baseurl_redirect` | Base URL Redirects | No |
| | `cors` | Cross Origin Headers | No |
| | `jsonld` | JSON-LD Media Type | Partial |
| | `linkheader_profile` | Profile Link Header | No |
| | `linkheader_canonical` | Canonical Link Header | No |

**Coverage: 8 of 42 tests covered (19%). Target: 42 of 42 (100%).**

### Current Coverage Gap

**By IIIF validator test categories** (maps to the 42 tests above):

| Category | Validator Tests | Our Rust Tests | Gap |
|----------|:--------------:|:--------------:|:---:|
| Identifier (7 tests) | 7 | 2 | 5 |
| Region (3 tests) | 3 | 1 | 2 |
| Size (8 tests) | 8 | 2 | 6 |
| Rotation (7 tests) | 7 | 1 | 6 |
| Quality (4 tests) | 4 | 1 | 3 |
| Format (8 tests) | 8 | 1 | 7 |
| HTTP (5 tests) | 5 | 0 | 5 |
| **Total** | **42** | **8** (+ 2 partial) | **34** |

Note: "Our Rust Tests" counts only tests that directly map to a validator test with equivalent coverage. Several existing tests partially cover a validator test but don't fully replicate it (e.g., `info_json` tests exist but don't validate all fields).

## Technical Considerations

### Sipi's IIIF Implementation

Key source files:
- `src/SipiHttpServer.cpp:590-860` — info.json generation, CORS headers, Link headers, content negotiation, base URI redirect
- `src/handlers/iiif_handler.cpp` — IIIF URL parsing and image processing pipeline
- `shttps/Connection.cpp:410-420` — CORS preflight handling
- `include/iiifparser/` — SipiRegion, SipiSize, SipiRotation, SipiQualityFormat, SipiIdentifier parsers

Sipi claims these features in info.json:
```
extraFeatures: [baseUriRedirect, canonicalLinkHeader, cors, jsonldMediaType, mirroring,
  profileLinkHeader, regionByPct, regionByPx, regionSquare, rotationArbitrary,
  rotationBy90s, sizeByConfinedWh, sizeByH, sizeByPct, sizeByW, sizeByWh, sizeUpscaling]
extraFormats: [tif, jp2]
preferredFormats: [jpg, tif, jp2, png]
```

### Test Infrastructure

The Rust e2e harness (`test/e2e-rust/`) provides:
- `SipiServer::start()` — spawns sipi with test config, waits for readiness
- `client()` — `reqwest::blocking::Client` with self-signed cert support
- `OnceLock<SipiServer>` — single server instance shared across tests
- Port allocation via `AtomicU16` for parallel safety

### Reviewer Findings to Address

From the previous PR's code review:
1. **Test overlap**: `server.rs` largely duplicates `iiif_compliance.rs` + Hurl — consolidate
2. **`insta` dependency unused** — ~~remove from Cargo.toml~~ **Superseded**: decided to keep `insta` for snapshot-testing info.json and knora.json responses (Phase 2, Phase 10, Phase 12)
3. **`client()` creates new Client per call** — should reuse via OnceLock

### Known Sipi Implementation Gaps (Discovered During Planning)

1. **`profileLinkHeader` claimed but not implemented**: Sipi lists `profileLinkHeader` in `extraFeatures` (`SipiHttpServer.cpp:827`) but no code emits a `Link: <...level2.json>;rel="profile"` header. **Action**: Write the test, mark `#[ignore]` with TODO linking to a compliance fix task.

2. **GIF, PDF, WebP formats parsed but not served**: `SipiQualityFormat.cpp` parses these formats, but `iiif_handler.cpp:84` has a URL parser regex that only accepts `jpg|tif|png|jp2` — requests for other formats are rejected before reaching the format handler. The `default:` branch in `SipiHttpServer.cpp` throws `SipiError("Unsupported file format requested!")`. **Action**: First empirically verify the actual HTTP status code returned (likely 400 from URL parser, not 406/501 from format handler). `format_gif`, `format_pdf`, `format_webp` tests should expect whatever status sipi actually returns. Add a comment documenting the observed behavior.

3. **CORS echoes origin, not always `*`**: `Connection.cpp:415` echoes the request's `Origin` header. Without an `Origin` header, `SipiHttpServer.cpp:896` sends `*`. Tests must specify whether they send an `Origin` header.

4. **`reqwest` follows redirects by default**: The `base_uri_redirect` test needs a client with `redirect(Policy::none())` to observe the 303 status code and `Location` header. **Action**: Add `client_no_redirect()` helper to `common/mod.rs`.

5. **Content negotiation default**: Without an `Accept` header, sipi returns `Content-Type: application/json` with a Link header (per `SipiHttpServer.cpp:852-858`). With `Accept: application/ld+json`, it returns `Content-Type: application/ld+json;profile="..."`. Both are IIIF-compliant.

6. **Existing `base_uri_redirect` test is misnamed**: `iiif_compliance.rs:138` tests a full IIIF image URL, not the redirect behavior. Must be renamed to `full_iiif_url_returns_image` before adding the real redirect test.

7. **No test fixture for escaped identifiers**: Tests for `id_escaped`, `id_error_escapedslash`, `id_error_unescaped` need a file with special characters or a symlink. **Action**: Create `test/_test_data/images/unit/test%23image.jp2` (symlink to lena512.jp2).

### What the IIIF Validator Actually Tests

There are two versions of the validator:
- **Legacy**: `iiif-validate.py` (v1.0.5, PyPI package `iiif-validator`) — CLI tool used by our `test_03_iiif.py`
- **Current**: https://image-validator.iiif.io — web-hosted validator with the same 42 test definitions, also available as source from https://github.com/IIIF/image-validator

Both validators:
1. Fetch `info.json` and validate all required fields
2. For each supported feature, construct a specific IIIF URL and make an HTTP request
3. Download the resulting image and check pixel colors against expected values (using `do_test_square()`)
4. Validate HTTP headers (CORS, Link, Content-Type)
5. Test error cases with random invalid parameters

**Key insight**: Most validator tests only check HTTP status code + that a valid image is returned. Pixel-level color checking is limited to specific geometric tests (region, rotation). We can replicate >90% of the validator's coverage with HTTP status + content-type + content-length assertions. The remaining pixel checks can be added later with the `image` crate.

## Implementation Approach

### Phase 1: Consolidate Existing Tests

Address reviewer findings from the previous PR before adding new tests.

- [ ] Keep `insta` in `test/e2e-rust/Cargo.toml` — use it for snapshot-testing `info.json` responses and stable HTTP header structures in later phases
- [ ] Refactor `common/mod.rs`: store `reqwest::Client` in a `static CLIENT: OnceLock<Client>` for reuse across tests (follows redirects — default behavior)
- [ ] Add `client_no_redirect()` helper to `common/mod.rs` — separate `static CLIENT_NO_REDIRECT: OnceLock<Client>` with `redirect(Policy::none())` for testing 303 redirects. Two distinct `OnceLock` instances, not one shared one.
- [ ] Rename existing `base_uri_redirect` test to `full_iiif_url_returns_image` (it doesn't test redirect behavior)
- [ ] Fix existing `info_json_content_type` test: it asserts `application/ld+json` but reqwest doesn't send `Accept: application/ld+json` header, so sipi returns `application/json`. Either: (a) change assertion to `application/json`, or (b) add explicit `Accept: application/ld+json` header to match the test's intent. Both cases get proper tests in Phase 3.
- [ ] Move IIIF-related tests from `server.rs` into `iiif_compliance.rs`:
  - Move: `iiif_image_delivery_jpg`, `iiif_image_delivery_png`, `iiif_region_crop`, `iiif_percentage_size`, `iiif_rotation`, `info_json_structure`, `deny_unauthorized_image`, `invalid_iiif_url_*`, `head_iiif_image_empty_body`
  - Keep in `server.rs`: `file_access_*`, `video_knora_json`, `missing_sidecar_*`, `sqlite_api`, `lua_*`
- [ ] Identify Hurl tests that will be superseded by new Rust tests (for removal in Phase 13): `iiif_image_delivery.hurl`, `info_json.hurl`, `invalid_iiif_urls.hurl`, `deny_unauthorized.hurl`, `not_found.hurl`, `head_request.hurl`
- [ ] Document `SipiFilenameHash::operator=` memory leak in assignment operator test (Critical finding)
- [ ] Create test fixture for escaped identifier tests. **Preferred strategy**: symlink `ln -s lena512.jp2 "test/_test_data/images/unit/test#image.jp2"`. **Caveat**: `#` in filenames survives git on Linux/macOS but may cause issues on Windows/some CI. **Fallback**: create the symlink in a test setup script or `conftest.rs` fixture if git can't store it. Verify the symlink actually commits to git before proceeding.

### Phase 2: info.json Complete Validation

Replicate `info_json.py` from the IIIF validator — full field-by-field validation.

- [ ] Test `@context` field equals `"http://iiif.io/api/image/3/context.json"`
- [ ] Test `id` field contains correct base URI (http and https via X-Forwarded-Proto)
- [ ] Test `type` equals `"ImageService3"` (already exists — verify)
- [ ] Test `protocol` equals `"http://iiif.io/api/image"` (already exists — verify)
- [ ] Test `profile` equals `"level2"` (already exists — verify)
- [ ] Test `width` and `height` are positive integers matching known test image dimensions
- [ ] Test `sizes` array contains expected entries with correct width/height pairs
- [ ] Test `tiles` array contains entries with `width`, `height`, `scaleFactors`
- [ ] Test `extraFormats` contains `["tif", "jp2"]`
- [ ] Test `preferredFormats` contains `["jpg", "tif", "jp2", "png"]`
- [ ] Test `extraFeatures` contains all 17 claimed features
- [ ] Test full info.json equality against known-good expected JSON (like Python `test_json_info_validation`)
- [ ] Test info.json with `X-Forwarded-Proto: https` produces `https://` in `id` field

### Phase 3: HTTP Feature Tests

Replicate `cors.py`, `baseurl_redirect.py`, `jsonld.py`, `linkheader_canonical.py`, `linkheader_profile.py`.

- [ ] Test CORS on info.json without Origin header: response has `Access-Control-Allow-Origin: *` (always sent by `SipiHttpServer.cpp:896`)
- [ ] Test CORS on info.json with Origin header: response has `Access-Control-Allow-Origin: *` (info.json always sends `*`)
- [ ] Test CORS on image response with Origin header: response echoes Origin in `Access-Control-Allow-Origin` (via `Connection.cpp:415`)
- [ ] Test CORS on image response without Origin header: response has NO `Access-Control-Allow-Origin` header (not sent when no Origin)
- [ ] Test CORS preflight: OPTIONS request on image URL with `Origin`, `Access-Control-Request-Method`, and `Access-Control-Request-Headers` returns: `Access-Control-Allow-Origin` (echoes origin), `Access-Control-Allow-Methods` (GET, POST, PUT, DELETE), `Access-Control-Allow-Headers` (echoes request), `Access-Control-Allow-Credentials: true`. **Note**: all three request headers must be present — without them, `Connection.cpp:433` skips CORS processing entirely.
- [ ] Test base URI redirect: GET `/{prefix}/{identifier}` (no IIIF params) returns 303 with `Location` header pointing to `info.json` — **must use `client_no_redirect()`**
- [ ] Test JSON-LD media type: request with `Accept: application/ld+json` returns `Content-Type: application/ld+json;profile="http://iiif.io/api/image/3/context.json"`
- [ ] Test JSON-LD default: request without Accept header returns `Content-Type: application/json` with `Link: <...context.json>` header
- [ ] Test canonical link header: image response includes `Link: <...>;rel="canonical"`
- [ ] Test profile link header: response includes `Link: <http://iiif.io/api/image/3/level2.json>;rel="profile"` — **mark `#[ignore]`: sipi claims this feature but doesn't emit the header (compliance fix needed)**

### Phase 4: Region Tests

Replicate `region_pixels.py`, `region_percent.py`, `region_error_random.py` from the IIIF validator, plus `regionSquare` (Level 2 feature, no dedicated validator test).

- [ ] Test `full` region: `/{id}/full/max/0/default.jpg` → 200
- [ ] Test pixel region: `/{id}/0,0,100,100/max/0/default.jpg` → 200 (regionByPx)
- [ ] Test pixel region offset: `/{id}/50,50,200,200/max/0/default.jpg` → 200
- [ ] Test percent region: `/{id}/pct:10,10,50,50/max/0/default.jpg` → 200 (regionByPct)
- [ ] Test square region: `/{id}/square/max/0/default.jpg` → 200 (regionSquare)
- [ ] Test region beyond bounds is cropped (not error): `/{id}/400,400,9999,9999/max/0/default.jpg` → 200 (lena512: start at 400,400 within bounds, SipiRegion clamps width/height)
- [ ] Test region start beyond image bounds: `/{id}/600,600,100,100/max/0/default.jpg` → 400 (lena512 is 512x512, SipiRegion.cpp:111 throws error when x >= nx)
- [ ] Test zero-width region: `/{id}/0,0,0,100/max/0/default.jpg` → 400
- [ ] Test invalid region syntax: `/{id}/invalid/max/0/default.jpg` → 400

### Phase 5: Size Tests

Replicate `size_wc.py`, `size_ch.py`, `size_wh.py`, `size_bwh.py`, `size_percent.py`, `size_up.py`, `size_noup.py`, `size_region.py`, `size_error_random.py`.

- [ ] Test `max` size: `/{id}/full/max/0/default.jpg` → 200
- [ ] Test size by width: `/{id}/full/256,/0/default.jpg` → 200 (sizeByW)
- [ ] Test size by height: `/{id}/full/,256/0/default.jpg` → 200 (sizeByH)
- [ ] Test exact size: `/{id}/full/200,200/0/default.jpg` → 200 (sizeByWh)
- [ ] Test best fit: `/{id}/full/!200,200/0/default.jpg` → 200 (sizeByConfinedWh)
- [ ] Test percent size: `/{id}/full/pct:50/0/default.jpg` → 200 (sizeByPct)
- [ ] Test upscaling: `/{id}/full/^1000,/0/default.jpg` → 200 (sizeUpscaling)
- [ ] Test no-upscale beyond original: `/{id}/full/1000,/0/default.jpg` → 400 (`SipiSize.cpp:172` throws "Upscaling not allowed!" when size > original without `^` prefix). The IIIF spec allows either 400 or full size — sipi chooses 400.
- [ ] Test size after region: `/{id}/0,0,200,200/100,/0/default.jpg` → 200
- [ ] Test invalid size syntax: `/{id}/full/invalid/0/default.jpg` → 400

### Phase 6: Rotation Tests

Replicate `rot_full_basic.py`, `rot_full_non90.py`, `rot_mirror.py`, `rot_mirror_180.py`, `rot_region_basic.py`, `rot_region_non90.py`, `rot_error_random.py`.

- [ ] Test 0° rotation: `/{id}/full/max/0/default.jpg` → 200
- [ ] Test 90° rotation: `/{id}/full/max/90/default.jpg` → 200 (rotationBy90s)
- [ ] Test 180° rotation: `/{id}/full/max/180/default.jpg` → 200
- [ ] Test 270° rotation: `/{id}/full/max/270/default.jpg` → 200
- [ ] Test arbitrary rotation: `/{id}/full/max/45/default.png` → 200 (rotationArbitrary)
- [ ] Test mirror: `/{id}/full/max/!0/default.jpg` → 200 (mirroring)
- [ ] Test mirror + 180°: `/{id}/full/max/!180/default.jpg` → 200
- [ ] Test rotation after region: `/{id}/square/max/90/default.jpg` → 200
- [ ] Test invalid rotation: `/{id}/full/max/abc/default.jpg` → 400

### Phase 7: Quality and Format Tests

Replicate `quality_color.py`, `quality_grey.py`, `quality_bitonal.py`, `quality_error_random.py`, `format_jpg.py`, `format_png.py`, `format_tif.py`, `format_jp2.py`, `format_conneg.py`.

- [ ] Test `default` quality: `/{id}/full/max/0/default.jpg` → 200
- [ ] Test `color` quality: `/{id}/full/max/0/color.jpg` → 200
- [ ] Test `gray` quality: `/{id}/full/max/0/gray.jpg` → 200
- [ ] Test `bitonal` quality: `/{id}/full/max/0/bitonal.jpg` → 200 (sipi supports bitonal — `SipiQualityFormat.cpp` parses it and `SipiImage` converts to 1-bit)
- [ ] Test invalid quality: `/{id}/full/max/0/invalid.jpg` → 400
- [ ] Test JPG format: `/{id}/full/max/0/default.jpg` → 200, Content-Type `image/jpeg`
- [ ] Test PNG format: `/{id}/full/max/0/default.png` → 200, Content-Type `image/png`
- [ ] Test TIFF format: `/{id}/full/max/0/default.tif` → 200, Content-Type `image/tiff`
- [ ] Test JP2 format: `/{id}/full/max/0/default.jp2` → 200, Content-Type `image/jp2`
- [ ] Test GIF format (unsupported by sipi): `/{id}/full/max/0/default.gif` → empirically verify actual status code first (likely 400 from URL parser regex, but could be different). Document observed behavior.
- [ ] Test PDF format (unsupported by sipi): `/{id}/full/max/0/default.pdf` → same: verify empirically
- [ ] Test WebP format (unsupported by sipi): `/{id}/full/max/0/default.webp` → same: verify empirically
- [ ] Test completely unknown format: `/{id}/full/max/0/default.bmp` → 400

### Phase 8: Identifier and Error Handling Tests

Replicate `id_basic.py`, `id_escaped.py`, `id_error_*.py` and general error handling.

- [ ] Test basic identifier (`id_basic`): `/{id}/full/max/0/default.jpg` → 200
- [ ] Test identifier correctness (`id_squares`): verify returned image has correct dimensions (status + info.json width/height)
- [ ] Test identifier with subdirectory: `/unit/lena512.jp2/full/max/0/default.jpg` → 200
- [ ] Test escaped identifier (`id_escaped`): `/unit/test%23image.jp2/full/max/0/default.jpg` → 200 (requires symlink from Phase 1)
- [ ] Test escaped slash gives 404 (`id_error_escapedslash`): `/unit%2Flena512.jp2/full/max/0/default.jpg` → 404
- [ ] Test unescaped special chars give 400 (`id_error_unescaped`): **Note**: `reqwest` auto-encodes URLs, so unescaped `#` becomes a fragment and `?` becomes a query string — neither reaches the server as part of the path. Options: (a) use raw TCP/hyper to send a truly unescaped URL, (b) test with a character that reqwest doesn't normalize (e.g., space → `%20`), or (c) mark `#[ignore]` with a note that this requires a raw HTTP client. Decide approach during implementation.
- [ ] Test random identifier gives 404 (`id_error_random`): `/nonexistent-random-id/full/max/0/default.jpg` → 404
- [ ] Test missing identifier: `//full/max/0/default.jpg` → 400
- [ ] Test incomplete IIIF URL (missing params): `/{id}/full/max/default.jpg` → 400
- [ ] Test malformed IIIF URL: `/{id}/max/0/default.jpg` → 400

### Phase 9: Port Remaining Python Server Tests

Port tests from `test_02_server.py` not yet in Rust that aren't IIIF-specific but are valuable. These are lower priority than Phases 1-8 (IIIF compliance). If context or time is limited, defer to a follow-up PR.

**From `test_02_server.py`:**
- [ ] ~~IIIF Auth API~~ — **excluded from scope** (Auth spec has broken edge cases, not used by us)
- [ ] Test X-Forwarded-Proto http: info.json id uses `http://` — **skip if already covered by Phase 2's `X-Forwarded-Proto` test** (Phase 2 owns this for info.json)
- [ ] Test X-Forwarded-Proto https: info.json id uses `https://` — **skip if already covered by Phase 2** (Phase 2 owns this for info.json)
- [ ] Test MIME consistency check: POST `/api/mimetest` with known files (test_mimeconsistency)
- [ ] Test thumbnail generation: GET `/{id}/full/!128,128/0/default.jpg` with dimension verification (test_thumbnail)
- [ ] Test image format conversion round-trip: upload TIFF, request as JP2, verify valid image (test_image_conversion)
- [ ] Test EXIF orientation preservation: upload image with EXIF TopLeft, verify orientation is preserved (test_orientation_topleft)
- [ ] Test 4-bit palette PNG: ensure sipi handles low-color-depth PNG without error (test_4bit_palette_png)
- [ ] Test upscaling enforcement: request size larger than original, verify behavior (test_upscaling_server)
- [ ] Test concurrent requests: 10 parallel requests to same image, all succeed (test_concurrency)

**From `test_04_range_requests.py`:**
- [ ] Test large file range requests (10MB): port range request tests for both small (1KB) and large (10MB) files

### Phase 10: knora.json Endpoint Testing

`knora.json` is sipi's custom file metadata endpoint (`/{prefix}/{identifier}/knora.json`). It's the bridge between sipi and dsp-api — dsp-api calls it to get file properties (dimensions, checksums, original filename, MIME types). It is **not** part of the IIIF spec but is critical infrastructure for DaSCH.

Source: `SipiHttpServer.cpp:880-1062`. Returns different fields depending on file type (image, video, other). Reads sidecar `.info` files for video metadata and provenance checksums.

Some tests already exist in `upload.rs` and `server.rs` but they're scattered and incomplete.

#### knora.json for Images

- [ ] Test required fields present: `@context`, `id`, `width`, `height`, `internalMimeType`
- [ ] Test `@context` equals `"http://sipi.io/api/file/3/context.json"`
- [ ] Test `id` contains correct base URI with prefix and identifier
- [ ] Test `width` and `height` match known test image dimensions
- [ ] Test `internalMimeType` matches actual file format (e.g., `image/tiff` for .tif)
- [ ] Test `originalMimeType` and `originalFilename` present when SipiEssentials are embedded
- [ ] Test `checksumOriginal` and `checksumDerivative` present when sidecar `.info` file exists. **Prerequisite**: either upload a file first (which creates a sidecar) or add a static `.info` fixture to `test/_test_data/`
- [ ] Test `numpages` present for multi-page images (if applicable)
- [ ] Test with `X-Forwarded-Proto: https` — `id` field uses `https://`
- [ ] Snapshot full knora.json response with insta for a known reference image

#### knora.json for Video

- [ ] Test video response includes: `internalMimeType` (`video/mp4`), `fileSize`, `originalFilename`
- [ ] Test sidecar fields: `duration`, `fps`, `width`, `height` (from `.info` sidecar file)
- [ ] Test missing sidecar: video knora.json still returns basic fields without error (existing test in `server.rs` — verify coverage)

#### knora.json for Other Files

- [ ] Test non-image/non-video file returns: `internalMimeType`, `fileSize`, `originalFilename`

#### knora.json Error Cases

- [ ] Test nonexistent file returns appropriate error
- [ ] Test unauthorized access returns 401 (Lua permission check via `check_file_access`)
- [ ] Test CORS headers match info.json behavior (Origin echo vs `*`)

### Phase 11: Lua Scripting Integration Testing

Sipi's behavior in production is fundamentally shaped by Lua scripts. The dsp-api repo (`dsp-api/sipi/scripts/`) provides the authorization layer, file location logic, and request handling that runs on every request. Our current tests use `sipi.fake-knora-test-config.lua` which loads a **fake** init script — this tests IIIF in isolation but completely skips the production authorization flow.

This phase ensures that sipi's Lua integration points work correctly, so that production scripts can rely on them.

**Key scripts in production** (from `dsp-api/sipi/scripts/`):
- `sipi.init.lua` — `pre_flight()` hook: calls dsp-api for IIIF permission (0=deny, 1=restricted, >=2=full); `file_pre_flight()` for non-IIIF resources
- `authentication.lua` — JWT extraction (header/query/cookie) and validation (exp, aud, iss claims)
- `file_specific_folder_util.lua` — hash-based file location (`ab/cd/filename.jp2`)
- `file_info.lua` — media type classification (IMAGE, AUDIO, TEXT, DOCUMENT, ARCHIVE, VIDEO)
- `cache.lua` — cache management routes (GET/DELETE `/cache`)

#### Lua API Contract Tests (C++ unit tests)

Test that sipi's C++ → Lua bindings work correctly for each function category the production scripts depend on. Note: `server.*` functions (JWT, HTTP, MIME, JSON, fs) are registered in `shttps/LuaServer.cpp` (requires a `Connection` context). `SipiImage`, `cache`, and `helper` modules are in `src/SipiLua.cpp`.

**Feasibility note**: Pure C++ unit tests for `server.*` functions need a live `Connection` object, which is hard to mock. **Alternative approach**: write Lua test scripts (like the existing `test_functions.lua`) that exercise `server.*` APIs and return results via HTTP, then assert from Rust e2e tests. Choose the most practical approach during implementation — the goal is coverage, not a specific test layer.

- [ ] Test `server.decode_jwt()`: valid token → decoded table; expired token → nil; bad signature → nil
- [ ] Test `server.generate_jwt()`: round-trip generate → decode
- [ ] Test `server.http()`: HTTP GET to a mock endpoint, verify response table structure (status, body, headers)
- [ ] Test `server.file_mimetype()`: known files return correct MIME types
- [ ] Test `server.file_mimeconsistency()`: matching extension/mime → true; mismatch → false
- [ ] Test `server.table_to_json()` / `server.json_to_table()`: round-trip fidelity
- [ ] Test `server.fs.*` functions (all from `shttps/LuaServer.cpp`): exists, ftype, is_readable, is_writeable, is_executable, modtime, readdir, getcwd, chdir, copyFile, moveFile, unlink, mkdir, rmdir
- [ ] Test `cache.*` functions: size, nfiles, filelist, delete, purge (requires cache to be populated first)
- [ ] Test `SipiImage` Lua object: new → dims → crop → scale → rotate → watermark → write (verify each step via metadata)
- [ ] Test `helper.filename_hash()`: known inputs produce expected hash paths

#### pre_flight Hook Integration Tests (Rust e2e)

Test sipi's behavior under different `pre_flight()` return values. This requires a test Lua script that returns configurable permission levels.

**Server lifecycle note**: These tests need a sipi instance running with `sipi.permission-test-config.lua`, not the default fake-knora config. Create a separate test file (e.g., `tests/lua_integration.rs`) with its own `OnceLock<SipiServer>` that starts sipi with the permission config on different ports. The `SipiServer::start()` API already supports custom configs and allocates ports dynamically.

- [ ] Create `sipi.permission-test-config.lua` — init script where `pre_flight()` returns different permission codes based on the requested identifier (using actual Lua return format):
  - `permit-full.jp2` → `return 'allow', filepath`
  - `permit-restricted.jp2` → `return { type='restrict', size='!128,128' }, filepath` (restricted view with size limit)
  - `permit-watermark.jp2` → `return { type='restrict', watermark=watermark_path }, filepath` (restricted view with watermark)
  - `permit-deny.jp2` → `return 'deny'` (no access)
  - `permit-not-found.jp2` → `return 'allow', '/nonexistent/path.jp2'` (points to nonexistent file)
- [ ] Test full access: request `permit-full.jp2` → 200, full-size image
- [ ] Test restricted size: request `permit-restricted.jp2` → 200, image dimensions ≤ 128x128
- [ ] Test restricted watermark: request `permit-watermark.jp2` → 200, image has watermark applied (content-length differs from full access)
- [ ] Test denied: request `permit-deny.jp2` → 401
- [ ] Test file not found via Lua: request `permit-not-found.jp2` → 404

#### Lua Route Registration Tests (Rust e2e)

Test that custom Lua routes work (these are used for `/cache`, upload endpoints, etc.).

- [ ] Test GET on a custom Lua route returns expected response (already partially covered by `sqlite_api` and `lua_test_functions` tests — verify and consolidate)
- [ ] Test POST on a custom Lua route with body
- [ ] Test cache management route: GET `/cache` returns JSON array (requires cache to be populated). **Note**: The fake test config doesn't register a `/cache` route — either add it to `sipi.permission-test-config.lua` or to the existing fake config.

#### Documentation

- [ ] Extend `docs/src/lua/index.md` (or create `docs/src/lua/integration.md`) — explain the Lua scripting architecture:
  - How `pre_flight()` and `file_pre_flight()` hooks work
  - How permission codes map to sipi behavior (deny, restricted view, full access)
  - How to write a custom init script
  - Reference to the Lua API docs (`docs/src/lua/index.md`)
  - How production scripts in dsp-api interact with sipi
- [ ] Document knora.json endpoint in `docs/src/guide/knora-json.md` (create under `guide/` which has existing reference content):
  - URL pattern: `/{prefix}/{identifier}/knora.json`
  - Response format for images, video, and other files
  - Sidecar `.info` file format
  - CORS behavior
  - How it differs from IIIF `info.json`
- [ ] Document info.json behavior in the IIIF compliance page (from Phase 13) and add to `docs/mkdocs.yml` `nav:`:
  - Content negotiation (Accept header → Content-Type)
  - Link headers
  - All fields and their sources

### Phase 12: Image Metadata Preservation Testing

Sipi's `SipiImage` holds much more than pixels. For long-term preservation (RDU prevalidation, Archive Ingest gating), we must verify that **all** metadata survives read→process→write cycles. Currently, `operator==` only compares dimensions, bps, photometric, and pixel data — it ignores every metadata layer.

**The problem:** An image can pass `operator==` while having lost its EXIF GPS coordinates, ICC color profile, XMP rights metadata, IPTC keywords, or SipiEssentials provenance data. For an archival system, this is unacceptable.

#### C++ Unit Tests: Full SipiImage Metadata Snapshots (ApprovalTests)

Add a serialization method to `SipiImage` that produces a deterministic text representation of **all** non-pixel properties, suitable for snapshot comparison.

- [ ] Add `SipiImage::metadata_snapshot()` method (or extend `operator<<`) that serializes a deterministic text representation. **Determinism note**: Sort all key-value pairs alphabetically. For EXIF/XMP fields that contain timestamps or tool versions, either (a) exclude them from the snapshot, or (b) use ApprovalTests scrubbers to normalize them. ICC profiles should be compared by type/colorspace/description, not raw binary. Serializes:
  - Dimensions: `nx`, `ny`, `nc`, `bps`
  - Photometric interpretation and orientation
  - ExtraSamples meanings (associated/unassociated alpha)
  - ICC profile: type (sRGB, AdobeRGB, etc.), color space, description, profile size
  - EXIF: all key-value pairs via `exifData` iteration (camera make/model, GPS, orientation, dates, etc.)
  - XMP: serialized XMP packet (or key fields: dc:creator, dc:rights, dc:description, etc.)
  - IPTC: keywords, caption, copyright, byline
  - SipiEssentials: original filename, mimetype, hash type, checksum, ICC usage flag
- [ ] Create ApprovalTests snapshots for reference images:
  - `lena512.jp2` → read → snapshot all metadata
  - `img_exif_gps.jpg` → read → snapshot (has GPS EXIF data)
  - `image_orientation.jpg` → read → snapshot (has orientation EXIF)
  - `gray_with_icc.jp2` → read → snapshot (has embedded ICC profile)
  - `png_16bit.png` → read → snapshot (has ICC profile + alpha channel)
- [ ] Test metadata preservation through format conversion round-trips:
  - TIFF → JP2 → TIFF: compare metadata snapshots (should be identical)
  - JPEG → JP2 → TIFF: compare metadata snapshots (EXIF, ICC should survive)
  - PNG → JP2 → PNG: compare metadata snapshots (ICC, alpha semantics should survive)
  - TIFF → JPEG: compare metadata snapshots (lossy — document what is expected to change)
- [ ] Test metadata preservation through image processing operations:
  - Region crop: metadata should survive (pixels change, metadata stays)
  - Resize: metadata should survive
  - Rotation: orientation tag should update, other metadata stays
  - Quality change (color→gray): photometric changes, ICC profile may change, EXIF/XMP/IPTC stay
- [ ] Test `readOriginal()` path specifically (used by Archive Ingest):
  - Read with `shttps::HashType::sha256` — verify SipiEssentials gets populated with correct checksum
  - Write to JP2 — verify SipiEssentials is embedded in output
  - Re-read the JP2 — verify SipiEssentials round-trips correctly

#### Rust E2E Tests: Metadata Over HTTP (insta snapshots)

For the IIIF server role, verify that metadata is accessible and preserved in served images.

- [ ] Add `kamadak-exif` (or similar) crate to `test/e2e-rust/Cargo.toml` for EXIF extraction from HTTP responses
- [ ] Snapshot-test EXIF metadata in served TIFF images: download `/{id}/full/max/0/default.tif`, extract EXIF, snapshot with insta
- [ ] Snapshot-test that EXIF GPS survives IIIF serving: request `img_exif_gps` image via IIIF, verify GPS coordinates in response
- [ ] Snapshot-test info.json with insta: full JSON response as golden snapshot. **Relationship to Phase 2**: Phase 2's field-by-field assertions remain as they give targeted error messages. The insta snapshot here is complementary — it catches unexpected changes in fields Phase 2 doesn't explicitly check (e.g., field ordering, extra fields). Both coexist.
- [ ] Test ICC profile preservation: request image as TIFF, verify ICC profile is present in response (Content-Type + binary inspection or exiftool-equivalent)

#### What This Catches

| Scenario | Currently Tested | After Phase 12 |
|----------|:---:|:---:|
| Pixel accuracy through conversion | Yes (operator==) | Yes |
| Dimensions/bps/photometric preserved | Yes (operator==) | Yes |
| ICC profile preserved through conversion | **No** | Yes |
| EXIF preserved through conversion | **No** | Yes |
| XMP rights metadata preserved | **No** | Yes |
| IPTC keywords preserved | **No** | Yes |
| SipiEssentials round-trip (origname, checksum) | **No** | Yes |
| Orientation tag updated after rotation | **No** | Yes |
| Alpha channel semantics preserved | **No** | Yes |
| Metadata visible over HTTP (IIIF serving) | **No** | Yes |

### Phase 13: Cleanup and Documentation

- [ ] Remove Hurl tests now covered by Rust e2e: `iiif_image_delivery.hurl`, `info_json.hurl`, `invalid_iiif_urls.hurl`, `deny_unauthorized.hurl`, `not_found.hurl`, `head_request.hurl`
- [ ] Keep Hurl tests without Rust equivalents: `file_access.hurl`, `video_knora_json.hurl`, `missing_sidecar.hurl`, `sqlite_api.hurl`, `lua_endpoints.hurl`
- [ ] **Do NOT modify or remove the Python e2e tests** (`test/e2e/`). They are the golden reference. The Rust e2e tests run alongside them. Removal is planned only after the Rust suite has proven reliable across several production releases.
- [ ] Create `docs/src/iiif-compliance.md` — IIIF Image API 3.0 compliance matrix tracking every feature's status. Structure:
  - **Sipi's role in the IIIF ecosystem** section explaining:
    - Sipi is an **IIIF Image API server** and a **CLI tool for the long-term preservation chain**. As a server, it serves images over HTTP with IIIF parameters. As a CLI tool, it is used by RDU (Research Data Unit) to prevalidate images for archival readiness, and as a gatekeeper in the Archive Ingest process to validate every image entering the repository. Both roles require correctness — the server must serve compliant IIIF responses, and the CLI must preserve all image metadata (EXIF, ICC, XMP, IPTC) through processing.
    - Sipi implements **one** of the IIIF specifications: the **Image API 3.0**. The other IIIF specs serve different roles in the ecosystem:
      - **Presentation API 3.0** — describes *how* to present objects (manifests, canvases, sequences). This is a viewer/client concern (e.g., Universal Viewer, Mirador), not an image server's job.
      - **Authorization Flow API 2.0** — browser-based auth workflows. Excluded: the spec has broken edge cases, and sipi handles access control via its Lua scripting layer and the upstream application (dsp-api).
      - **Change Discovery API 1.0** — harvesting/synchronizing IIIF resources across institutions. A repository-level concern, not image serving.
      - **Content Search API 2.0** — searching within annotations (OCR, transcriptions). Not image serving.
      - **Content State API 1.0** — compact deep-links into viewer state. Purely client-side.
      - **Extensions** (navPlace, Text Granularity, Georeference) — all operate at the Presentation/annotation layer, not the image layer.
    - The optional `partOf` property in `info.json` (linking to a Presentation manifest) is not emitted by sipi. If needed, it could be added via Lua scripting.
  - Header: spec version, sipi version, date last updated, compliance level claimed
  - Table per category (Identifier, Region, Size, Rotation, Quality, Format, HTTP) with columns:
    - Feature name (from spec)
    - Level (0/1/2/optional)
    - Status: **Supported**, **Partial**, **Not Supported**, **Not Applicable**
    - Test: link to the Rust e2e test function (e.g., `iiif_compliance::region_square`)
    - Notes: implementation details, known limitations, TODO for fixes
  - Section for `extraFeatures` claimed in info.json — each with same status/test/notes columns
  - Section for explicitly excluded specs (Auth API — with rationale)
  - This page is the single source of truth for "what does sipi support?" and must be updated whenever a compliance fix lands
- [ ] Add `iiif-compliance.md` to `docs/mkdocs.yml` `nav:` section (docs use mkdocs, not mdBook)
- [ ] Create `docs/src/development/e2e-testing.md` — architecture guide for the Rust e2e test suite. Structure:
  - **Purpose**: why Rust e2e exists alongside the Python golden reference suite
  - **Technology choices**: Rust `#[test]` + `reqwest` (blocking) + `serde_json` + `insta` for snapshots; why not async, why not nextest
  - **Server lifecycle**: `SipiServer` struct, `OnceLock` shared instance, stdout ready signal with TCP fallback, `Drop`-based SIGTERM cleanup
  - **Port allocation**: `AtomicU16` counter, why tests can't run Rust e2e and Hurl simultaneously
  - **Client helpers**: `client()` (follows redirects, reused via `OnceLock`) and `client_no_redirect()` (for testing 303s)
  - **Test organization**: `iiif_compliance.rs` (IIIF spec features), `server.rs` (non-IIIF endpoints), `smoke.rs` (basic health), `upload.rs`, `range_requests.rs`
  - **Adding a new test**: step-by-step (which file, how to get server/client, assertion patterns, when to use `insta` snapshots vs manual assertions)
  - **Relationship to other test layers**: unit tests (C++/GoogleTest), approval tests (ApprovalTests.cpp), Hurl (kept for non-IIIF endpoints), Python e2e (golden reference, do not modify)
  - **Running tests**: `make nix-test-e2e` / `make zig-test-e2e` / `cargo test` directly, `SIPI_BIN` env var override
- [ ] Add `e2e-testing.md` to `docs/mkdocs.yml` `nav:` under Development section
- [ ] Update `docs/src/development/developing.md` with new IIIF compliance test section
- [ ] Update CLAUDE.md testing section if needed

## Acceptance Criteria

- [ ] All 42 tests from the official IIIF validator (https://image-validator.iiif.io/list_tests) have a corresponding Rust e2e test (Auth-related tests excluded)
- [ ] Tests that expose sipi implementation gaps are present but marked `#[ignore]` with a TODO — each ignored test is a trackable compliance fix for follow-up
- [ ] All 5 HTTP feature categories are tested (CORS, redirect, JSON-LD, canonical link, profile link)
- [ ] info.json is validated field-by-field against known-good expected output
- [ ] Error handling tests cover invalid region, size, rotation, quality, format, and identifier
- [ ] No test overlap between `server.rs` and `iiif_compliance.rs`
- [ ] `insta` used for info.json snapshot tests, `client()` reuses connection pool via `OnceLock`, `client_no_redirect()` helper added
- [ ] knora.json endpoint tested for all file types (image, video, other) with full field validation and insta snapshots
- [ ] Lua integration tested: `pre_flight()` permission flow (deny, restricted, full), JWT decode, `server.http()`, `SipiImage` Lua bindings
- [ ] knora.json and info.json endpoints documented; Lua scripting architecture documented
- [ ] Non-ignored tests pass in CI (`make nix-test-e2e`)
- [ ] Test count: ~60-70 Rust e2e tests in `iiif_compliance.rs` (up from current 12), ~100+ total across all test files (up from current 45)
- [ ] Metadata preservation tested via ApprovalTests snapshots for all reference images (EXIF, ICC, XMP, IPTC, SipiEssentials)
- [ ] Format conversion round-trips verified to preserve metadata (not just pixels)
- [ ] `readOriginal()` → write → re-read path tested for SipiEssentials integrity (Archive Ingest path)
- [ ] Each `#[ignore]`d test has a TODO comment documenting what sipi needs to fix for compliance
- [ ] GIF/PDF/WebP format tests document sipi's intentional non-support (not Level 2 required)

## Dependencies

- Existing Rust e2e harness from first plan (`test/e2e-rust/`)
- Test images in `test/_test_data/images/` (already available: lena512.jp2, lena512.tif, etc.)
- `reqwest` blocking client (already in Cargo.toml) — two `OnceLock<Client>` instances: one default (follows redirects) and one with `redirect(Policy::none())`
- `serde_json` for info.json parsing (already in Cargo.toml)
- `kamadak-exif` crate (or similar) for Phase 12 EXIF extraction from HTTP responses (new dependency)
- Watermark image file for Phase 11 `pre_flight` permission tests (must exist in test fixtures or be created during setup)
- ApprovalTests.cpp (already configured in `test/CMakeLists.txt`) for Phase 12 metadata snapshots

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Some IIIF features claimed but not implemented (known: `profileLinkHeader`) | Tests fail | Mark `#[ignore]` with TODO. Each ignored test becomes a trackable compliance fix task. |
| Pixel-level validation (color accuracy) is hard without image decoding | ~9 validator tests can't be fully replicated | Status+headers only for now. Tests affected: `id_squares`, `region_pixels`, `rot_full_basic`, `rot_region_basic`, `rot_full_non90`, `rot_region_non90`, `rot_mirror_180`, `size_region`, `size_up`. Future: add `image` crate. |
| `reqwest` follows redirects by default | `base_uri_redirect` test silently passes | `client_no_redirect()` helper with `Policy::none()` — specified in Phase 1. |
| GIF/PDF/WebP formats parsed but not served | 3 format tests return errors not 200 | Empirically verify actual status code before writing assertions. URL parser regex blocks these before format handler. Treat as negative tests. File as sipi feature gaps. |
| Authorization tests depend on correct Lua init script | `deny_unauthorized_image` test fails if init script doesn't handle deny path | Verify `sipi.fake-knora-test-config.lua` loads init script with deny path handling. Phase 11 adds `sipi.permission-test-config.lua` for full permission testing. |
| No test fixture for escaped identifiers | `id_escaped` test can't run | Create symlink in Phase 1: `test#image.jp2` → `lena512.jp2` |

## Success Metrics

- **IIIF feature coverage**: 100% of Level 2 required features tested (up from ~40%)
- **Extra feature coverage**: 100% of 17 claimed extraFeatures tested (up from ~35%)
- **HTTP feature coverage**: 5/5 categories tested (up from 1/5)
- **Error handling coverage**: 6+ error scenarios per parameter type
- **Test consolidation**: `server.rs` IIIF tests merged into `iiif_compliance.rs`, no duplication
- **External dependency reduction**: `iiif-validate.py` no longer needed for compliance verification
- **Metadata preservation coverage**: all 5 metadata layers (EXIF, ICC, XMP, IPTC, SipiEssentials) snapshot-tested through conversion round-trips
- **Archive Ingest path tested**: `readOriginal()` with SHA-256 checksum → write JP2 → re-read verified
