# Testing Strategy

<!-- last_reviewed: 2026-03-11 -->

This document defines the authoritative testing strategy for sipi. It maps the IIIF Image API 3.0 specification, sipi's extensions (Lua scripting, cache, CLI, knora integration), and Rust migration readiness onto a concrete testing pyramid. Use this document to determine **where** a new test should live, **what** layer it belongs to, and **how** to assess coverage.

## Sipi Feature Inventory

Sipi is a multithreaded, high-performance, IIIF-compatible media server written in C++23. The following is an exhaustive inventory of every feature area. Each feature needs test coverage — the [coverage matrices](#iiif-image-api-30-coverage-matrix) later in this document track the current state.

### IIIF Image API 3.0

Sipi implements the full [IIIF Image API 3.0](https://iiif.io/api/image/3.0/) at Level 2 compliance:

| Feature Area | Parameters | Source |
|---|---|---|
| **Region** (Section 4.1) | `full`, `square`, `x,y,w,h`, `pct:x,y,w,h` | `src/iiifparser/SipiRegion.cpp` |
| **Size** (Section 4.2) | `max`, `w,`, `,h`, `w,h`, `!w,h`, `pct:n`, `^` upscale variants | `src/iiifparser/SipiSize.cpp` |
| **Rotation** (Section 4.3) | Arbitrary angles (float), `!` mirror prefix | `src/iiifparser/SipiRotation.cpp` |
| **Quality** (Section 4.4) | `default`, `color`, `gray`, `bitonal` | `src/iiifparser/SipiQualityFormat.cpp` |
| **Format** (Section 4.5) | `jpg`, `png`, `tif`, `jp2`, `webp` | `src/iiifparser/SipiQualityFormat.cpp` |
| **Identifiers** (Section 3) | URL-encoded, `%2F` slash, prefix-based resolution | `src/iiifparser/SipiIdentifier.cpp` |
| **Info.json** (Section 5) | Full response: `@context`, `id`, `type`, `protocol`, `profile`, `width`, `height`, `sizes`, `tiles`, `extraFormats`, `extraFeatures`, `preferredFormats` | `src/SipiHttpServer.cpp` |
| **Content negotiation** | `Accept: application/ld+json` → JSON-LD with `@context`; default → `application/json` | `src/SipiHttpServer.cpp` |
| **HTTP behavior** (Section 7) | Base URI redirect, HEAD, CORS, Link headers, canonical URI, 400/401/403/404/500/501 errors | `src/SipiHttpServer.cpp` |
| **IIIF Extension: `red:n`** | Reduce factor for JP2 (faster subsampling on read) — sipi-specific, not in IIIF spec | `src/iiifparser/SipiSize.cpp` |

!!! note "IIIF Auth exclusion"
    Sipi does **not** implement the IIIF Authentication API. Access control is handled via custom Lua preflight scripts (`pre_flight`, `file_pre_flight`) that return allow/deny/restrict permissions. This is intentional — the Knora/DSP integration requires custom auth flows (cookie-based sessions) that don't map to the IIIF Auth spec.

### Image Format Support

**Input/Output Formats:**

| Format | Handler | Read | Write | Notes |
|---|---|---|---|---|
| TIFF | `SipiIOTiff` | Yes | Yes | Multi-page, tiled, pyramid; LZW/ZIP/CCITT compression; 1/8/16-bit; RGB/YCbCr/CMYK |
| JPEG | `SipiIOJpeg` | Yes | Yes | 8-bit, progressive and baseline; configurable quality |
| PNG | `SipiIOPng` | Yes | Yes | Palette/gray/RGB/RGBA; 1/2/4/8/16-bit |
| JPEG2000 | `SipiIOJ2k` | Yes | Yes | Via Kakadu (commercial license); reduce factor; quality layers; progression orders |
| WebP | `SipiIOWebp` | Yes | Yes | Via libwebp |

**Metadata Systems:**

| System | Library | Read | Write | Source |
|---|---|---|---|---|
| EXIF | Exiv2 | Yes | Yes | `src/metadata/SipiExif.cpp` |
| XMP | Exiv2 | Yes | Yes | `src/metadata/SipiXmp.cpp` |
| IPTC | Exiv2 | Yes | Yes | `src/metadata/SipiIptc.cpp` |
| ICC Profiles | littleCMS2 | Yes | Yes | `src/metadata/SipiIcc.cpp` |
| SipiEssentials | Custom | Yes | Yes | `src/metadata/SipiEssentials.cpp` |

**Predefined ICC Profiles:** sRGB, AdobeRGB, GRAY_D50, LUM_D65, CMYK_standard, LAB, ROMM_GRAY.

**SipiEssentials** is a custom metadata packet embedded in image headers. It stores: original filename, MIME type, pixel data checksum (MD5/SHA1/SHA256/SHA384/SHA512), and a backup of the ICC profile. This survives format conversions and enables provenance tracking.

**Color Space Support:** RGB, Grayscale, Bitonal, YCbCr, CMYK (with conversion to sRGB), CIELab (with conversion). 8 TIFF orientations handled; `topleft()` normalization for tiling compatibility. 16-bit big-endian internal representation with automatic 8-bit conversion where needed.

### Image Processing Pipeline

The IIIF processing pipeline applies transformations in spec-mandated order:

1. **Region** — crop the source image
2. **Size** — scale to requested dimensions
3. **Rotation** — rotate and/or mirror
4. **Quality** — color space conversion (color, gray, bitonal)
5. **Format** — encode to output format

Each step allocates an intermediate buffer. Peak memory is ~2x image size per transform step.

**Watermarking** is applied as an additional step when the preflight script returns a `restrict` permission with a watermark path. Watermark files must be single-channel 8-bit gray TIFF (SAMPLESPERPIXEL=1, BITSPERSAMPLE=8, PHOTOMETRIC=MINISBLACK).

### HTTP Server (`shttps/`)

The `shttps` library is a custom lightweight HTTP server:

| Feature | Details | Source |
|---|---|---|
| SSL/TLS | Configurable port, certificate, and key paths | `shttps/Server.h` |
| Threading | Configurable thread pool (`nthreads`, default 8) | `shttps/Server.h` |
| Keep-alive | Configurable timeout (seconds) | `shttps/Connection.h` |
| Chunked transfer | `Transfer-Encoding: chunked` support | `shttps/Connection.h` |
| Range requests | HTTP 206 Partial Content | Handler-level |
| CORS | Via Lua preflight scripts | `scripts/` |
| Methods | GET, POST, PUT, DELETE | `shttps/Connection.h` |
| Authentication | JWT (HS256), HTTP Basic Auth, cookie support | `shttps/jwt.h` |
| Max POST size | Configurable (`max_post_size`, default 300M) | `shttps/Connection.cpp` |
| Multipart upload | Form-data file upload with metadata | `shttps/Connection.h` |

### Caching System

File-based LRU cache with dual-limit eviction (`SipiCache.h`, `src/SipiCache.cpp`):

| Feature | Details |
|---|---|
| **Eviction policy** | LRU by access time; evicts down to 80% low-water mark |
| **Size limit** | `cache_size`: `'-1'`=unlimited, `'0'`=disabled, or `'200M'`, `'1G'` |
| **File count limit** | `cache_nfiles`: 0=no limit |
| **Crash recovery** | Serialized index on disk; rebuild from directory scan if index missing |
| **Concurrent access** | Mutex-protected; `blocked_files` map prevents reads during writes |
| **Canonical key** | Full IIIF URL (with watermark flag) as cache key |
| **Metrics** | hits, misses, evictions, skips, size, file count (Prometheus) |
| **API endpoints** | `GET /api/cache` (list files), `DELETE /api/cache` (purge/delete specific) |
| **Cache metadata** | Image dimensions, tile info, pyramid levels, MIME type, checksum per entry |

### Lua Scripting System

Per-request isolated Lua 5.3.5 interpreter with full server access:

**SipiImage Lua Class:**

| Method | Description |
|---|---|
| `SipiImage.new(filepath)` | Load image from file |
| `SipiImage.new(upload_index)` | Load from uploaded file |
| `SipiImage.new(filepath, {region=, size=, reduce=, ...})` | Load with IIIF options |
| `img:dims()` | Get dimensions (nx, ny, orientation) |
| `img:exif(tag)` | Read EXIF data |
| `img:crop(region)` | Crop to IIIF region |
| `img:scale(size)` | Resize to IIIF size |
| `img:rotate(rotation)` | Rotate/mirror |
| `img:topleft()` | Normalize orientation |
| `img:watermark(path)` | Apply watermark |
| `img:write(target)` | Write to file or `'HTTP.jpg'` for HTTP response |

**Server Object:**

| Method/Property | Description |
|---|---|
| `server.method`, `.path`, `.get`, `.post`, `.content` | Request data |
| `server.sendStatus(code)` | Set HTTP status |
| `server.sendHeader(name, value)` | Set response header |
| `server.print(...)` | Write response body |
| `server.setBuffer()` | Enable buffered output |
| `server.requireAuth()` | Require HTTP basic auth |
| `server.generate_jwt(table)` | Create HS256 JWT |
| `server.decode_jwt(token)` | Decode/verify JWT |
| `server.http(method, url, headers, timeout)` | Outbound HTTP request |
| `server.json_to_table(json)` / `server.table_to_json(t)` | JSON conversion |
| `server.getMimeType(filename)` | MIME detection |
| `server.log(msg, level)` | Structured logging |
| `server.uuid_to_base62(uuid)` / `server.base62_to_uuid(b62)` | UUID encoding |

**Cache Object:**

| Method | Description |
|---|---|
| `cache.filelist(sort)` | List entries (sort: AT_ASC, AT_DESC, FS_ASC, FS_DESC) |
| `cache.delete(canonical)` | Remove entry by canonical URL |
| `cache.purge()` | Trigger LRU eviction |
| `cache.nfiles()` / `cache.size()` | Current counts |

**SQLite Integration:** `server.db` provides Lua access to SQLite databases for custom data storage.

**Preflight Scripts:**

- `pre_flight(prefix, identifier, cookie)` → returns permission (`allow`, `deny`, `restrict`) + filepath for IIIF requests
- `file_pre_flight(prefix, identifier, cookie)` → same for raw file downloads
- Restriction types: `watermark` (overlay image), `size` (reduce dimensions)

**Custom Routes:** Lua scripts mapped to HTTP method + URL pattern via config `routes` table.

### CLI Mode

Sipi operates in three CLI modes (`src/sipi.cpp`):

**File Conversion:** `sipi infile outfile [options]`

| Option | Description |
|---|---|
| `-F, --format` | Output format (jpg, tif, png, jpx, j2k, webp) |
| `-I, --icc` | ICC profile conversion (sRGB, AdobeRGB, GRAY) |
| `-q, --quality` | JPEG quality (1-100) |
| `-n, --pagenum` | Multi-page file page selection |
| `-r, --region` | Crop region (pixels) |
| `-s, --size` | IIIF-format size |
| `--scale` | Percentage scaling |
| `-R, --reduce` | JP2 reduce factor |
| `-m, --mirror` | Mirror: horizontal, vertical |
| `-o, --rotate` | Rotation angle (0-360) |
| `-k, --skipmeta` | Strip all metadata |
| `-w, --watermark` | Apply watermark (TIFF) |

**JP2-specific options:** `--Sprofile`, `--Clayers`, `--Clevels`, `--Corder`, `--Cprecincts`, `--Cblk`, `--Stiles`, `--Cuse_sop`, `--rates`, `--Ctiff_pyramid`

**Query Mode:** `sipi -x infile` / `sipi --query infile` — print image metadata.

**Compare Mode:** `sipi -C file1 file2` / `sipi --compare file1 file2` — compare two images.

**Server Mode:** `sipi --config config.lua` with CLI overrides for port, hostname, imgroot, cache settings, SSL, JWT, admin credentials, logging, etc.

### Configuration System

Lua-based configuration (`SipiConf.h`, `src/SipiConf.cpp`):

| Category | Keys |
|---|---|
| **Server** | `hostname`, `port`, `ssl_port`, `ssl_certificate`, `ssl_key`, `nthreads`, `keep_alive` |
| **Image repository** | `imgroot`, `prefix_as_path`, `subdir_levels`, `subdir_excludes` |
| **Image processing** | `jpeg_quality`, `scaling_quality.{jpeg,tiff,png,j2k}` (high/medium/low) |
| **Cache** | `cache_dir`, `cache_size`, `cache_nfiles` |
| **Request handling** | `max_post_size`, `tmpdir`, `max_temp_file_age` |
| **Lua** | `initscript`, `scriptdir`, `thumb_size` |
| **Authentication** | `jwt_secret` (42 chars), `admin.user`, `admin.password` |
| **Static files** | `fileserver.docroot`, `fileserver.wwwroute` |
| **Knora/DSP** | `knora_path`, `knora_port` |
| **Logging** | `loglevel`, `logfile` |
| **Routes** | `routes` table: `{method, route, script}` |

**Deprecated keys:** `cachedir` → `cache_dir`, `cachesize` → `cache_size`, `cache_hysteresis` → removed (80% low-water mark is now hardcoded).

### Prometheus Metrics

Metrics endpoint at `GET /metrics` (`SipiMetrics.h`, `src/SipiMetrics.cpp`):

| Metric | Type | Description |
|---|---|---|
| `cache_hits_total` | Counter | Total cache hits |
| `cache_misses_total` | Counter | Total cache misses |
| `cache_evictions_total` | Counter | Total files evicted |
| `cache_skips_total` | Counter | Cache checks skipped (cache disabled) |
| `cache_size_bytes` | Gauge | Current cache size |
| `cache_files` | Gauge | Current cached file count |
| `cache_size_limit_bytes` | Gauge | Configured size limit |
| `cache_files_limit` | Gauge | Configured file count limit |

### Upload & Ingest

- Multipart form-data file upload via POST
- Format conversion on upload (e.g., TIFF → JP2) via Lua routes
- knora.json sidecar generation with checksums, MIME type, original filename, dimensions
- Admin-protected upload route (`/admin/upload` via `admin_upload.lua`)
- SipiEssentials metadata embedded during ingest

### Security Features

| Feature | Details |
|---|---|
| JWT validation | HS256 with configurable secret; decode + verify in Lua |
| HTTP Basic Auth | Via `server.requireAuth()` in Lua routes |
| Preflight access control | `pre_flight` / `file_pre_flight` scripts return allow/deny/restrict |
| Path traversal prevention | URL-decoded identifier validation |
| Max POST size | Configurable (`max_post_size`, default 300M) |
| Admin auth | Separate admin user/password for management endpoints |

### Integration Features

| Feature | Details |
|---|---|
| **Knora/DSP** | Session cookie validation, knora.json sidecar, configurable API path/port |
| **API endpoints** | `/api/cache` (manage cache), `/api/exit` (shutdown), `/metrics` (Prometheus) |
| **File access** | Raw file download via `/prefix/identifier/file` with `file_pre_flight` auth |
| **Sentry** | Error reporting via `SIPI_SENTRY_DSN`, `SIPI_SENTRY_ENVIRONMENT`, `SIPI_SENTRY_RELEASE` |

### Build & Deployment

| Feature | Details |
|---|---|
| **Build systems** | CMake, Nix |
| **CI** | GitHub Actions: unit tests, e2e tests, Hurl tests, fuzz (nightly), Docker builds |
| **Documentation** | MkDocs Material site, LLM-optimized `llms.txt` output |
| **Dependency management** | Vendored archives in `vendor/` with SHA-256 checksums |

---

## Testing Pyramid

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
        │         Unit Tests               │  GoogleTest (C++) where it's the only reach
        │         + Rust unit tests        │  New: Rust #[test] + proptest (preferred)
        └──────────────────────────────────┘
```

**Distribution target (post-Rust-migration steady state):** ~50% unit, ~30% e2e contract, ~15% snapshot/integration, ~5% fuzz. Current distribution (~47% unit, ~52% e2e) is inverted because the C++ codebase lacks Rust unit tests; as migration progresses and Rust `#[test]` modules grow, the ratio will shift toward the target.

## Layer Definitions

### Layer 1: Unit Tests (fastest, most numerous)

**Purpose:** Test individual functions and parsers in isolation.

| Sublayer | Framework | Location | When to use |
|---|---|---|---|
| C++ unit | GoogleTest | `test/unit/` | When the behavior cannot be reached from Rust/e2e/Hurl — internal C++ state, FFI boundaries, RAII / exception paths, concurrency primitives |
| Rust unit (preferred) | `#[test]` + `proptest` | Future `src/` modules | During Rust migration: inline `#[cfg(test)]` modules |
| Rust property-based | `proptest` | Future `src/` modules | Parsers, serializers, roundtrip invariants |

**What belongs here:**

- IIIF URL parsing (region, size, rotation, quality, format)
- Filename hashing
- Configuration parsing
- HTTP header parsing, URL encoding/decoding
- Image metadata extraction
- Any pure function with well-defined inputs/outputs

**C++ unit-test policy:** Default to Rust unit tests, Hurl, or Rust e2e for new coverage — they survive the migration and exercise the contract clients see. Add C++ unit tests where they are the only practical reach, and only there. In practice this means:

- **Internal C++ state** that has no HTTP-observable surface (cache eviction order, memory-budget acquire/release, parser intermediate AST).
- **FFI / C-library boundaries** where the bug class lives in the C++↔C interop (libtiff variadic-arg type widths, kakadu callback lifetimes, lcms2 profile ownership) and Rust e2e would observe only the downstream symptom.
- **Move/copy/RAII semantics** that pure-function tests can pin but server-level tests cannot (assignment-operator buffer lifecycle, exception-safety guarantees, `noexcept` move correctness).
- **Concurrency primitives** with lock-free or atomic semantics (`SipiMemoryBudget`) where deterministic e2e reproduction is impractical.
- **Replacement-target coverage:** components scheduled for Rust replacement (shttps, Lua scripting, cache management) are covered with C++ unit tests that travel with the C++ code and disappear cleanly when the component is replaced. This documents existing behavior for the rewrite team and avoids false failures when the Rust replacement changes internal behavior while preserving the HTTP contract.

Behavior that *is* HTTP-observable belongs in Hurl (status/header contracts) or Rust e2e (multi-step flows, response-body inspection, golden snapshots) — not in C++ unit tests.

### Layer 2: Snapshot / Golden Baseline Tests

**Purpose:** Detect unintended output changes via approved golden baselines.

| Framework | Location | When to use |
|---|---|---|
| `insta` (Rust) | `test/e2e-rust/tests/snapshots/` | info.json structure, HTTP headers, response metadata |
| ApprovalTests (C++) | `test/approval/` | Image-conversion metadata fingerprints; byte-exact encoder output baselines |

**ApprovalTests scope:** New C++ approval suites are admissible only for byte-exact encoder-output gating against dependency migrations (e.g. libtiff, libjpeg, kakadu, lcms2 version bumps) and for metadata fingerprints that have no Rust-side equivalent. Functional behaviour belongs in Layer 3 (Hurl/Rust e2e), not here.

**What belongs here:**

- Full info.json structure (field names, types, values)
- HTTP response header sets (content-type, CORS, Link)
- knora.json response structure
- Image metadata fingerprints (EXIF tags, XMP fields, ICC profile name) — golden baselines prevent silent metadata drift during code changes or format handler updates
- Byte-exact encoder output baselines for dep-migration regression gating (TIFF/JP2/PNG goldens captured under deterministic encoder settings)
- Any complex output where field-by-field assertion is fragile

**Pattern:** Use `insta::assert_json_snapshot!` with redactions for dynamic fields (`id`, timestamps).

**ICC determinism gate.** Approval tests under `test/approval/image_encode_baseline_test.cpp` are byte-stable only when `SOURCE_DATE_EPOCH` is set. lcms2 stamps wall-clock UTC into bytes 24-35 of every freshly-created ICC profile (and several emit paths in SipiImage round-trip through that), so without the env var the seconds field drifts across consecutive runs of the same binary. `SipiIcc::iccBytes()` overwrites those bytes (and zeros the Profile ID at bytes 84-99) when the env var is set; CMake injects `SOURCE_DATE_EPOCH=946684800` (2000-01-01T00:00:00Z UTC) into the `sipi.approvaltests` invocation via `set_tests_properties(... ENVIRONMENT ...)`. Production never sets the var and retains lcms2's wall-clock behaviour. See [`docs/adr/0002-icc-profile-determinism-test-only.md`](../../adr/0002-icc-profile-determinism-test-only.md) for the architectural rationale and [`test/approval/CHANGELOG.approval.md`](../../../test/approval/CHANGELOG.approval.md) for the maintainer's how-to.

### Layer 3: E2E Contract Tests (HTTP-level)

**Purpose:** Test sipi's HTTP API contract — the behavior visible to clients. These tests survive the Rust migration because they test the contract, not the implementation.

| Sublayer | Framework | Location | When to use |
|---|---|---|---|
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
|---|---|
| Need to inspect response body (JSON, image bytes) | Only checking status code + headers |
| Multi-step flow (upload then fetch) | Single request/response |
| Need golden baseline (insta snapshot) | Simple assertion (status, header value) |
| Need to compute something (checksum, dimension) | Declarative assertion suffices |
| Need test setup/teardown (create files, etc.) | No setup needed |

### Layer 4: Fuzz Testing (continuous, nightly)

**Purpose:** Find crashes, memory safety issues, and edge cases in parsers and input handlers.

| Framework | Location | When to use |
|---|---|---|
| libFuzzer (C++) | `fuzz/` | IIIF URI parser, HTTP request parser |
| `cargo-fuzz` / `proptest` (Rust, future) | Future crate `fuzz/` | After Rust migration of parsers |

**What belongs here:**

- IIIF URI parser (`parse_iiif_uri`)
- HTTP request parsing
- Image format header parsing
- Any function that processes untrusted input

**Corpus management:** CI uploads corpus artifacts; `just fuzz-corpus-update` merges CI corpus into seed corpus. See [Fuzz Testing](fuzzing.md) for full details.

## Test Decision Tree

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
- Tests needing cache verification require a cache-enabled server config (use `sipi.cache-test-config.lua` which has cache configured)
- Python e2e tests have been retired and replaced by Rust e2e tests. See [Python Test Deprecation](#python-test-deprecation--parity-checklist) for the parity checklist
- Flaky tests (e.g., races against file flush) should use the `retry_flaky()` helper — see [Flaky Test Handling](#flaky-test-handling)

## IIIF Image API 3.0 Coverage Matrix

The following matrix maps every testable IIIF spec requirement to its test status. This is the authoritative coverage reference.

### Info.json (Section 5)

| Requirement | Status | Test | Notes |
|---|---|---|---|
| `@context` field present and correct | :white_check_mark: | `info_json_context` | |
| `id` matches base URI | :white_check_mark: | `info_json_id_contains_base_uri` | |
| `type` = `ImageService3` | :white_check_mark: | `info_json_type_imageservice3` | |
| `protocol` = `http://iiif.io/api/image` | :white_check_mark: | `info_json_protocol` | |
| `profile` = `level2` | :white_check_mark: | `info_json_profile_level2` | |
| `width` and `height` integers | :white_check_mark: | `info_json_dimensions_match_lena512` | |
| `sizes` array with valid dimensions | :white_check_mark: | `info_json_sizes_have_valid_dimensions` | |
| `tiles` with scaleFactors | :white_check_mark: | `info_json_tiles_have_scale_factors` | |
| `extraFormats` | :white_check_mark: | `info_json_extra_formats` | |
| `preferredFormats` | :white_check_mark: | `info_json_preferred_formats` | |
| `extraFeatures` (17 features) | :white_check_mark: | `info_json_all_17_extra_features` | |
| Golden baseline snapshot | :white_check_mark: | `info_json_golden_snapshot` | insta |
| Header snapshot (CT, CORS, Link) | :white_check_mark: | `info_json_headers_snapshot` | insta |
| Content-Type without Accept | :white_check_mark: | `info_json_content_type_default` | `application/json` |
| Content-Type with Accept: ld+json | :white_check_mark: | `jsonld_media_type_with_accept` | |
| Link header on default request | :white_check_mark: | `jsonld_default_has_link_header` | |
| Canonical Link header | :white_check_mark: | `canonical_link_header` | |
| Profile Link header | :x: IGNORED | `profile_link_header` | DEV-6003: sipi bug |
| X-Forwarded-Proto HTTPS rewrite | :white_check_mark: | `info_json_x_forwarded_proto_https` | |
| Required fields structural check | :white_check_mark: | `info_json_has_required_fields` | |
| Structural: sizes array exists | :white_check_mark: | `info_json_has_sizes_array` | |
| Structural: tiles array exists | :white_check_mark: | `info_json_has_tiles_array` | |
| Structural: extraFeatures exists | :white_check_mark: | `info_json_has_extra_features` | |

### Region (Section 4.1)

| Requirement | Status | Test | Notes |
|---|---|---|---|
| `full` | :white_check_mark: | `full_iiif_url_returns_image` | |
| `square` | :white_check_mark: | `region_square` | |
| `pct:x,y,w,h` | :white_check_mark: | `region_percent` | |
| `x,y,w,h` (pixel) | :white_check_mark: | `region_pixel`, `region_pixel_offset` | |
| Overflow → crop at edge | :white_check_mark: | `region_beyond_bounds_is_cropped` | |
| Start beyond image → error | :white_check_mark: | `region_start_beyond_image` | |
| Zero width → 400 | :white_check_mark: | `region_zero_width` | |
| Invalid syntax → error | :white_check_mark: | `region_invalid_syntax` | |
| Region + size combination | :white_check_mark: | `size_after_region` | |
| Region + rotation combination | :white_check_mark: | `rotation_after_region` | |
| Region crop (specific) | :white_check_mark: | `iiif_region_crop` | |
| Region dimension verification | :x: GAP | — | Need to verify output dimensions match requested region |

### Size (Section 4.2)

| Requirement | Status | Test | Notes |
|---|---|---|---|
| `max` | :white_check_mark: | `full_iiif_url_returns_image` | implicit |
| `w,` (width) | :white_check_mark: | `size_by_width` | |
| `,h` (height) | :white_check_mark: | `size_by_height` | |
| `w,h` (exact) | :white_check_mark: | `size_exact` | |
| `!w,h` (best fit) | :white_check_mark: | `size_best_fit` | |
| `pct:n` | :white_check_mark: | `size_percent` | |
| `^` upscaling | :white_check_mark: | `size_upscaling` | |
| No upscale beyond original | :white_check_mark: | `size_no_upscale_beyond_original` | |
| Invalid syntax → error | :white_check_mark: | `size_invalid_syntax` | |
| Output dimension verification | :x: GAP | — | Need to decode image and verify actual pixel dimensions |
| `^max` upscale to limits | :x: GAP | — | Not tested |
| `^,h` (height-only upscale) | :x: GAP | — | Not tested |
| `^w,h` (exact with upscale) | :x: GAP | — | Not tested |
| `^!w,h` (confined upscale) | :x: GAP | — | Not tested |
| `^pct:n` (upscale percent) | :x: GAP | — | Not tested |

### Rotation (Section 4.3)

| Requirement | Status | Test | Notes |
|---|---|---|---|
| `0` (no rotation) | :white_check_mark: | `full_iiif_url_returns_image` | implicit |
| `90` | :white_check_mark: | `iiif_rotation_90` | |
| `180` | :white_check_mark: | `rotation_180` | |
| `270` | :white_check_mark: | `rotation_270` | |
| Arbitrary (e.g. 22.5) | :white_check_mark: | `rotation_arbitrary` | |
| `!0` (mirror only) | :white_check_mark: | `mirror_rotation` | |
| `!180` (mirror + rotate) | :white_check_mark: | `mirror_plus_180` | |
| Invalid → error | :white_check_mark: | `rotation_invalid` | |
| Rotation output verification | :x: GAP | — | Need to verify actual rotation applied (image dimensions swap for 90/270) |

### Quality (Section 4.4)

| Requirement | Status | Test | Notes |
|---|---|---|---|
| `default` | :white_check_mark: | `full_iiif_url_returns_image` | implicit |
| `color` | :white_check_mark: | `quality_color` | |
| `gray` | :white_check_mark: | `quality_gray` | |
| `bitonal` | :white_check_mark: | `quality_bitonal` | |
| Invalid → error | :white_check_mark: | `quality_invalid` | |
| `extraQualities` in info.json | :x: GAP | — | Sipi supports color/gray/bitonal but may not emit extraQualities |

### Format (Section 4.5)

| Requirement | Status | Test | Notes |
|---|---|---|---|
| `jpg` + Content-Type | :white_check_mark: | `format_jpg_content_type` | |
| `png` + Content-Type | :white_check_mark: | `format_png_content_type` | |
| `tif` + Content-Type | :white_check_mark: | `format_tiff_content_type` | |
| `jp2` + Content-Type | :white_check_mark: | `format_jp2_content_type` | |
| Unsupported (gif, pdf, webp, bmp) | :white_check_mark: | `unsupported_formats_rejected` | |

### CORS (Section 7.1)

| Requirement | Status | Test | Notes |
|---|---|---|---|
| Info.json ACAO without Origin | :white_check_mark: | `cors_info_json_without_origin` | |
| Info.json ACAO with Origin | :white_check_mark: | `cors_info_json_with_origin` | |
| Image ACAO with Origin | :white_check_mark: | `cors_image_with_origin` | |
| Image ACAO without Origin | :white_check_mark: | `cors_image_without_origin` | |
| OPTIONS preflight | :white_check_mark: | `cors_preflight` | |

### HTTP Behavior (Section 7)

| Requirement | Status | Test | Notes |
|---|---|---|---|
| Base URI → redirect to info.json | :white_check_mark: | `base_uri_redirect` | |
| HEAD request | :white_check_mark: | `head_iiif_image_empty_body` | |
| 401 unauthorized | :white_check_mark: | `deny_unauthorized_image` | |
| 404 not found | :white_check_mark: | `id_random_gives_404` | |
| Path traversal rejected | :white_check_mark: | `path_traversal_rejected` | |
| Incomplete URL → error | :white_check_mark: | `id_incomplete_iiif_url` | |
| Malformed URL → error | :white_check_mark: | `id_malformed_iiif_url` | |
| Empty identifier → error | :white_check_mark: | `invalid_iiif_url_empty_identifier` | |
| HEAD returns headers | :white_check_mark: | `head_request_returns_headers` | |
| Missing file → 404 | :white_check_mark: | `returns_404_for_missing_file` | |
| HTTP 304 conditional requests | :x: GAP | — | Sipi sends Last-Modified/Cache-Control but If-Modified-Since not tested |
| Operation ordering (Region→Size→Rot→Qual→Fmt) | :x: GAP | — | No test verifies transformation order is correct |
| Fractional percent regions (e.g. pct:0.5,...) | :x: GAP | — | Only integer percent tested |

### Identifier (Section 3)

| Requirement | Status | Test | Notes |
|---|---|---|---|
| Encoded slash `%2F` | :white_check_mark: | `id_escaped_slash_decoded` | |
| Encoded `#` (`%23`) | :x: IGNORED | `id_escaped` | DEV-6004: sipi bug |
| Subdirectory identifier | :white_check_mark: | via server tests | |
| Non-ASCII identifiers | :x: GAP | — | Not tested |
| ARK/URN identifiers | :x: GAP | — | Not tested (may not apply) |

## Sipi Extension Coverage Matrix

| Feature | Status | Test Location | Notes |
|---|---|---|---|
| File upload (TIFF→JP2) | :white_check_mark: | `upload.rs` | |
| Upload knora.json | :white_check_mark: | `upload.rs` | |
| Upload JPEG with comment block | :white_check_mark: | `upload.rs` | |
| Video knora.json metadata | :white_check_mark: | `server.rs` | |
| Lua test_functions endpoint | :white_check_mark: | `server.rs` | |
| Lua mediatype endpoint | :white_check_mark: | `server.rs` | |
| Lua mimetype_func endpoint | :white_check_mark: | `server.rs` | |
| Lua knora_session_cookie | :white_check_mark: | `server.rs` | |
| Lua orientation endpoint | :white_check_mark: | `server.rs` | |
| Lua exif_gps endpoint | :white_check_mark: | `server.rs` | |
| Lua read_write endpoint | :white_check_mark: | `server.rs` | |
| SQLite API | :white_check_mark: | `server.rs` + Hurl | |
| Missing sidecar handling | :white_check_mark: | `server.rs` + Hurl | |
| Concurrent request handling | :white_check_mark: | `server.rs` | |
| File access allowed/denied | :white_check_mark: | `server.rs` | |
| Knora.json validation | :white_check_mark: | `server.rs` | |
| Upload edge cases | :white_check_mark: | `upload.rs` | |
| Video metadata extensions | :white_check_mark: | `server.rs` | |
| Small-file range requests | :white_check_mark: | `range_requests.rs` | 7 tests |
| Cache hit/miss verification | :x: GAP | — | No tests verify cache metrics or behavior |
| CLI mode (file conversion) | :x: GAP | — | No tests for `sipi --file` mode |
| Prometheus metrics endpoint | :x: GAP | — | No tests for `/metrics` |
| SSL/TLS endpoints | :x: GAP | — | No Rust tests for HTTPS |
| Large-file range requests (10MB+) | :x: GAP | — | Python-only |
| Image dimension verification | :x: GAP | — | Tests check status codes but not actual output dimensions |
| EXIF preservation through IIIF pipeline | :x: GAP | — | No test verifies EXIF survives transforms |
| XMP preservation through IIIF pipeline | :x: GAP | — | No test verifies XMP survives transforms |
| ICC profile preservation/conversion | :x: GAP | — | C++ unit tests exist but no HTTP-level test |
| IPTC metadata preservation | :x: GAP | — | No e2e test |
| SipiEssentials round-trip | :x: GAP | — | Custom metadata not tested via HTTP |
| CLI conversion metadata fidelity | :x: GAP | — | Untested |
| MIME consistency check (`/api/mimetest`) | :x: GAP | — | Python-only |
| Thumbnail generation (`/make_thumbnail`) | :x: GAP | — | Python-only |
| Convert from binaries (`/convert_from_binaries`) | :x: GAP | — | Python-only |
| Temp directory cleanup | :x: GAP | — | Python-only |
| Restricted image size reduction | :x: GAP | — | Python tests only |
| 4-bit palette PNG upload | :x: GAP | — | Python-only |
| Cache API routes (`/api/cache`) | :x: GAP | — | No tests |
| Favicon endpoint | :x: GAP | — | Handler exists, no tests |
| Memory safety (ASan/LSan) | :x: GAP | — | Only fuzz harness uses sanitizers |
| Thread safety (TSan) | :x: GAP | — | Untested for data races |
| Performance regression detection | :x: GAP | — | No latency thresholds or load testing |
| Corrupt/truncated image handling | :x: GAP | — | Should return 500, not crash |
| Lua route handler errors | :x: GAP | — | Should return 500 gracefully |
| Zero-byte / empty file upload | :x: GAP | — | Should fail gracefully |
| Invalid server config startup | :x: GAP | — | No test for invalid config |
| Double-encoded URL handling | :x: GAP | — | `%252F` behavior untested |
| Extremely long URL / header | :x: GAP | — | Partially covered by fuzz |
| JWT validation edge cases | :x: GAP | — | Expired, `alg:none`, tampered tokens |
| Image decompression bomb | :x: GAP | — | No pixel limit on decode |
| Upload size enforcement | :x: GAP | — | `max_post_size` enforced but untested |
| CRLF header injection | :x: GAP | — | No sanitization test |
| Cache key collision | :x: GAP | — | No isolation test |
| Error message information disclosure | :x: GAP | — | May leak filesystem paths |
| Slowloris / connection exhaustion | :x: GAP | — | No resilience test |
| `parseSizeString` edge cases | :x: GAP | — | Zero tests for this function |
| Deprecated config key migration | :x: GAP | — | Migration logic untested |
| CLI argument overrides | :x: GAP | — | Never tested |
| Empty jwt_secret behavior | :x: GAP | — | May silently disable auth |
| Invalid Lua config syntax | :x: GAP | — | Should fail cleanly |
| Config with nonexistent paths | :x: GAP | — | Untested startup behavior |
| SImage Lua API coverage | :x: GAP | — | 12 methods tested only via black-box HTTP |
| Lua JWT round-trip | :x: GAP | — | Generate + decode correctness |
| Lua UUID round-trip | :x: GAP | — | base62 conversion correctness |
| Lua `server.http` outbound | :x: GAP | — | Error handling for unreachable hosts |
| Lua error propagation to HTTP | :x: GAP | — | C++ exception → 500 propagation |
| HTTP keep-alive | :x: GAP | — | No multi-request connection test |
| Chunked transfer encoding | :x: GAP | — | `ChunkReader` never tested |
| Connection: close header | :x: GAP | — | Server behavior untested |
| Thread pool exhaustion | :x: GAP | — | Queuing/rejection behavior unknown |
| Graceful shutdown | :x: GAP | — | SIGTERM handler untested |
| Multi-page TIFF `@page` e2e | :x: GAP | — | Parser works, e2e untested |
| CMYK→sRGB through IIIF pipeline | :x: GAP | — | Unit-tested, not HTTP-level |
| CIELab through IIIF pipeline | :x: GAP | — | Unit-tested, not HTTP-level |
| 16-bit depth through IIIF pipeline | :x: GAP | — | Unit-tested, not HTTP-level |
| Progressive JPEG handling | :x: GAP | — | Common in web content, untested |
| TIFF with JPEG compression | :x: GAP | — | Known bug (YCrCb autoconvert) |
| 1-bit TIFF (bi-level) | :white_check_mark: | unit + rust-e2e | MINISWHITE and MINISBLACK, LZW and uncompressed (DEV-6249) |
| JPEG YCCK colorspace | :white_check_mark: | unit | Was throw; now decoded via CMYK path (DEV-6250) |
| JPEG CMYK with APP14 (Photoshop) — inverted before ICC | :white_check_mark: | unit | DEV-6257 |
| JPEG CMYK without APP14 (raw) — not inverted | :white_check_mark: | unit | DEV-6257 negative case |
| JPEG with APP13 before APP1 + non-ASCII IPTC | :white_check_mark: | unit | Heritage collection regression (DEV-6250) |
| CLI `--json` output contract | :white_check_mark: | unit + rust-e2e | success + error payloads, single-document stdout |
| Watermark application via HTTP | :x: GAP | — | Unit-tested, not e2e |
| Restrict + watermark combined | :x: GAP | — | Untested combination |
| Watermark cache key separation | :x: GAP | — | Separate entries untested |
| CLI watermark mode | :x: GAP | — | Untested |
| Concurrent cache writes (same key) | :x: GAP | — | `blocked_files` mutex untested |
| Cache eviction during active reads | :x: GAP | — | Potential read error |
| Concurrent file uploads | :x: GAP | — | Potential race conditions |
| Lua state thread isolation | :x: GAP | — | Shared global table untested |
| Cache disabled mode (`cache_size=0`) | :x: GAP | — | Untested |
| Cache LRU purge under size limit | :x: GAP | — | Completely untested |
| Cache nfiles limit enforcement | :x: GAP | — | Count-based eviction untested |
| Keep-alive timeout enforcement | :x: GAP | — | Idle connection termination untested |
| Sustained load memory growth | :x: GAP | — | **Production issue:** no pixel limit |
| Concurrent large image decode memory | :x: GAP | — | Peak RSS untested |
| Image decode memory accounting | :x: GAP | — | No aggregate limit |
| Intermediate buffer accumulation | :x: GAP | — | ~2x per transform step |
| Cache as memory pressure relief | :x: GAP | — | Hit path avoids decode — untested |

## Gap Summary

| Category | Covered | Gaps | Coverage |
|---|---|---|---|
| Info.json fields | 22 | 1 (profile Link — sipi bug) | 96% |
| Region parameters | 11 | 1 (dimension verify) | 92% |
| Size parameters | 9 | 6 (dimension verify, ^max, ^,h, ^w,h, ^!w,h, ^pct) | 60% |
| Rotation parameters | 8 | 1 (dimension verify) | 89% |
| Quality parameters | 5 | 1 (extraQualities field) | 83% |
| Format parameters | 5 | 0 | 100% |
| CORS | 5 | 0 | 100% |
| HTTP behavior | 10 | 3 (304, operation order, fractional pct) | 77% |
| Identifiers | 2 | 3 (non-ASCII, ARK/URN, bug) | 40% |
| Sipi extensions | 19 | 76 | 20% |
| **Total** | **96** | **92** | **51%** |

**Key gap categories:**

- **Metadata** (6 gaps): EXIF, XMP, ICC, IPTC, SipiEssentials, CLI metadata — silent drift risk
- **Error handling** (6 gaps): corrupt images, Lua errors, empty uploads, config, double-encoding, long URLs — crash/hang risk
- **Security** (7 gaps): JWT, decompression bombs, upload limits, CRLF injection, cache poisoning, info disclosure, slowloris
- **Configuration** (6 gaps): parseSizeString, deprecated keys, CLI overrides, jwt_secret, invalid Lua, nonexistent paths
- **Lua API** (5 gaps): SImage methods, JWT round-trip, UUID round-trip, HTTP client, error propagation
- **Connection handling** (5 gaps): keep-alive, chunked, Connection: close, thread pool, graceful shutdown
- **Format edge cases** (5 gaps): CMYK/CIELab/16-bit through IIIF, progressive JPEG, TIFF-JPEG. *Previously listed 1-bit TIFF and YCCK-JPEG; both resolved by DEV-6249 / DEV-6250 / DEV-6257.*
- **Concurrency** (4 gaps): cache writes, eviction during read, parallel uploads, Lua state isolation
- **Resource limits** (4 gaps): cache disabled, LRU purge, nfiles limit, keep-alive timeout
- **Memory/OOM** (5 gaps): sustained load, concurrent decode, accounting, buffers, cache relief — **active production issue**
- **Watermark** (4 gaps): HTTP-level, restrict+watermark, cache separation, CLI

## Cross-Cutting: Memory Safety (Sanitizer Builds)

Memory leaks and undefined behavior are not a separate pyramid layer but a **build variant** that runs existing tests with compiler instrumentation. This is critical for sipi as a long-running C++ server where leaks accumulate.

**Current state:** ASan+UBSan infrastructure is in place — `ENABLE_SANITIZERS` CMake option, `just nix-build-sanitized` (builds `.#sanitized` with tests running in the Nix sandbox), and nightly `sanitizer.yml` CI workflow. Known findings to triage on first run:

- **DEV-6002: `SipiFilenameHash::operator=` memory leak** — `operator=` allocates `new vector<char>` without deleting the old `hash` pointer. Confirmed by code inspection. Fix: add `delete hash;` before the new allocation, or switch to `std::unique_ptr`.
- **Potential: `SipiFilenameHash` copy constructor** — also `new`s without freeing, but only leaks if the destination object was previously constructed with a different hash (doesn't happen via typical usage).
- **Expected: false positives from external libraries** — exiv2, lcms2, and other vendored libraries may trigger ASan warnings that aren't sipi bugs. These should be suppressed via an ASan suppression file if needed.

**Sanitizer stack:**

| Sanitizer | Catches | Flag | Overhead |
|---|---|---|---|
| ASan (AddressSanitizer) | Buffer overflow, use-after-free, double-free, leaks | `-fsanitize=address` | ~2x |
| UBSan (UndefinedBehaviorSanitizer) | Integer overflow, null deref, misaligned access | `-fsanitize=undefined` | ~1.2x |
| TSan (ThreadSanitizer) | Data races, deadlocks | `-fsanitize=thread` | ~5-15x |

**Infrastructure:**

| Component | Status | Details |
|-----------|--------|---------|
| `ENABLE_SANITIZERS` CMake option | Done | `-fsanitize=address,undefined` on all targets |
| `just nix-build-sanitized` | Done | Builds `.#sanitized` derivation (Debug + ASan + UBSan); unit tests run inside the Nix sandbox and fail the build on any finding |
| Nightly CI (`sanitizer.yml`) | Done | PR + scheduled; unit tests in-sandbox, e2e out-of-sandbox with ASan log capture |
| TSan variant | Future | Optional nightly, separate from ASan (can't combine) |

**Strategy:** Nightly CI job runs unit tests + e2e suite with ASan+UBSan. TSan as optional nightly variant. Not in PR CI (too slow).

## Cross-Cutting: Performance Regression Detection

**Current state:** Prometheus metrics include cache counters/gauges and `sipi_request_duration_seconds` histogram (5ms–10s buckets). CI infrastructure includes smoke latency assertions in PR CI. Throughput-style load testing is intentionally **not** run in CI — synthetic `wrk` against three IIIF endpoints on a shared GitHub runner does not mirror dasch-prod-01's actual workload (cold-cache reads of medium-sized JP2s under spiky concurrency) and the prior nightly `loadtest.yml` artifacts were not consulted. Meaningful load testing belongs in a staging environment with realistic fixtures and traffic shape.

**Strategy (three tiers):**

| Tier | What | Tool | When |
|---|---|---|---|
| Smoke latency | Assert response time < threshold in e2e tests | Rust `Instant::now()` | PR CI |
| Load baseline | Throughput against realistic workload | `wrk` / `k6` | **Future** — staging environment, not CI |
| Component benchmarks | Micro-benchmarks for parsers, decode | `criterion` (Rust, future) | Post-migration |

**Smoke latency thresholds (proposed):**

- Info.json request: < 50ms
- 512x512 JPEG delivery (cache miss): < 500ms
- 512x512 JPEG delivery (cache hit): < 100ms
- These catch gross regressions (10x slowdown), not subtle changes

## Snapshot Review Workflow

Sipi uses [insta](https://insta.rs/) for golden baseline snapshots. When a snapshot changes:

1. Run tests: `cargo test` (in `test/e2e-rust/`)
2. Review pending snapshots: `cargo insta review`
3. Accept intentional changes, reject regressions
4. Commit updated `.snap` files

**When to use insta:**

- info.json response structure
- HTTP response header sets
- knora.json response structure
- Image metadata fingerprints

**Pattern:** Use `insta::assert_json_snapshot!` with `redact` for dynamic values:

```rust
insta::assert_json_snapshot!(info_json, {
    ".id" => "[base_uri]",
});
```

## CI Integration

| Target | Just Recipe | When | Notes |
|---|---|---|---|
| C++ unit tests (CI) | `just nix-build` (`.#dev` checkPhase) | PR CI | GoogleTest via ctest, runs in the Nix sandbox |
| C++ unit tests (Bazel inner-loop) | `just bazel-test-unit` | local | `bazel test //test/unit/...`; 11 of 12 components (sipiimage tagged manual, [DEV-6354](https://linear.app/dasch/issue/DEV-6354)). Local-only until [DEV-6348](https://linear.app/dasch/issue/DEV-6348) (Y+6) cuts CI over. |
| C++ approval tests (CI) | `just nix-build` (`.#dev` checkPhase) | PR CI | ApprovalTests via ctest; `SOURCE_DATE_EPOCH=946684800` injected by CMake |
| C++ approval tests (Bazel inner-loop) | `just bazel-test-approval` | local | `bazel test //test/approval:approvaltests`; same env injection via `BUILD.bazel`. Local-only until [DEV-6348](https://linear.app/dasch/issue/DEV-6348) (Y+6). |
| Rust e2e tests (CI) | `just nix-test-e2e` | PR CI | Pre-built binaries from `.#e2e-tests` (crane); reads `$SIPI_BIN` |
| Rust e2e tests (inner-loop) | `just rust-test-e2e` | local | cargo-driven; reads `$SIPI_BIN`; same test code as `nix-test-e2e` |
| Docker smoke (CI) | `just nix-test-smoke` | PR + tag CI | Pre-built binary from `.#smoke-test`; runs against loaded image |
| Hurl contract tests | `just hurl-test` | PR CI | Declarative HTTP tests |
| Python e2e tests | *(retired)* | — | Replaced by Rust e2e tests |
| Fuzz testing | `.github/workflows/fuzz.yml` | Nightly | libFuzzer corpus growth |
| Sanitizer builds | `just nix-build-sanitized` (`.#sanitized`) | PR + Nightly | ASan+UBSan; unit tests in-sandbox, e2e out-of-sandbox |

## Python Test Deprecation — Parity Checklist

Python e2e tests (`test/e2e/`) have been retired. The following per-function parity checklist confirmed Rust coverage before removal.

### test_01_conversions.py (2 tests) — RETIRED

| Python Test | Rust Equivalent | Notes |
|---|---|---|
| `test_iso_15444_4_decode_jp2` | `cli_file_conversion` in `cli.rs` | JP2→TIFF decode with PAE comparison |
| `test_iso_15444_4_round_trip` | `cli_file_conversion` in `cli.rs` | TIFF→JP2→TIFF round-trip |

### test_02_server.py (32 tests) — RETIRED

| Python Test | Rust Equivalent | Notes |
|---|---|---|
| `test_sipi_starts` | `server_starts_and_responds` in `smoke.rs` | |
| `test_sipi_log_output` | — | Dropped: infrastructure check ("Added route" in stdout), not behavioral |
| `test_lua_functions` | `lua_test_functions` in `server.rs` | |
| `test_clean_temp_dir` | `temp_directory_cleanup` in `server.rs` | |
| `test_lua_scripts` | `lua_mediatype` in `server.rs` | |
| `test_lua_mimetype` | `lua_mimetype_func` in `server.rs` | |
| `test_knora_session_parsing` | `lua_knora_session_cookie` in `server.rs` | |
| `test_file_bytes` | `full_iiif_url_returns_image` in `iiif_compliance.rs` | Status+content-type check; byte-exact comparison covered by dimension verification and snapshot tests |
| `test_restrict` | `restricted_image_reduction` in `server.rs` | Verifies 128x128 via image decode |
| `test_deny` | `deny_unauthorized_image` in `iiif_compliance.rs` | |
| `test_not_found` | `returns_404_for_missing_file` in `smoke.rs` | |
| `test_iiif_url_parsing` | `invalid_iiif_url_empty_identifier`, `id_incomplete_iiif_url`, `id_malformed_iiif_url` in `iiif_compliance.rs` | Multiple tests cover all 5 invalid URL patterns |
| `test_read_write` | `lua_read_write` in `server.rs` | |
| `test_jpg_with_comment` | `upload_jpeg_with_comment_block` in `upload.rs` | |
| `test_odd_file` | `upload_odd_file` in `upload.rs` | |
| `test_head_response_should_be_empty` | `head_iiif_image_empty_body` in `iiif_compliance.rs` + `head_request_returns_headers` in `smoke.rs` | |
| `test_mimeconsistency` | `mime_consistency` in `server.rs` | All 6 test cases ported |
| `test_thumbnail` | `thumbnail_generation` + `thumbnail_convert_from_file` in `server.rs` | |
| `test_image_conversion` | `image_conversion_from_binaries` in `server.rs` | `/convert_from_binaries` endpoint |
| `test_knora_info_validation` | `knora_json_image_required_fields` + `upload_tiff_knora_json` in `upload.rs` | Image and CSV sidecar flows |
| `test_json_info_validation` | Info.json tests in `iiif_compliance.rs` + `info_json_x_forwarded_proto_https` | Full structure + X-Forwarded-Proto |
| `test_knora_json_for_video` | `video_knora_json` in `server.rs` | |
| `test_handling_of_missing_sidecar_file_for_video` | `missing_sidecar_handled_gracefully` in `server.rs` | |
| `test_sqlite_api` | `sqlite_api` in `server.rs` | |
| `test_iiif_auth_api` | `iiif_auth_api` in `server.rs` | 401 + IIIF Auth service block in info.json |
| `test_orientation_topleft` | `lua_orientation` in `server.rs` | Lua endpoint tests same code path |
| `test_4bit_palette_png` | `upload_4bit_palette_png` in `upload.rs` | |
| `test_upscaling_server` | `size_upscaling` + `size_upscale_*` in `iiif_compliance.rs` | Status + dimension verification |
| `test_file_access` | `file_access_allowed` + `file_access_denied` in `server.rs` | |
| `test_concurrency` | `concurrent_requests` in `server.rs` | |
| `test_orientation` | `lua_orientation` in `server.rs` | |
| `test_exif_gps` | `lua_exif_gps` in `server.rs` | |

### test_03_iiif.py (1 test) — RETIRED

| Python Test | Rust Equivalent | Notes |
|---|---|---|
| `test_iiif_validation` | — | Explicitly excluded: calls external `iiif-validate.py` binary, effectively a no-op when validator unavailable. IIIF compliance covered by 80+ tests in `iiif_compliance.rs` |

### test_04_range_requests.py (12 active tests) — RETIRED

| Python Test | Rust Equivalent | Notes |
|---|---|---|
| `test_small_file_no_range` | `small_file_no_range` in `range_requests.rs` | |
| `test_small_file_range_first_100_bytes` | `small_file_range_first_100_bytes` in `range_requests.rs` | |
| `test_small_file_range_middle_bytes` | `small_file_range_middle_bytes` in `range_requests.rs` | |
| `test_small_file_range_last_byte` | `small_file_range_last_byte` in `range_requests.rs` | |
| `test_small_file_open_ended_from_start` | `small_file_open_ended_from_start` in `range_requests.rs` | |
| `test_small_file_open_ended_from_middle` | `small_file_open_ended_from_middle` in `range_requests.rs` | |
| `test_large_file_no_range` | `large_file_full_download` in `range_requests.rs` | |
| `test_large_file_range_first_megabyte` | `large_file_range_first_1mb` in `range_requests.rs` | |
| `test_large_file_range_middle_chunk` | `large_file_range_middle_chunk` in `range_requests.rs` | |
| `test_large_file_range_last_chunk` | `large_file_range_last_bytes` in `range_requests.rs` | Last 1000 bytes |
| `test_large_file_range_single_last_byte` | `large_file_range_single_last_byte` in `range_requests.rs` | |
| `test_large_file_open_ended_from_start` | `large_file_open_ended_from_start` in `range_requests.rs` | |
| `test_large_file_open_ended_from_middle` | `large_file_open_ended_from_middle` in `range_requests.rs` | |
| `test_large_file_multiple_ranges_simulation` | `large_file_sequential_range_reassembly` in `range_requests.rs` | |

### Infrastructure files — RETIRED

| File | Notes |
|---|---|
| `conftest.py` | Test manager, fixtures, nginx control — replaced by `test/e2e-rust/tests/common/` |
| `config.ini` | Python test config — replaced by Rust test harness |
| `nginx/` | Nginx reverse proxy for SSL testing — Rust tests use direct HTTPS |
| `requirements.txt` | Not present (deps managed by Nix/pip) |

## Rust Migration Testing Path

When a C++ component is migrated to Rust:

1. **Before migration:** Ensure e2e contract tests cover the component's behavior
2. **During migration:** Write Rust unit tests (`#[test]`, `proptest`) for the new implementation
3. **After migration:** Existing e2e tests validate the Rust implementation matches C++ behavior
4. **Cleanup:** Remove corresponding C++ unit tests (they tested the old implementation)

The `insta` golden baselines are critical — they capture exact C++ server behavior and detect any Rust implementation drift.

## Flaky Test Handling

Some e2e tests are inherently racy — for example, a test that uploads a file and immediately GETs the converted result may fail if the server hasn't flushed to disk yet. Rather than retrying at the CI job level, handle flakiness at the **test level** using the `retry_flaky()` helper from the test harness (`test/e2e-rust/src/lib.rs`):

```rust
use sipi_e2e::retry_flaky;

#[test]
fn my_flaky_test() {
    let srv = server();
    // ... setup ...

    retry_flaky(3, || {
        match client().get(&url).send() {
            Ok(resp) if resp.status().as_u16() == 200 => Ok(()),
            Ok(resp) => Err(format!("HTTP {}", resp.status())),
            Err(e) => Err(format!("{}", e)),
        }
    });
}
```

**Guidelines:**

- `retry_flaky(max_attempts, closure)` retries the closure up to `max_attempts` times with a 2-second sleep between attempts
- The closure returns `Ok(())` on success or `Err(message)` on failure
- Failed attempts emit `[retry_flaky]` log lines for CI visibility
- Only use for tests with a known race condition — do not mask real bugs with retries
- If a test needs more than 3 retries, the underlying issue should be fixed instead

## Future Additions

- **Doc tests:** Once sipi has Rust library code (post-migration), `///` example doc tests become valuable
- **`criterion` benchmarks:** Fine-grained micro-benchmarks for parsers, image decode, ICC conversion — after Rust migration
- **`sipi_request_duration_seconds`:** Prometheus histogram for production latency monitoring
