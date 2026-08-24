---
title: "SIPI Production Hardening"
date: 2026-03-14
author: "Ivan Subotic"
status: reviewed(2)
repositories:
  - sipi
  - ops-deploy
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
  - DEV-5920
  - DEV-5927
  - DEV-6033
---

# SIPI Production Hardening

## Context

SIPI is the IIIF-compatible image server powering the DaSCH Service Platform. A production readiness audit in February 2026 uncovered 11 issues spanning input validation, memory safety, and operational infrastructure. Separately, a production OOM incident caused by a bot systematically requesting full-resolution renders exposed the need for per-request pixel limits and per-client rate limiting (PR #523).

This PRD covers a coordinated hardening sweep that addresses all findings in a single initiative, rather than treating each as an isolated fix. The issues cluster into natural groups that share code paths and testing requirements, making a bundled approach more efficient than 11+ separate PRs.

### Production Incident Context

In February 2026, a single bot (216.73.216.108) caused an OOM crash by systematically requesting full-resolution renders of large images. Individual requests were valid, but the cumulative load exhausted server memory. PR #523 addresses this with per-request pixel limits, connection liveness checks, and `bad_alloc` handling as immediate mitigations, plus a per-client rate limiter as the long-term defense.

### Relationship to PR #519 (Test Infrastructure) and PR #516 (Cache Rewrite)

PR #519 added comprehensive Rust e2e tests, fuzz testing, and sanitizer support. PR #516 rewrote the cache subsystem with LRU eviction and Prometheus metrics. This hardening initiative builds on both: the test infrastructure provides the verification framework, and the cache rewrite resolved the previous production disk-fill incident. This is the next layer of production readiness.

## Goals

1. **Eliminate input validation vulnerabilities**: Path traversal, null byte injection, and header injection must be blocked before any file I/O or response construction.
2. **Fix all known memory safety bugs**: Memory leaks and unsafe pointer handling in SipiImage and SipiFilenameHash must be corrected to prevent unbounded memory growth in long-running production deployments.
3. **Protect against resource exhaustion**: Per-request pixel limits and per-client rate limiting must prevent any single client from exhausting server memory through cumulative load.
4. **Improve operational posture**: Health endpoints and graceful shutdown must bring SIPI in line with Docker Compose/Swarm operational best practices and reduce deployment risk. Non-root Docker (R34-R38) is deferred to a separate coordinated initiative.

## Non-Goals

- SSL/TLS refactoring (SIPI runs behind Traefik; SSL removal is a separate initiative)
- Image format additions or IIIF API extensions
- Performance optimization beyond what the fixes naturally provide
- Lua scripting security (separate concern, different attack surface)
- Separate `/readyz` endpoint (may be added later if startup probes are needed; `/health` is sufficient for initial rollout)

## Backwards Compatibility

The input validation fixes (F1) intentionally change HTTP responses for malicious or malformed inputs:

| Input | Before | After |
|-------|--------|-------|
| `/../etc/passwd` in IIIF identifier | File contents or 500 error | HTTP 400 |
| `%00` in URL path | Connection reset / crash | HTTP 400 |
| `\r\n` in Content-Disposition filename | Header injection possible | Sanitized header |

These are breaking changes for malicious inputs only. Legitimate IIIF requests are unaffected — the `realpath()` check (R2) validates that the resolved path is within `imgroot()`, which always succeeds for properly stored images.

## Dependencies

- **Traefik configuration** (external): The rate limiter's `X-Client-Fingerprint` header (R25) requires Traefik to be configured to forward a client fingerprint header. Without this, the rate limiter falls back to `X-Forwarded-For` or peer IP, which may be less granular in NAT environments.
- **PR #519** (merged): Test infrastructure (Rust e2e, fuzz, sanitizers) used to verify all fixes
- **PR #516** (merged): Cache rewrite — no direct dependency, but establishes the Prometheus metrics pattern used by the rate limiter
- **PR #523** (open, branch `claude/fix-sipi-oom-crash-ZPTzX`): Per-request pixel limit, OOM safeguards, and rate limiter already implemented. This is the implementation branch for the entire PRD — all phases will be committed here

## Core Features

### F1: Input Validation (Security)

Block malicious input at the HTTP handler boundary before any downstream processing.

#### F1.1: Path Traversal Prevention [DEV-5913]

**Problem:** `src/SipiHttpServer.cpp:422-424` — IIIF identifiers are concatenated directly into filesystem paths without validation. An attacker can use `../` sequences or URL-encoded variants to read arbitrary files on the server.

**Requirements:**
- R1: After URL-decoding the IIIF identifier, reject any identifier containing `..` path components (literal `..`, URL-encoded `%2e%2e`, double-encoded variants)
- R2: After constructing the full file path, call `realpath()` and verify the resolved path starts with the configured image root directory
- R3: Return HTTP 400 with a generic error message (no path information leaked)
- R4: Log the blocked attempt with client IP and raw identifier for security monitoring

#### F1.2: Null Byte Injection Prevention [DEV-6023]

**Problem:** `%00` in URL paths causes a connection reset — likely a C-string `strlen` truncation somewhere in the request parsing chain that corrupts subsequent processing.

**Requirements:**
- R5: Reject requests containing null bytes (`\0`, `%00`) in any URL component before they reach any C-string operation
- R6: Return HTTP 400 for requests containing null bytes
- R7: The fix must be in the HTTP parsing layer (shttps), not per-handler, to protect all routes

#### F1.3: Header Injection Prevention [DEV-5916]

**Problem:** `src/SipiHttpServer.cpp:1162` — the IIIF identifier is interpolated into a `Content-Disposition` header without escaping. An attacker can inject arbitrary HTTP headers or split the response.

**Requirements:**
- R8: Sanitize the filename used in `Content-Disposition`: strip or encode CR (`\r`), LF (`\n`), null bytes, and non-printable characters
- R9: Properly quote the filename parameter per RFC 6266 (use `filename*=UTF-8''...` for non-ASCII)
- R10: Apply the same sanitization to any other header that interpolates user input

### F2: Memory Safety

Fix memory management bugs that cause leaks or undefined behavior in long-running server processes.

#### F2.1: SipiImage Copy/Assignment Fixes [DEV-5915, DEV-5914]

**Problem:** The copy constructor dereferences metadata `shared_ptr`s without null checks (crash on images without metadata). The assignment operator allocates new pixel buffers without deleting old ones (memory leak on every assignment).

**Requirements:**
- R11: Copy constructor must null-check all `shared_ptr` metadata members before dereferencing
- R12: Assignment operator must free the old pixel buffer before allocating a new one (the implementation should modernize `pixels` to RAII ownership per the C++23 style guide (`sipi/docs/src/development/cpp-style-guide.md`) Rule of Zero, but at minimum the leak must be fixed)
- R13: Assignment operator must handle the case where `bufsiz == 0` but old `pixels` is non-null

#### F2.2: SipiImage Arithmetic Operator Fixes [DEV-5917, DEV-5918]

**Problem:** `operator-` and `operator+` allocate on the heap with `new` and return a reference — the caller cannot delete the memory. `operator-=` and `operator+=` use raw `new byte[]` without RAII, leaking on exceptions.

**Requirements:**
- R14: `operator-` and `operator+` must return by value (compiler applies RVO/move semantics)
- R15: `operator-=` and `operator+=` must use `std::unique_ptr<byte[]>` for temporary buffers, transferring ownership only on success
- R16: All arithmetic operators must be covered by unit tests that verify no memory leaks (run under ASan)

#### F2.3: SipiFilenameHash Memory Fixes [DEV-6002]

**Problem:** Copy constructor doesn't copy `path`/`name` members. Assignment operator leaks old `hash` pointer. Both bugs stem from manual memory management of a `std::vector<char>*` pointer.

**Requirements:**
- R17: Convert `std::vector<char> *hash` to a value member (`std::vector<char> hash`), eliminating manual memory management entirely (Rule of Zero)
- R18: Ensure `filepath()` returns the complete path including filename after copy/assignment
- R19: Existing unit tests in `test/unit/filenamehash/` must be updated to verify correct behavior (they currently document the broken behavior and will need adjustment after the fix)

### F3: Resource Exhaustion Protection

Prevent any single client from exhausting server resources through cumulative load.

#### F3.1: Per-Request Pixel Limit (implemented in PR #523)

**Problem:** Individual requests for extremely large image renders can consume gigabytes of memory.

**Status:** R20-R22 are already implemented in PR #523. Listed here for completeness and to document the design decisions.

**Requirements:**
- R20: Configurable `max_pixel_limit` (CLI, env, Lua config) — reject requests exceeding this limit with HTTP 400 (Bad Request)
- R21: Connection liveness check before expensive processing — if the client disconnected, skip the work
- R22: `bad_alloc` handler that returns HTTP 500 instead of crashing the server

#### F3.2: Per-Client Rate Limiter [DEV-6033]

**Problem:** A single bot can exhaust server memory through cumulative load even when individual requests are within the pixel limit.

**Requirements:**
- R23: Sliding-window rate limiter tracking per-client pixel consumption
- R24: Configurable window (default 60s) and budget (default 0 = disabled) via CLI, env, Lua config. Ships disabled by default for safe rollout — F3.1's per-request pixel limit provides first-line defense independent of rate limiter configuration
- R25: Client identity resolution: custom header (e.g. `X-Client-Fingerprint` from Traefik) > `X-Forwarded-For` rightmost IP > peer IP
- R26: HTTP 429 response with `Retry-After` header (seconds until oldest record expires)
- R27: Optimistic budget deduction (record at check time, before processing)
- R28: Thread-safe with acceptable contention for 8 worker threads
- R29: Memory hygiene: lazy per-client cleanup on every check, full map sweep every N checks
- R30: Prometheus counter `sipi_rate_limited_total`

### F4: Operational Infrastructure

Bring SIPI in line with Docker Compose/Swarm operational best practices. (ops-deploy uses Docker Compose with Swarm mode, not Kubernetes.)

#### F4.1: Health Endpoints [DEV-5919]

**Problem:** No dedicated health check endpoint. Operators must hit IIIF image endpoints which may be slow or misleading.

**Requirements:**
- R31: `/health` endpoint returning JSON `{"status": "ok", "version": "x.y.z", "uptime_seconds": N}`
- R32: Must bypass Lua script handling and authentication for speed
- R33: Direct C++ handler in `SipiHttpServer`, no image processing

#### F4.2: Non-Root Docker [DEV-5920] — DEFERRED

> **Note:** R34-R38 are valid but deferred from this initiative. Infra will handle non-root Docker as part of a larger coordinated effort to harden all DaSCH Docker images. SIPI shares NFS volumes with dsp-ingest — both currently run as root — so switching UIDs requires cross-repo coordination (sipi, dsp-api/dsp-ingest, ops-deploy). See DEV-5920 for the full NFS analysis and deployment checklist.

**Problem:** Container runs as root, violating least privilege and CIS Docker Benchmark 4.1.

**Requirements:**
- R34: Create dedicated `sipi` user/group (UID 1000) in Dockerfile
- R35: `chown` necessary directories (image root, cache, config, tmp) to `sipi:sipi`
- R36: `USER sipi` directive before `ENTRYPOINT`
- R37: Verify SIPI works on non-privileged port 1024 (already the default)
- R38: Update ops-deploy: set `user: "1000:1000"` in Docker Compose service definition, ensure volume mounts are writable by UID 1000
- R38a: Update ops-deploy: enable rate limiter in production with recommended budget (e.g., `rate_limit_max_pixels = 50000000`, `rate_limit_window = 60`) — exact values to be validated against production traffic patterns before deployment

#### F4.3: Graceful Shutdown [DEV-5927]

**Problem:** SIGTERM handling stops accepting connections but may interrupt in-flight requests, causing truncated responses during deployments.

**Requirements:**
- R39: On SIGTERM: stop accepting new connections (already done), then wait for in-flight requests with configurable timeout (default 30s)
- R40: Force-close remaining connections after timeout
- R41: Log shutdown progress (draining N connections, timeout reached, etc.)
- R42: Update ops-deploy: set `stop_grace_period: 35s` (drain timeout + 5s buffer) in Docker Compose service definition

## Acceptance Criteria

### Input Validation
- [ ] Path traversal: `/../etc/passwd`, `%2e%2e`, and double-encoded variants return HTTP 400
- [ ] Path traversal: blocked attempts logged with client IP and raw identifier (R4)
- [ ] Null byte: `%00` in URLs returns HTTP 400, server stays healthy
- [ ] Null byte: fix is in `shttps/` layer, protecting all routes including Lua handlers (R7)
- [ ] Header injection: identifiers with `\r\n` produce safe Content-Disposition per RFC 6266 (R9)
- [ ] Header injection: all headers interpolating user input are sanitized, not just Content-Disposition (R10)

### Memory Safety
- [ ] SipiImage copy/assignment: no ASan findings, no memory leaks under sustained load
- [ ] SipiImage arithmetic: operators return by value, no heap leaks under ASan
- [ ] SipiFilenameHash: `filepath()` correct after copy/assignment, no ASan findings
- [ ] All memory fixes follow C++23 style guide (`sipi/docs/src/development/cpp-style-guide.md`) ownership rules (Rule of Zero, no raw owning pointers)

### Resource Exhaustion
- [ ] Per-request pixel limit: oversized requests return HTTP 400 (already in PR #523)
- [ ] Rate limiter: bot scenario returns HTTP 429 after budget exceeded, normal browsing unaffected
- [ ] Rate limiter: client identity resolves correctly through custom header > X-Forwarded-For > peer IP (R25)
- [ ] Rate limiter: no mutex contention visible under 8 concurrent workers (R28)
- [ ] Rate limiter: per-client map doesn't grow unbounded — stale entries cleaned up (R29)

### Operational
- [ ] `/health` returns 200 with version and uptime within 5ms
- [ ] ~~Docker image runs as non-root (UID 1000)~~ — DEFERRED (see F4.2)
- [ ] ops-deploy manifests updated with `stop_grace_period`, Traefik `/health` label, and rate limiter budget (R38a)
- [ ] Graceful shutdown: in-flight requests complete during rolling deployment

### Cross-Cutting
- [ ] All existing tests pass (unit, e2e, smoke, hurl, fuzz)
- [ ] New tests cover each fix (fuzz corpus for input validation, e2e for rate limiter, unit for memory fixes)
- [ ] Zero new ASan/UBSan findings in CI

## Implementation Phases

The issues cluster into three natural phases based on dependency and risk:

### Phase 1: Input Validation + OOM Safeguards (Highest Priority)

**Why first:** These are externally exploitable — a malicious actor can trigger them today.

- F1.1 Path traversal prevention
- F1.2 Null byte injection prevention
- F1.3 Header injection prevention
- F3.1 Per-request pixel limit (PR #523, already implemented)

**Estimated scope:** ~4 source files (`SipiHttpServer.cpp`, `Connection.cpp`, `SipiIdentifier.cpp`, connection handling) plus fuzz corpus entries and e2e tests

### Phase 2: Memory Safety (High Priority)

**Why second:** These cause production instability over time but aren't externally triggerable on demand.

- F2.1 SipiImage copy/assignment
- F2.2 SipiImage arithmetic operators
- F2.3 SipiFilenameHash memory bugs

**Estimated scope:** ~4 source files (`SipiImage.cpp`, `SipiImage.hpp`, `SipiFilenameHash.cpp`, `SipiFilenameHash.h`) plus unit tests

### Phase 3: Rate Limiting + Operational (Medium Priority)

**Why third:** Defense-in-depth and operational improvements that build on the fixes above.

- F3.2 Per-client rate limiter
- F4.1 Health endpoints
- ~~F4.2 Non-root Docker~~ — DEFERRED (see F4.2 note)
- F4.3 Graceful shutdown

**Estimated scope:** ~8 source files modified/created (rate limiter class, server integration, config, Dockerfile, ops-deploy manifests) plus e2e tests and load test scenario

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Path traversal fix breaks legitimate identifiers containing special characters | IIIF requests fail for some images | Validate against actual production identifier patterns before deploying; use allowlist approach (realpath check) not blocklist |
| Null byte fix in shttps breaks non-IIIF routes | Other HTTP handlers affected | Fix in parsing layer is intentionally broad — null bytes are never valid in HTTP URIs |
| SipiImage operator changes break Lua scripts using image arithmetic | Lua image operations fail | Return-by-value is ABI-compatible for callers; run full Lua test suite |
| Rate limiter false positives in multi-user NAT environments | Legitimate users blocked | Configurable budget with generous default; custom header support for Traefik fingerprinting |
| Non-root Docker breaks volume permissions | Container can't read/write images | Test with actual production volume mounts; document required UID/GID |
| Graceful shutdown timeout too short for large image processing | Requests still interrupted | Default 30s is generous (typical IIIF tile < 1s); configurable for edge cases |

## Success Metrics

- Zero security findings from input validation fuzz testing (run against all three vulnerability classes)
- Zero ASan/UBSan findings in CI (already running via PR #519 infrastructure)
- Memory growth rate < 10 MB/hour under sustained load test (validates memory safety fixes beyond point-in-time ASan checks)
- Rate limiter: confirmed 429 response in load test scenario matching the original OOM incident pattern
- Health endpoint: < 5ms p99 response time
- Graceful shutdown: zero 502/504 errors during rolling deployment (verify in staging)
