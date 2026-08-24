---
title: "SIPI Production Hardening"
type: feat
date: 2026-03-14
author: "Ivan Subotic"
status: reviewed
repositories:
  - name: sipi
  - name: ops-deploy
linear:
  - DEV-5913
  - DEV-6023
  - DEV-5916
  - DEV-5915
  - DEV-5914
  - DEV-5917
  - DEV-5918
  - DEV-6002
  - DEV-5919
  - DEV-5927
  - DEV-6033
  - DEV-6031
---

# SIPI Production Hardening — Implementation Plan

## Overview

Implement the SIPI production hardening initiative across three phases: input validation (security), memory safety, and operational infrastructure. All work builds on PR #523 (branch `claude/fix-sipi-oom-crash-ZPTzX`), which already implements per-request pixel limits (R20-R22) and includes a rate limiter design proposal.

The PRD defines 42 requirements (R1-R42) across 12 Linear issues. This plan covers all requirements except R34-R38 (non-root Docker), which are deferred to a separate cross-repo initiative (see section 3.4). Each in-scope requirement is mapped to specific code changes, file locations, and test strategies.

## Problem Statement

SIPI has 11 known vulnerabilities and operational gaps from a February 2026 production audit, plus an OOM incident from a bot systematically requesting full-resolution renders. The issues cluster into:

1. **Input validation** — path traversal, null byte injection, header injection (externally exploitable today)
2. **Memory safety** — leaks and undefined behavior in SipiImage/SipiFilenameHash (production instability over time)
3. **Resource exhaustion** — per-client rate limiting not yet implemented (defense-in-depth)
4. **Operational** — no health endpoint, no graceful shutdown (deployment risk). Non-root Docker (R34-R38) deferred to separate initiative.

## Proposed Solution

Three-phase implementation on PR #523's branch, ordered by risk: security fixes first, memory safety second, operational improvements third. Each phase produces a self-contained commit group that can be verified independently.

## Technical Approach

### Architecture

All changes are within the existing SIPI architecture. No new external dependencies. Key integration points:

- **HTTP parsing layer** (`shttps/`): null byte rejection (R5-R7)
- **IIIF handler** (`src/SipiHttpServer.cpp`): path traversal, header injection, rate limiting
- **Image classes** (`src/SipiImage.cpp`, `src/SipiFilenameHash.cpp`): memory fixes
- **Server infrastructure** (`shttps/Server.cpp`): graceful shutdown
- **Build/deploy** (`Dockerfile`, `ops-deploy/`): Docker HEALTHCHECK, config

### Deployment Context

ops-deploy uses **Docker Compose with Swarm mode** (`roles/dsp-deploy/templates/docker-compose-iiif.yml.j2`). Key deployment mappings:

| Requirement | Docker Compose Setting |
|---|---|
| R42 (graceful shutdown) | `stop_grace_period: 35s` in service definition |
| R31 (health check) | `HEALTHCHECK` in Dockerfile + Traefik label for external monitoring |

**Note:** R38 (`user: "1000:1000"`) has been deferred to a separate initiative — see section 3.4.

### Implementation Phases

#### Phase 1: Input Validation [DEV-5913, DEV-6023, DEV-5916]

**Why first:** Externally exploitable — a malicious actor can trigger these today.

##### 1.1 Path Traversal Prevention (R1-R4)

**Target:** `src/SipiHttpServer.cpp` — `serve_iiif` function (around line 1742 where URI is parsed)

**Implementation:**

Create a validation function in `src/SipiHttpServer.cpp`:

```cpp
// After URL-decoding the IIIF identifier, before any filesystem operation
[[nodiscard]] static std::expected<std::string, std::string>
validate_iiif_identifier(std::string_view identifier, std::string_view imgroot)
{
    // R1: Reject .. components (literal, URL-encoded, double-encoded)
    // Check decoded identifier for ".." path components
    if (contains_traversal(identifier)) {
        return std::unexpected("Invalid identifier");
    }

    // R2: Construct full path, resolve via realpath(), verify prefix
    auto full_path = std::string(imgroot) + "/" + std::string(identifier);
    char resolved[PATH_MAX];
    if (realpath(full_path.c_str(), resolved) == nullptr) {
        return std::unexpected("File not found");
    }
    if (std::string_view(resolved).substr(0, imgroot.size()) != imgroot) {
        return std::unexpected("Invalid identifier");
    }

    return std::string(resolved);
}
```

The `contains_traversal()` helper checks for:
- Literal `..` as a path component (between `/` delimiters or at start/end)
- Already-decoded `%2e%2e` and `%252e%252e` (double-encoded) — check before and after URL-decoding

**R3:** On validation failure, call `send_error(conn_obj, Connection::BAD_REQUEST, "Invalid IIIF identifier")` — no path info leaked. **Note:** The existing error at `SipiHttpServer.cpp:434` leaks the internal filesystem path in its message — this must also be sanitized as part of R3.

**R4:** Before returning the error, log: `log_warn("Path traversal blocked: client=%s identifier=%s", conn_obj.peer_ip().c_str(), raw_identifier.c_str())`

**Critical: Two additional attack surfaces require validation:**

1. **Lua preflight path (line 420):** When a Lua preflight function exists, `infile` comes from `pre_flight_info["infile"]` — set by Lua, not by SIPI. The `realpath()` + prefix check must be applied to the final `infile` *after* Lua returns, regardless of source. Place the `realpath()` validation at line 432 (before `access()` check), which is the common path for both Lua and non-Lua flows.

2. **Prefix parameter (line 423):** When `prefix_as_path` is true, `urldecode(params[iiif_prefix])` is concatenated into the filesystem path. The `contains_traversal()` check must cover the prefix as well. However, since the `realpath()` + prefix check at line 432 validates the *combined* resolved path against `imgroot()`, this catches prefix-based traversal too. Ensure the `realpath()` check is always applied, not just when `contains_traversal()` fires.

**Recommended implementation point:** Create a `validate_resolved_path()` function that takes the final `infile` string and the **resolved** `imgroot`, calls `realpath()`, and verifies the prefix. Call it at line 432 (before `access()`), which is the single chokepoint for all paths — Lua, prefix, and identifier.

**Important:** `imgroot` must be resolved via `realpath()` once at server startup (e.g., in `SipiHttpServer::run()`). If `imgroot` is a symlink (e.g., `/sipi/images` → `/nfs/data/images`), `realpath(infile)` resolves through the symlink but a prefix check against the unresolved `imgroot` would always fail.

**Files:**

| File | Change |
|------|--------|
| `src/SipiHttpServer.cpp` | Add `validate_iiif_identifier()`, `contains_traversal()`, `validate_resolved_path()`. Apply `contains_traversal()` to identifier early; apply `validate_resolved_path()` at line 432 as the definitive guard covering Lua paths, prefix paths, and direct paths |

##### 1.2 Null Byte Injection Prevention (R5-R7)

**Target:** `shttps/Connection.cpp` — HTTP request parsing layer

**Implementation:**

R7 requires the fix in the HTTP parsing layer to protect all routes.

**Key insight:** The raw URI stored in `Connection::_uri` is NOT URL-decoded — `urldecode()` (Connection.cpp:146) is called per-handler, not at the Connection level. So `%00` exists as literal characters `%`, `0`, `0` in `_uri`, not as `\0`. A `find('\0')` check alone misses it.

**Two-layer check in `Connection::process()` (where the raw URI is first parsed):**

```cpp
// R5/R6/R7: Reject null bytes in any form
// Layer 1: Reject percent-encoded null bytes (%00, case-insensitive)
auto lower_uri = to_lower(uri);  // normalize for case-insensitive check
if (lower_uri.find("%00") != std::string::npos) {
    send_error(Connection::BAD_REQUEST, "Invalid request");
    return;
}
// Layer 2: Reject literal null bytes (shouldn't appear in HTTP, but defense-in-depth)
if (uri.find('\0') != std::string::npos) {
    send_error(Connection::BAD_REQUEST, "Invalid request");
    return;
}
```

This catches `%00` (case-insensitive: `%00`, `%00`) in the raw URI and any literal `\0` that somehow enters the URI string. The `%00` check is specific — it won't false-positive on other percent-encoded characters like `%0a` (newline). The check is before any handler dispatch, protecting all routes.

**Files:**

| File | Change |
|------|--------|
| `shttps/Connection.cpp` | Add null byte check on raw URI in request parsing (before URL-decode) |
| `shttps/Connection.h` | No changes expected |

##### 1.3 Header Injection Prevention (R8-R10)

**Target:** `src/SipiHttpServer.cpp:1162` — Content-Disposition header construction

**Implementation:**

Create a sanitization function:

```cpp
[[nodiscard]] static std::string sanitize_header_value(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (unsigned char c : input) {
        // R8: Strip CR, LF, null, and control characters (0x00-0x1F, 0x7F)
        // Preserve non-ASCII (>= 0x80) for RFC 6266 filename* encoding
        if (c == '\r' || c == '\n' || c == '\0' || (c < 0x20) || c == 0x7F) {
            continue;
        }
        result += static_cast<char>(c);
    }
    return result;
}
```

**Note:** Non-ASCII bytes (>= 0x80) are preserved so the subsequent `is_ascii()` check can route to the `filename*=UTF-8''...` branch for proper RFC 6266 encoding. Stripping non-ASCII would make the RFC 6266 branch unreachable and corrupt non-ASCII IIIF identifiers in Content-Disposition.

For the Content-Disposition header (R9), use RFC 6266 `filename*` for non-ASCII:

```cpp
// R9: RFC 6266 compliant filename
auto safe_name = sanitize_header_value(identifier);
if (is_ascii(safe_name)) {
    // Escape '"' and '\' per RFC 2616 quoted-string rules
    auto quoted = escape_quoted_string(safe_name);
    conn_obj.header("Content-Disposition",
        "attachment; filename=\"" + quoted + "\"");
} else {
    conn_obj.header("Content-Disposition",
        "attachment; filename*=UTF-8''" + percent_encode(safe_name));
}
```

R10: Audit all other `conn_obj.header()` calls that interpolate user input and apply the same sanitization.

**Files:**

| File | Change |
|------|--------|
| `src/SipiHttpServer.cpp` | Add `sanitize_header_value()`, `escape_quoted_string()`, `percent_encode()`, update Content-Disposition, audit other headers |

##### Phase 1 Testing

| Test Type | What | Where |
|-----------|------|-------|
| Fuzz corpus | Path traversal variants (`../`, `%2e%2e`, `%252e%252e`, symlinks) | `test/fuzz/corpus/path_traversal/` |
| Fuzz corpus | Null byte in URL (`%00`, `\0`) | `test/fuzz/corpus/null_byte/` |
| Fuzz corpus | Header injection (`\r\n`, non-printable) | `test/fuzz/corpus/header_injection/` |
| E2E (Rust) | Path traversal returns 400, no file content leaked | `test/e2e-rust/tests/input_validation.rs` |
| E2E (Rust) | Null byte returns 400, server stays healthy | `test/e2e-rust/tests/input_validation.rs` |
| E2E (Rust) | CRLF in identifier produces safe Content-Disposition | `test/e2e-rust/tests/input_validation.rs` |
| E2E (Rust) | Update existing `security.rs:crlf_header_injection` test — tighten assertion to expect 400 (currently accepts 200/400/404) | `test/e2e-rust/tests/security.rs` |
| Unit | `contains_traversal()` edge cases: `%2e%2e`, `%252e%252e`, mixed case `%2E%2e`, partial `%2` | `test/unit/input_validation/` |
| Unit | `sanitize_header_value()` edge cases: non-ASCII, control chars, empty input | `test/unit/input_validation/` |
| Unit | `validate_resolved_path()`: symlinks, Lua-returned paths, prefix paths | `test/unit/input_validation/` |
| E2E (Rust) | Null byte (`%00`) returns 400 on non-IIIF routes (`/metrics`, `/health`) | `test/e2e-rust/tests/input_validation.rs` |

**Phase 1 acceptance:** All unit, fuzz, and e2e tests pass. `%00` returns 400 on all routes (not just IIIF).

---

#### Phase 2: Memory Safety [DEV-5915, DEV-5914, DEV-5917, DEV-5918, DEV-6002]

**Why second:** These cause production instability but aren't externally triggerable on demand.

##### 2.1 SipiImage Copy/Assignment (R11-R13)

**Target:** `src/SipiImage.cpp:58-97` (copy constructor) and `src/SipiImage.cpp:154-195` (`operator=`)

**R11 — Copy constructor null checks:**

```cpp
SipiImage::SipiImage(const SipiImage &img_p) {
    // ... dimension copies ...
    if (img_p.exif) exif = std::make_shared<SipiExif>(*img_p.exif);
    if (img_p.iptc) iptc = std::make_shared<SipiIptc>(*img_p.iptc);
    if (img_p.xmp)  xmp  = std::make_shared<SipiXmp>(*img_p.xmp);
    if (img_p.icc)  icc  = std::make_shared<SipiIcc>(*img_p.icc);
    // ... pixel copy ...
}
```

**R12 — Assignment operator leak fix:**

The preferred approach (per C++23 style guide Rule of Zero) is to convert `pixels` from `byte*` to `std::unique_ptr<byte[]>`. However, `pixels` is widely used across the codebase (image I/O handlers, Lua bindings). The minimal fix:

```cpp
SipiImage &SipiImage::operator=(const SipiImage &img_p) {
    if (this != &img_p) {
        // R12: Free old buffer before allocating new
        delete[] pixels;
        pixels = nullptr;
        bufsiz = 0;

        // Dimension and state copies (match copy constructor)
        nx = img_p.nx;
        ny = img_p.ny;
        nc = img_p.nc;
        bps = img_p.bps;
        orientation = img_p.orientation;
        es = img_p.es;
        photo = img_p.photo;      // BUG FIX: missing in current operator=
        emdata = img_p.emdata;    // BUG FIX: missing in current operator=
        skip_metadata = img_p.skip_metadata;
        conobj = img_p.conobj;    // non-owning pointer to Connection, safe to copy

        // R11: Null-check metadata before deep copy
        if (img_p.xmp)  xmp  = std::make_shared<SipiXmp>(*img_p.xmp);   else xmp.reset();
        if (img_p.icc)  icc  = std::make_shared<SipiIcc>(*img_p.icc);   else icc.reset();
        if (img_p.iptc) iptc = std::make_shared<SipiIptc>(*img_p.iptc); else iptc.reset();
        if (img_p.exif) exif = std::make_shared<SipiExif>(*img_p.exif); else exif.reset();

        if (img_p.bufsiz > 0 && img_p.pixels != nullptr) {
            bufsiz = img_p.bufsiz;
            pixels = new byte[bufsiz];
            memcpy(pixels, img_p.pixels, bufsiz);
        }
        // R13: bufsiz == 0 but old pixels was non-null is now handled
        // by the unconditional delete[] above
    }
    return *this;
}
```

**Note:** Full RAII modernization of `pixels` to `std::unique_ptr<byte[]>` is deferred — it touches every format handler. The leak fix is sufficient and safe. The `photo` and `emdata` member copies are additional bugs found in the current `operator=` (the copy constructor copies them correctly at lines 66 and 94).

##### 2.2 SipiImage Arithmetic Operators (R14-R16)

**Target:** `src/SipiImage.cpp` — `operator+`, `operator-`, `operator+=`, `operator-=`

**R14 — Return by value:**

Current code returns `SipiImage&` from `operator+`/`operator-` (referencing heap-allocated object). Change signatures to return `SipiImage` by value. The compiler applies RVO/NRVO.

**Prerequisite:** SipiImage needs a move constructor and move assignment operator for efficient return-by-value. Without these, the compiler falls back to copy, which is expensive for large pixel buffers. Add:

```cpp
SipiImage(SipiImage &&other) noexcept;
SipiImage &operator=(SipiImage &&other) noexcept;
```

Then change the operator signatures:

```cpp
// Before: SipiImage& operator-(const SipiImage &rhs);
// After:
SipiImage operator-(const SipiImage &rhs) const;
SipiImage operator+(const SipiImage &rhs) const;
```

**R14 body — return-by-value eliminates operator-/operator+ leaks:**

After the signature change, `operator-` and `operator+` become trivial and leak-free:

```cpp
SipiImage SipiImage::operator-(const SipiImage &rhs) const {
    SipiImage result(*this);  // stack copy
    result -= rhs;
    return result;  // RVO/NRVO, no heap allocation
}
```

**R15 — RAII temporary buffers in `operator-=` and `operator+=`:**

Both operators allocate two raw resources: `int *diffbuf` and `SipiImage *new_rhs`. All must use RAII:

```cpp
SipiImage &SipiImage::operator-=(const SipiImage &rhs) {
    // ... validation ...
    std::unique_ptr<SipiImage> new_rhs_guard;
    const SipiImage *rhs_ptr = &rhs;
    if ((nx != rhs.nx) || (ny != rhs.ny)) {
        new_rhs_guard = std::make_unique<SipiImage>(rhs);
        new_rhs_guard->scale(nx, ny);
        rhs_ptr = new_rhs_guard.get();
    }

    auto diffbuf = std::make_unique<int[]>(nx * ny * nc);
    // ... compute into diffbuf using rhs_ptr->pixels ...
    // ... normalize diffbuf back into this->pixels ...
    // RAII: diffbuf and new_rhs_guard auto-freed on scope exit or exception
    return *this;
}
```

**Additional bugs in `operator+=`:**
1. **Memory leak:** `new_rhs` (line 1607) is never deleted on the success path (line 1700 deletes `diffbuf` but not `new_rhs`). Apply `std::unique_ptr<SipiImage>` as shown above.
2. **Logic bug (16-bit):** Line 1638 uses subtraction (`ltmp[...] - rtmp[...]`) instead of addition — copy-paste from `operator-=`. Fix while applying RAII.

**Note:** `operator-=` does delete `new_rhs` on success (line 1577) but would leak on exception between allocation and delete. RAII makes both paths safe.

##### 2.3 SipiFilenameHash (R17-R19)

**Target:** `include/SipiFilenameHash.h`, `src/SipiFilenameHash.cpp`

**R17 — Convert pointer to value member:**

```cpp
// Before:
class SipiFilenameHash {
    std::vector<char> *hash;  // manual memory management
    // ...
};

// After (Rule of Zero):
class SipiFilenameHash {
    std::vector<char> hash;   // value member, no manual management
    std::string path;
    std::string name;
    // Copy/assignment/destructor all defaulted (Rule of Zero)
};
```

This eliminates the copy constructor bug (R18 — `path`/`name` now copied automatically) and the assignment operator leak (old `hash` pointer freed automatically).

**R19 — Update tests:**

The tests in `test/unit/filenamehash/` currently document broken behavior. After the fix:
- `filepath()` must return the complete path including filename after copy/assignment
- Update test expectations to verify correct behavior

##### Phase 2 Testing

| Test Type | What | Where |
|-----------|------|-------|
| Unit (ASan) | SipiImage copy constructor with null metadata | `test/unit/sipiimage/` |
| Unit (ASan) | SipiImage assignment — verify no leak (assign, reassign, destroy) | `test/unit/sipiimage/` |
| Unit (ASan) | Arithmetic operators return by value, no leak | `test/unit/sipiimage/` |
| Unit (ASan) | SipiFilenameHash copy/assign — `filepath()` correct | `test/unit/filenamehash/` |
| CI | Zero ASan/UBSan findings across all unit tests | Existing CI workflow |

**Phase 2 acceptance:** Zero ASan findings, all existing tests pass, `filepath()` correct after copy/assign.

---

#### Phase 3: Rate Limiting + Operational [DEV-6033, DEV-5919, DEV-5927, DEV-6031]

**Why third:** Defense-in-depth and operational improvements that build on the fixes above.

##### 3.1 Per-Client Rate Limiter (R23-R30) [DEV-6033]

Implementation follows the detailed proposal at `docs/proposals/pixel-rate-limiter.md`. Key design decisions updated based on deployment reality:

**Operational philosophy:** Ship in **monitor mode** first. Infra observes real traffic patterns via Prometheus, then tunes thresholds, then enables enforcement. The rate limiter must never surprise-block legitimate users on first deploy.

**Production incident validation (2026-03-12):** Bot 216.73.216.108 sent 111 requests over 18 minutes, every one `full/3888,/0/default.jpg` — full-region, width-constrained renders of unique images in project 0812. At ~19.4 MP per request = **2.2 GP total pixels in 18 min** (~1.2 GP per 10 min). Response times degraded from 8s → 10s → 502 (OOM). The rate limiter at 500MP/10min would have blocked this after ~26 images (~5 minutes in).

**DaSCH tile sizes:** dsp-ingest converts uploads to JPEG2000 via SIPI without explicit tile parameters. SIPI's adaptive J2K encoder (`SipiIOJ2k.cpp:796-810`) tiles based on image dimensions: images ≥ 2048px min dimension → **1024×1024 tiles** (1,048,576 pixels/tile). Smaller images get no tiling. Most DaSCH production images are > 2048px → 1024×1024 tiles.

**Traffic pattern context:** All major IIIF viewers (Universal Viewer, Mirador, OpenSeadragon) respect tile sizes declared in `info.json`. With 1024×1024 tiles on a 1920×1080 viewport, ~4-6 tiles are visible at once. A deep zoom session on a single large image: ~60-150 tile requests. Heavy researcher browsing 10 images in 10 minutes: ~600 MP - 1.5 GP in tile pixels. A harvesting bot bypasses tiles entirely and requests `full/` renders.

**Tile-aware rate limiting:** To avoid penalizing legitimate tile browsing, requests below a configurable pixel threshold (`rate_limit_pixel_threshold`, default 2,000,000 = 2MP) do not consume budget. This exempts 1024×1024 tiles (~1MP) while counting full-resolution downloads (~19MP+). The threshold should be set above the tile size but below typical full-image renders.

**R24 — Modes:**

| Mode | Behavior | When to use |
|------|----------|-------------|
| `off` | Rate limiter disabled entirely | Binary default — safe for any deployment |
| `monitor` | Log + increment Prometheus metrics, **never return 429** | Initial production deploy — observe patterns |
| `enforce` | Log + metrics + return HTTP 429 | After infra validates thresholds against real traffic |

**Key files:**

| File | Change |
|------|--------|
| `include/SipiRateLimiter.h` | **New** — `SipiRateLimiter` class with sliding-window algorithm |
| `src/SipiRateLimiter.cpp` | **New** — `check_and_record()`, cleanup logic, mode-aware enforcement |
| `src/SipiHttpServer.cpp` | Add `resolve_client_id()` helper, rate limit check after pixel limit check (`TOO_MANY_REQUESTS` already handled in `send_error` at Connection.cpp:1050) |
| `src/SipiHttpServer.hpp` | Add `_rate_limiter` (`std::unique_ptr<SipiRateLimiter>`) + accessors |
| `include/SipiConf.h` | Add `rate_limit_window`, `rate_limit_max_pixels`, `rate_limit_mode`, `rate_limit_pixel_threshold` fields + getters/setters |
| `src/SipiConf.cpp` | Parse 4 fields from Lua config |
| `src/sipi.cpp` | 4 CLI options (`--rate-limit-max-pixels`, `--rate-limit-window`, `--rate-limit-mode`, `--rate-limit-pixel-threshold`) + wiring |
| `config/sipi.config.lua` | Document 4 new config entries |
| `include/SipiMetrics.h` | Add rate limiter metrics (see below) |
| `src/SipiMetrics.cpp` | Initialize rate limiter metrics |
| `CMakeLists.txt` | Add `SipiRateLimiter.cpp` to sources |

**R25 — Client identity resolution:**

No custom Traefik headers — use what's available:

```cpp
static std::string resolve_client_id(Connection &conn_obj) {
    // 1. Rightmost X-Forwarded-For (set by Traefik)
    auto xff = conn_obj.header("X-Forwarded-For");
    if (!xff.empty()) {
        auto pos = xff.rfind(',');
        auto ip = (pos != std::string::npos) ? xff.substr(pos + 1) : xff;
        return trim(ip);
    }
    // 2. Peer IP (direct connection or no proxy)
    return conn_obj.peer_ip();
}
```

**R27 — Optimistic deduction:** Budget recorded at check time (before processing), not after. Slight over-penalization of failed requests is acceptable.

**R28 — Thread safety:** Single `std::mutex` with short critical section. Acceptable for 8 workers at typical request rates.

**R29 — Memory hygiene:** Lazy per-client cleanup on every check + full map sweep every 1000 checks.

**R30 — Observability (Prometheus metrics + structured logs):**

**Metrics** (no per-IP labels — per-IP cardinality is a Prometheus anti-pattern at ~3-4 KB per time series):

| Metric | Type | Labels | Purpose |
|--------|------|--------|---------|
| `sipi_rate_limit_decisions_total` | Counter | `action` (`allowed` / `rejected` / `shadow_rejected`) | Core metric — rejection ratio is the single most important signal. `shadow_rejected` = would-be-blocked in monitor mode |
| `sipi_rate_limit_near_limit_total` | Counter | — | Clients at >80% of budget (early warning, Envoy pattern) |
| `sipi_rate_limit_check_duration_seconds` | Histogram | — | Rate limiter is in the hot path — must not add latency. Alert on p99 > 5ms |
| `sipi_request_pixels` | Histogram | `type` (`tile` / `large`) | Distribution of pixels per request — infra sees where traffic clusters to tune thresholds. Buckets: 0.5M, 1M, 2M, 5M, 10M, 25M, 50M, 100M |
| `sipi_rate_limit_clients_tracked` | Gauge | — | Active client entries in the map — monitors for unbounded growth |

**Structured logs** (per-IP detail goes here, not in Prometheus):

On budget exceeded (both monitor and enforce mode), log a structured JSON line:
```json
{"event":"rate_limit_exceeded","client_ip":"216.73.216.108","pixels_consumed":524000000,"budget":500000000,"window_seconds":600,"action":"shadow_rejected","request_pixels":19400000,"path":"/0812/...jpx/full/3888,/0/default.jpg"}
```

Infra queries per-IP detail via Loki: `{job="sipi"} | json | event="rate_limit_exceeded"`. Heavy hitters are identified from logs, not from metric labels.

**Monitor mode workflow:**
1. Deploy with `rate_limit_mode=monitor` — all decisions logged, none enforced
2. Dashboard: `rate(sipi_rate_limit_decisions_total{action="shadow_rejected"}[5m])` shows what *would* be blocked
3. Compare shadow rejection rate to total: if > 5%, thresholds may be too aggressive
4. Examine Loki logs for which IPs would be affected
5. Tune `rate_limit_max_pixels` and `rate_limit_pixel_threshold`
6. Switch to `rate_limit_mode=enforce`

**Recommended alerting rules** (for infra to configure in Grafana):
- `RateLimiterHighRejectionRate`: rejection ratio > 10% for 5 minutes (warning)
- `RateLimiterRejectionSpike`: rejections > 3x hourly baseline for 2 minutes (warning)
- `RateLimiterHighLatency`: p99 check latency > 5ms for 5 minutes (critical)
- `RateLimiterShadowHighRejection`: shadow rejections > 5% for 15 minutes (info — thresholds need tuning)

##### 3.2 Health Endpoint (R31-R33) [DEV-5919]

**Target:** `src/SipiHttpServer.cpp` — new handler + route registration in `run()`

```cpp
static void health_handler(Connection &conn_obj, LuaServer &, void *user_data, void *) {
    // R32: Bypass Lua and auth
    auto *server = static_cast<SipiHttpServer *>(user_data);
    auto uptime = std::chrono::steady_clock::now() - server->start_time();
    auto uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();

    // Requires: #include "generated/SipiVersion.h" (not currently included in SipiHttpServer.cpp)
    std::string json = R"({"status":"ok","version":")" + std::string(VERSION)
                     + R"(","uptime_seconds":)" + std::to_string(uptime_sec) + "}";

    conn_obj.header("Content-Type", "application/json");
    conn_obj.setBuffer();
    conn_obj << json;
    conn_obj.flush();
}
```

**Important:** Use `setBuffer()` + write + `flush()` (the "Flush-or-Send" pattern from documented learnings). Direct writes without buffering can cause incomplete responses.

**Route registration** in `SipiHttpServer::run()` — must come before the catch-all `/` route:

```cpp
add_route(Connection::GET, "/health", health_handler);
```

**Files:**

| File | Change |
|------|--------|
| `src/SipiHttpServer.cpp` | Add `health_handler`, register route, track `_start_time`, add `#include "generated/SipiVersion.h"` |
| `src/SipiHttpServer.hpp` | Add `_start_time` member + `start_time()` accessor |
| `Dockerfile` | Add `HEALTHCHECK` instruction for Docker Swarm liveness: `HEALTHCHECK --interval=30s --timeout=5s CMD curl -sf http://localhost:1024/health \|\| exit 1` (`curl` is already installed in the runtime image at line 101) |

**Deployment: Traefik and Docker Swarm health checks:**

**Swarm limitation:** Docker Swarm has a single `HEALTHCHECK` — it does **not** distinguish between liveness and readiness probes (unlike Kubernetes). When a HEALTHCHECK fails, Swarm both stops routing traffic and kills/replaces the container. There is no way to express "alive but temporarily not ready." The `start_period` parameter gives the container time to initialize (failures don't count), but the container **receives traffic** during `start_period` — opposite of Kubernetes readiness behavior.

**Design consequence:** The `/health` endpoint must be designed carefully since it serves double duty. It should return healthy once initialization is complete and only return unhealthy for truly fatal conditions (deadlocks, corrupted state), not for transient load. A future `/readyz` endpoint would need to be implemented at the Traefik level, not via Docker HEALTHCHECK.

Two options for Swarm health:
1. **Docker HEALTHCHECK** (recommended): Add `HEALTHCHECK` to Dockerfile — Docker Swarm uses this for service health natively. No Traefik change needed since the health check is internal.
2. **Traefik route**: Add a label for `/health` routing if external monitoring needs to reach it through Traefik.

The plan recommends option 1 (Docker HEALTHCHECK) for Swarm-internal health.

**UptimeRobot integration:** Currently UptimeRobot monitors SIPI via `/server/test.html` (static file served at `wwwroute = '/server'`, backed by `docroot = './server'`). This goes through Traefik. Two options for the new `/health` endpoint:
1. **Add Traefik label** to expose `/health` externally, then point UptimeRobot at `/health` instead of `/server/test.html`. This is cleaner — `/health` returns structured JSON with version/uptime vs a static HTML page.
2. **Keep `/server/test.html`** for UptimeRobot, use Docker HEALTHCHECK for Swarm. This is the minimal-change option.

Recommendation: option 1 — add a Traefik label for `/health` and update UptimeRobot. The label:
```yaml
- traefik.http.routers.{{ STACK }}-iiif-health.rule=Host(`{{ DSP_IIIF_HOST }}`) && Path(`/health`)
- traefik.http.routers.{{ STACK }}-iiif-health.service={{ STACK }}-iiif
- traefik.http.routers.{{ STACK }}-iiif-health.entrypoints=websecure
- traefik.http.routers.{{ STACK }}-iiif-health.tls=true
- traefik.http.routers.{{ STACK }}-iiif-health.tls.certresolver=leresolver
```

##### 3.3 Replace Python Smoke Tests with Rust [DEV-6031]

**Problem:** The Python smoke tests (`test/smoke/test_smoke.py`) use pytest + testcontainers to validate the Docker image, adding a Python dependency to Docker CI. A Rust `smoke.rs` already exists in `test/e2e-rust/tests/` but tests against the binary (via `common::server()`), not the Docker image.

**Goal:** Confidence that the packaged Docker image works. Smoke tests should answer: "can I deploy this image and serve requests?" They are not exhaustive — that's what the e2e and unit tests are for. Smoke tests validate the packaging (libraries linked, config files present, server starts, core request path works).

**Implementation:** Replace Python tests with Rust `testcontainers`-based tests that start the actual Docker image and validate it. The exact smoke test scenarios should be determined during implementation based on what constitutes a meaningful "does the package work?" check. Research what the testing strategy (`docs/src/development/testing-strategy.md`) says about the smoke test layer and design accordingly.

The new Docker smoke tests can go in the existing `smoke.rs` (as `#[cfg(feature = "docker")]` gated tests) or in a separate `docker_smoke.rs` — whichever keeps the test suite clean.

**Files:**

| File | Change |
|------|--------|
| `test/e2e-rust/tests/smoke.rs` or `docker_smoke.rs` | Docker-image smoke tests using `testcontainers` |
| `test/e2e-rust/Cargo.toml` | Add `testcontainers` dependency |
| `test/smoke/` | **Delete** — Python smoke tests replaced |
| `Makefile` | Update `test-smoke` / `test-smoke-ci` targets to run Rust tests instead of pytest |

**Acceptance:** `make test-smoke` runs Rust tests against Docker image. No Python in CI. Image starts, serves requests, and exercises the core request path.

##### 3.4 Non-Root Docker (R34-R38) [DEV-5920] — DEFERRED

**Deferred to separate initiative.** Switching SIPI to non-root (UID 1000) requires coordinating NFS file ownership across three repositories (sipi, dsp-ingest, ops-deploy) and multiple teams. Both SIPI and dsp-ingest currently run as root; both access the same NFS volumes. Changing one container's UID without coordinating the other will break NFS file permissions. This is tracked as a separate Linear issue.

**What remains in this plan:** R38a (rate limiter ops-deploy config) is NOT about non-root and stays in scope.

##### 3.5 ops-deploy Configuration (R38a, R20 config)

**R38a — Rate limiter config in production** (`roles/dsp-deploy/templates/iiif/conf/sipi.prod-config.lua.j2`):

```lua
-- Rate limiting: per-client pixel budget
rate_limit_max_pixels = {{ DSP_IIIF_RATE_LIMIT_MAX_PIXELS | default(500000000) }},
rate_limit_window = {{ DSP_IIIF_RATE_LIMIT_WINDOW | default(600) }},
rate_limit_mode = "{{ DSP_IIIF_RATE_LIMIT_MODE | default('monitor') }}",
rate_limit_pixel_threshold = {{ DSP_IIIF_RATE_LIMIT_PIXEL_THRESHOLD | default(2000000) }},

-- Per-request pixel limit
max_pixel_limit = {{ DSP_IIIF_MAX_PIXEL_LIMIT | default(100000000) }},
```

And in `roles/dsp-deploy/defaults/main.yml`:

```yaml
DSP_IIIF_RATE_LIMIT_MAX_PIXELS: 500000000      # 500MP per window — tune after observing monitor-mode metrics
DSP_IIIF_RATE_LIMIT_WINDOW: 600                 # 10 minutes — long enough to catch sustained harvesting
DSP_IIIF_RATE_LIMIT_MODE: "monitor"             # Start in monitor mode — log only, no 429s
DSP_IIIF_RATE_LIMIT_PIXEL_THRESHOLD: 2000000    # 2MP — requests below this are free (exempts 1024x1024 tiles)
DSP_IIIF_MAX_PIXEL_LIMIT: 100000000             # 100MP — caps individual request size
```

**Rollout strategy:**
1. **Deploy in `monitor` mode** — rate limiter logs and counts but never returns 429. Infra observes Prometheus metrics (`sipi_rate_limit_decisions_total{action="shadow_rejected"}` for would-be-blocked requests, `sipi_request_pixels` histogram for traffic distribution) and Loki logs for per-IP detail.
2. **Tune thresholds** — adjust `DSP_IIIF_RATE_LIMIT_MAX_PIXELS` and `DSP_IIIF_RATE_LIMIT_PIXEL_THRESHOLD` based on observed legitimate vs bot traffic. Tile-heavy browsing (legitimate) should stay well under the budget; full-image harvesting (bots) should exceed it.
3. **Switch to `enforce` mode** — change `DSP_IIIF_RATE_LIMIT_MODE` to `enforce`. No code change or restart needed beyond config reload.

**Default disambiguation:** The sipi binary's built-in default for `rate_limit_mode` is `off` (disabled entirely). The ops-deploy default is `monitor` — rate limiter is active and observable but non-blocking. The per-request pixel limit (100MP) is always enforced regardless of rate limiter mode. The `rate_limit_pixel_threshold` (2MP) ensures tile-based browsing never consumes budget — only full-region/large-crop requests count. Both per-request limit and rate limiter are needed — per-request caps individual request size, rate limiter caps cumulative large-request load over time.

##### 3.6 Graceful Shutdown (R39-R42) [DEV-5927]

**Target:** `shttps/Server.cpp` — signal handler and accept loop

**Current behavior:** `sig_thread` calls `serverptr->stop()` which sends a message via `stoppipe` (async-signal-safe). `Server::run()` receives this message, removes listening sockets, calls `close_all_dynsocks()`, and broadcasts EXIT to worker threads. In-flight requests are not waited on.

**Architecture constraint:** `Server::stop()` (Server.h:466-475) is explicitly documented as async-signal-safe and uses only POSIX `send()`. The drain logic **cannot** go in `stop()` — it must be in `Server::run()`'s event loop, which receives the stop message.

**R39 — Drain in-flight requests:**

Modify `Server::run()` to implement a two-phase shutdown when it receives the EXIT control message:

```cpp
// In Server::run(), when EXIT message is received (around line 1042):

// Phase 1: Stop accepting new connections
remove_listening_sockets();  // already done today
_drain_mode = true;
_drain_deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(_drain_timeout);

// DO NOT call close_all_dynsocks() or broadcast_exit() yet

// Phase 2: In the main event loop, check drain condition each iteration
if (_drain_mode) {
    if (_active_connections.load() == 0) {
        // R41: Log completion
        log_info("All connections drained, shutting down");
        close_all_dynsocks();
        broadcast_exit();
        break;
    }
    if (std::chrono::steady_clock::now() >= _drain_deadline) {
        // R40: Force-close after timeout
        log_warn("Drain timeout, force-closing %d connections",
                 _active_connections.load());
        close_all_dynsocks();
        broadcast_exit();
        break;
    }
    // R41: Log progress periodically
}
```

This requires:
- `std::atomic<int> _active_connections` counter in `Server`
- RAII guard in `socket_request_processor` (line 703, `PROCESS_REQUEST` case):
  ```cpp
  case SocketControl::PROCESS_REQUEST: {
      auto guard = ConnectionGuard(server->_active_connections); // ++on construct, --on destruct
      // ... existing request processing (SockStream, Connection, handler) ...
  } // guard destructor decrements, even on exception
  ```
- `bool _drain_mode` flag (checked each event loop iteration)
- `_drain_timeout` config (default 30s)
- `_drain_deadline` time point
- **Do not modify `stop()`** — it remains async-signal-safe

**R42 — ops-deploy** (`docker-compose-iiif.yml.j2`):

```yaml
services:
  iiif:
    stop_grace_period: 35s  # drain timeout (30s) + buffer (5s)
```

**Files:**

| File | Change |
|------|--------|
| `shttps/Server.h` | Add `_active_connections` atomic, `_drain_mode` flag, `_drain_timeout`, `_drain_deadline` |
| `shttps/Server.cpp` | Implement two-phase drain in `run()` event loop (NOT in `stop()`), add connection counter RAII guard in `socket_request_processor` |
| `include/SipiConf.h` | Add `drain_timeout` field |
| `src/SipiConf.cpp` | Parse from Lua config |
| `src/sipi.cpp` | CLI option `--drain-timeout` / `SIPI_DRAIN_TIMEOUT` |
| `config/sipi.config.lua` | Document `drain_timeout` |

##### Phase 3 Testing

| Test Type | What | Where |
|-----------|------|-------|
| E2E (Rust) | Monitor mode: budget exceeded → logged, request proceeds | `test/e2e-rust/tests/rate_limiter.rs` |
| E2E (Rust) | Enforce mode: budget exceeded → 429 with `Retry-After` (R26) | `test/e2e-rust/tests/rate_limiter.rs` |
| E2E (Rust) | Client identity resolution (XFF, peer IP) | `test/e2e-rust/tests/rate_limiter.rs` |
| E2E (Rust) | Tile-heavy browsing stays under budget | `test/e2e-rust/tests/rate_limiter.rs` |
| E2E (Rust) | `/health` returns 200 with JSON, version, uptime | `test/e2e-rust/tests/health.rs` |
| E2E (Rust) | Graceful shutdown: in-flight request completes | `test/e2e-rust/tests/shutdown.rs` |
| Smoke (Rust) | Docker image packaging validation (starts, serves requests, core path works) | `test/e2e-rust/tests/smoke.rs` or `docker_smoke.rs` |
| Load test | Bot scenario → 429, sustained load → memory stable | `test/load/` |

##### Phase 3 Documentation

Two infra runbooks to be created in `docs/src/operation/`:

**`rate-limiter.md`** — everything infra needs to operate the rate limiter:
- What the rate limiter does and why (link to OOM incident context)
- Configuration parameters: `rate_limit_mode`, `rate_limit_max_pixels`, `rate_limit_window`, `rate_limit_pixel_threshold` — what each does, defaults, and how to tune
- Monitor → enforce workflow: step-by-step guide for deploying in monitor mode, observing metrics, tuning thresholds, switching to enforce
- Prometheus metrics reference: all 5 metrics with descriptions, PromQL examples
- Recommended Grafana alerting rules (copy-pasteable YAML)
- Loki queries for per-IP investigation: how to find heavy hitters, how to see what would be blocked
- Traffic pattern reference: tile sizes (1024×1024 JP2, 256×256 future TIFF), pixels per tile, what legitimate vs bot traffic looks like
- Troubleshooting: false positives (raise budget), false negatives (lower threshold), rate limiter latency

**`health-endpoint.md`** — health endpoint and monitoring:
- `/health` endpoint: response format, what it checks, response time expectations
- Docker Swarm HEALTHCHECK: single probe (no liveness/readiness split), `start_period`, what happens on failure (container killed and replaced)
- UptimeRobot migration: how to switch from `/server/test.html` to `/health` via Traefik label
- Traefik label configuration for exposing `/health` externally

## Alternative Approaches Considered

### Input Validation: WAF vs Application-Level

A Web Application Firewall (e.g., ModSecurity rules in Traefik) could block path traversal and null bytes. Rejected because: (a) SIPI is deployed in varied environments where WAF may not be available, (b) application-level validation is defense-in-depth regardless of infrastructure, (c) the `realpath()` check is the definitive server-side guard.

### Memory Safety: Full RAII Modernization vs Minimal Fixes

Converting `SipiImage::pixels` from `byte*` to `std::unique_ptr<byte[]>` would eliminate the class of leak bugs entirely. Rejected for this PR because it touches every format handler (SipiIOTiff, SipiIOJ2k, SipiIOJpeg, SipiIOPng) and Lua bindings — too large a change surface. The minimal fix (delete before allocate) is safe and verifiable under ASan. Full RAII modernization should be a follow-up.

### Rate Limiter: Token Bucket vs Sliding Window

A token bucket algorithm would be simpler but doesn't provide the `Retry-After` header value (no way to know when the next token arrives for pixel-denominated budgets). The sliding window tracks actual records, making `Retry-After` computation straightforward.

### Graceful Shutdown: pthread_cancel vs Cooperative Drain

Force-killing threads via `pthread_cancel` would be simpler but leaves resources in undefined state. Cooperative drain (wait for handlers to complete, with a timeout) is safer and matches Docker Swarm `stop_grace_period` expectations.

## Acceptance Criteria

### Functional Requirements

- [ ] Path traversal: `/../etc/passwd`, `%2e%2e`, double-encoded variants → HTTP 400
- [ ] Path traversal: blocked attempts logged with client IP and raw identifier
- [ ] Path traversal: error responses contain no internal filesystem paths (R3, including SipiHttpServer.cpp:434)
- [ ] Null byte: `%00` in any URL → HTTP 400, server stays healthy, fix in `shttps/` layer
- [ ] Header injection: `\r\n` in identifier → safe Content-Disposition per RFC 6266
- [ ] Header injection: all headers interpolating user input sanitized
- [ ] SipiImage copy/assignment: no ASan findings, no memory leaks
- [ ] SipiImage arithmetic: operators return by value, no heap leaks, `operator+=` 16-bit case uses addition (not subtraction)
- [ ] SipiFilenameHash: `filepath()` correct after copy/assignment
- [ ] Rate limiter: monitor mode logs budget exceeded without returning 429
- [ ] Rate limiter: enforce mode returns HTTP 429 after budget exceeded
- [ ] Rate limiter: client identity resolves through XFF > peer IP
- [ ] Rate limiter: `Retry-After` header present on 429 with correct seconds value (R26)
- [ ] Rate limiter: Prometheus metrics visible (`decisions_total`, `near_limit_total`, `check_duration_seconds`, `request_pixels` histogram, `clients_tracked`)
- [ ] Rate limiter: no mutex contention visible under 8 workers
- [ ] `/health` returns 200 with version and uptime within 5ms
- [ ] Smoke tests migrated to Rust — `make test-smoke` runs Rust tests, no Python in CI
- [ ] Graceful shutdown: in-flight requests complete during rolling deployment

### Non-Functional Requirements

- [ ] All memory fixes follow C++23 style guide ownership rules
- [ ] Zero new ASan/UBSan findings in CI
- [ ] Memory growth rate < 10 MB/hour under sustained load test
- [ ] Health endpoint < 5ms p99 response time
- [ ] Rate limiter per-client map doesn't grow unbounded

### Quality Gates

- [ ] All existing tests pass (unit, e2e, smoke, hurl, fuzz)
- [ ] New tests cover each fix (fuzz corpus for input validation, e2e for rate limiter, unit for memory fixes)
- [ ] ops-deploy changes pass `just lint dsp-deploy` and `just test dsp-deploy -x`
- [ ] Conventional Commits used for all commits (drives release-please)

## Success Metrics

- Zero security findings from input validation fuzz testing
- Zero ASan/UBSan findings in CI
- Memory growth rate < 10 MB/hour under sustained load test
- Rate limiter: confirmed 429 in load test matching OOM incident pattern
- Health endpoint: < 5ms p99
- Graceful shutdown: zero 502/504 during rolling deployment (verify in staging)

## Dependencies & Prerequisites

| Dependency | Type | Status |
|-----------|------|--------|
| PR #523 branch (`claude/fix-sipi-oom-crash-ZPTzX`) | Base branch | Open — all work committed here |
| PR #519 (test infrastructure) | Test framework | Merged |
| PR #516 (cache rewrite) | Metrics pattern | Merged |
| Traefik X-Forwarded-For header | External config | Already configured — Traefik forwards client IP by default |

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Path traversal fix breaks legitimate identifiers with special characters | L | H | `realpath()` allowlist approach (not blocklist); test against production identifier patterns |
| Null byte fix in shttps breaks non-IIIF routes | L | M | Intentionally broad — null bytes are never valid in HTTP URIs (RFC 3986) |
| SipiImage operator changes break Lua image arithmetic | L | M | Return-by-value is ABI-compatible for callers; run full Lua test suite |
| Rate limiter false positives in NAT environments | M | M | Configurable budget, generous 10-minute window, monitor mode for initial validation. XFF provides per-client IP behind Traefik |
| Graceful shutdown timeout too short for large images | L | L | Default 30s generous (typical tile < 1s); configurable |
| ops-deploy deployment model | — | — | PRD and plan use Docker Compose/Swarm terminology (`stop_grace_period`, `user:`) |

## File Manifest

### sipi

| File | Phase | Action | Requirements |
|------|-------|--------|-------------|
| `src/SipiHttpServer.cpp` | 1, 3 | Modify | R1-R4 (path traversal), R8-R10 (header injection), R23-R30 (rate limiter), R31-R33 (health) |
| `src/SipiHttpServer.hpp` | 3 | Modify | Rate limiter member, health endpoint accessors |
| `shttps/Connection.cpp` | 1 | Modify | R5-R7 (null byte rejection) |
| `src/SipiImage.cpp` | 2 | Modify | R11-R16 (copy/assign, arithmetic) |
| `src/SipiImage.hpp` | 2 | Modify | R14 (operator signatures, move constructor/assignment) |
| `src/SipiFilenameHash.cpp` | 2 | Modify | R17-R18 (pointer → value) |
| `include/SipiFilenameHash.h` | 2 | Modify | R17 (member type change) |
| `include/SipiRateLimiter.h` | 3 | **New** | R23-R29 |
| `src/SipiRateLimiter.cpp` | 3 | **New** | R23-R29 |
| `include/SipiConf.h` | 3 | Modify | Rate limiter + drain timeout config |
| `src/SipiConf.cpp` | 3 | Modify | Parse new config fields |
| `src/sipi.cpp` | 3 | Modify | CLI options for rate limiter + drain timeout |
| `config/sipi.config.lua` | 3 | Modify | Document new config entries |
| `include/SipiMetrics.h` | 3 | Modify | Rate limiter metrics: `decisions_total`, `near_limit_total`, `check_duration_seconds`, `request_pixels`, `clients_tracked` |
| `src/SipiMetrics.cpp` | 3 | Modify | Initialize rate limiter metrics (counters, histogram, gauge) |
| `shttps/Server.h` | 3 | Modify | R39-R41 (`_active_connections` atomic, `_drain_mode`, `_drain_timeout`, `_drain_deadline`, `ConnectionGuard` RAII class) |
| `shttps/Server.cpp` | 3 | Modify | R39-R41 (two-phase drain in `run()` event loop, connection counter in `socket_request_processor`) |
| `Dockerfile` | 3 | Modify | R31 (`HEALTHCHECK` for Docker Swarm) |
| `CMakeLists.txt` | 3 | Modify | Add SipiRateLimiter source |
| `test/unit/input_validation/` | 1 | **New** | Unit tests for `contains_traversal()`, `sanitize_header_value()`, `validate_resolved_path()` |
| `test/e2e-rust/tests/input_validation.rs` | 1 | **New** | Phase 1 e2e tests |
| `test/e2e-rust/tests/security.rs` | 1 | Modify | Tighten CRLF injection assertion (200→400) |
| `test/e2e-rust/tests/rate_limiter.rs` | 3 | **New** | Rate limiter e2e tests |
| `test/e2e-rust/tests/health.rs` | 3 | **New** | Health endpoint test |
| `test/e2e-rust/tests/shutdown.rs` | 3 | **New** | Graceful shutdown test |
| `test/unit/filenamehash/` | 2 | Modify | R19 (update for correct behavior) |
| `test/e2e-rust/tests/smoke.rs` or `docker_smoke.rs` | 3 | Modify/**New** | Docker-image smoke tests replacing Python (DEV-6031) |
| `test/e2e-rust/Cargo.toml` | 3 | Modify | Add `testcontainers` dependency |
| `test/smoke/` | 3 | **Delete** | Python smoke tests replaced by Rust |
| `Makefile` | 3 | Modify | Update `test-smoke` targets to run Rust tests |
| `docs/src/operation/rate-limiter.md` | 3 | **New** | Infra runbook: rate limiter config, monitor→enforce workflow, Prometheus metrics, Grafana alerting rules, Loki queries for per-IP investigation |
| `docs/src/operation/health-endpoint.md` | 3 | **New** | Infra runbook: health endpoint behavior, Docker Swarm HEALTHCHECK (single probe, no liveness/readiness split), UptimeRobot migration, Traefik label config |

### ops-deploy

| File | Phase | Action | Requirements |
|------|-------|--------|-------------|
| `roles/dsp-deploy/templates/docker-compose-iiif.yml.j2` | 3 | Modify | R42 (`stop_grace_period:`), R31 (Traefik `/health` label for UptimeRobot) |
| `roles/dsp-deploy/templates/iiif/conf/sipi.prod-config.lua.j2` | 3 | Modify | R38a (rate limiter config), R20 (pixel limit config) |
| `roles/dsp-deploy/defaults/main.yml` | 3 | Modify | Default values for new config vars |

## Commit Strategy

Follow [Conventional Commits](https://www.conventionalcommits.org/) per sipi's `CLAUDE.md`. Group by logical change:

```
# Phase 1
fix(security): prevent path traversal in IIIF identifier handling
fix(security): reject null bytes in HTTP request parsing layer
fix(security): sanitize Content-Disposition header against injection

# Phase 2
fix(memory): null-check shared_ptr metadata in SipiImage copy constructor
fix(memory): free old pixel buffer in SipiImage assignment operator
fix(memory): return SipiImage arithmetic operators by value and fix operator+= 16-bit logic bug
fix(memory): convert SipiFilenameHash hash pointer to value member

# Phase 3
feat(rate-limit): add per-client pixel consumption rate limiter
feat(health): add /health endpoint with version and uptime
feat(shutdown): graceful connection drain on SIGTERM
refactor(test): replace Python smoke tests with Rust testcontainers
docs(operation): add rate limiter and health endpoint infra runbooks

# Tests (per phase or grouped)
test: add input validation fuzz corpus and e2e tests
test: add memory safety unit tests under ASan
test: add rate limiter, health, and shutdown e2e tests

# ops-deploy (separate repo)
feat(dsp-deploy): add SIPI rate limiter config, health route, and graceful shutdown
```

## Future Considerations

- **Non-root Docker (R34-R38)** — deferred to separate initiative requiring cross-repo coordination (sipi, dsp-ingest, ops-deploy) for NFS file ownership and UID alignment
- **Full RAII modernization** of `SipiImage::pixels` → `std::unique_ptr<byte[]>` (DEV-6034, follow-up PR, touches all format handlers)
- **Rate limiter sharding** — if mutex contention is observed at scale, shard by client ID hash
- **`/readyz` endpoint** — separate startup probe if health check becomes insufficient (not possible with Swarm's single-probe model; would require Traefik-level readiness check)
- **SSL removal** — SIPI runs behind Traefik; built-in SSL adds complexity without benefit (DEV-6035)
- **CONVENTIONS.md migration** — move repo-specific architectural patterns from plugin to per-repo file (DEV-6032)

## References & Research

### Internal References

- PRD: `dasch-specs/specs/2026-03-14-sipi-production-hardening/01-sipi-production-hardening-PRD.md`
- Rate limiter proposal: `sipi/docs/proposals/pixel-rate-limiter.md`
- C++23 style guide: `sipi/docs/src/development/cpp-style-guide.md`
- Review guidelines: `sipi/REVIEW.md`
- Testing strategy: `sipi/docs/src/development/testing-strategy.md`
- Commit conventions: `sipi/docs/src/development/commit-conventions.md`
- Path traversal location: `src/SipiHttpServer.cpp:1742` (URI parsing in `serve_iiif`)
- Header injection location: `src/SipiHttpServer.cpp:1162` (Content-Disposition)
- Signal handling: `shttps/Server.cpp:51-74` (`sig_thread`)
- ops-deploy SIPI service: `roles/dsp-deploy/templates/docker-compose-iiif.yml.j2:42`
- ops-deploy SIPI config: `roles/dsp-deploy/templates/iiif/conf/sipi.prod-config.lua.j2`

### External References

- RFC 6266 (Content-Disposition): https://www.rfc-editor.org/rfc/rfc6266
- RFC 3986 (URI syntax, null bytes invalid): https://www.rfc-editor.org/rfc/rfc3986
- CIS Docker Benchmark 4.1 (non-root containers): https://www.cisecurity.org/benchmark/docker
- OWASP Path Traversal: https://owasp.org/www-community/attacks/Path_Traversal
