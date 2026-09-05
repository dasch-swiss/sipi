---
title: "SIPI Cache Management Fix"
type: PRD
status: implemented
created: 2026-03-04
repositories:
  - sipi
linear:
  - DEV-5610
  - INFRA-940
  - INFRA-955
  - INFRA-996
  - INFRA-1058
---

# SIPI Cache Management Fix

## Context

SIPI is the image server used by the DaSCH Service Platform. It maintains a file-based cache of converted images (e.g., IIIF tile conversions) to avoid re-processing on repeated requests.

In November 2025, production's `/var` partition filled up partly due to SIPI's cache growing to 4.8GB (~9000 files in ~40 hours). Investigation revealed:

- The prod config had `cachesize = '0M'`, which disables the cache size limit entirely
- The code default for `cache_nfiles` is `0` (unlimited), contradicting documentation claiming a 200-file default
- Even when limits are configured, SIPI doesn't track pre-existing cache files after a restart — it starts counting from zero while old files remain on disk, causing unbounded growth across restarts

Interim mitigations were applied: the cache directory was moved to an NFS share (INFRA-940) and a 1GB limit was configured (INFRA-955). However, the NFS mount introduced a significant operational problem: SIPI startup now takes over 3 minutes (scanning/accessing the cache over the network), which severely impacts availability. Due to Docker/Docker-Compose limitations of having only a single health route, the health check timeout must be raised to accommodate this slow startup, meaning crashes take longer to detect _and_ longer to recover from. This makes the NFS mitigation untenable long-term — the cache needs to work correctly on local disk so the NFS mount can be removed.

### Related Issues

- [DEV-5610](https://linear.app/dasch/issue/DEV-5610/sipi-cache-default-limit-not-working) — Root issue: cache limit not working
- [INFRA-940](https://linear.app/dasch/issue/INFRA-940/move-sipi-cache-dir-into-tmp-network-share) — Mitigation: move cache to NFS
- [INFRA-955](https://linear.app/dasch/issue/INFRA-955/configure-sipi-cache-limit) — Mitigation: configure 1GB limit
- [INFRA-996](https://linear.app/dasch/issue/INFRA-996/sipi-image-request-metrics) — External SIPI request metrics (Done)
- [INFRA-1058](https://linear.app/dasch/issue/INFRA-1058/observe-sipi-cache) — Observe cache behavior with new limits (Open)

## Goals

1. **Fix cache persistence across restarts**: On startup, SIPI must index existing cache files and count them toward configured limits, so the cache never grows beyond its configured bounds regardless of restarts.
2. **Fix defaults to match documentation**: Ensure code defaults for `cache_size` and `cache_nfiles` match documented behavior (200MB / 200 files), or update documentation to accurately reflect actual defaults.
3. **Clarify configuration semantics**: Adopt clear, unambiguous semantics for cache configuration values.
4. **Replace broken eviction**: Remove the broken hysteresis-based eviction and replace with LRU (least recently used) eviction using high-water/low-water marks.
5. **Enable return to local filesystem**: Once cache limits are reliably enforced, remove the NFS mount for the cache directory, restoring fast startup times and improving crash recovery/availability.
6. **Add cache observability**: Expose Prometheus metrics for cache utilization, hit rates, and eviction activity.

## Core Features

### F1: Cache Startup Indexing

When SIPI starts:

1. Read the `.sipicache` binary index file if present. This file maps canonical IIIF URLs to cached files and stores metadata (image dimensions, tile sizes, compression levels, access times).
2. Reconcile the index with the filesystem: delete orphan files on disk not in the index, remove index entries for files no longer on disk.
3. Count all indexed files toward `cache_size` and `cache_nfiles` limits.
4. If the existing cache exceeds limits, evict files using LRU (least recently accessed) until within the low-water mark (80% of limit).
5. Begin serving requests.

**Crash recovery**: If the `.sipicache` file is missing (e.g., after a crash or kill -9), delete all cache files in the directory and start with an empty cache. The cache will warm up naturally. This is acceptable because caching is a performance optimization, not a correctness requirement.

### F2: LRU Eviction with High-Water/Low-Water Marks

Replace the broken hysteresis-based eviction:

- **High-water mark** (eviction trigger): 100% of configured limit — eviction triggers when `cache_size` or `cache_nfiles` is reached
- **Low-water mark** (eviction target): 80% of configured limit — eviction continues until cache is at or below 80%
- **Eviction order**: Least recently accessed first (by `access_time`, updated on every cache hit)
- Files currently being served (`blocked_files`) are skipped during eviction
- This applies both at startup (clearing excess from prior runs) and during normal operation

### F3: Correct Default Values

Set code defaults to:
- `cache_dir` default: `./cache` (relative to SIPI working directory)
- `cache_size` default: `200M` (200 megabytes)
- `cache_nfiles` default: `200` files

If `cache_dir` does not exist, SIPI creates it automatically.

### F4: Clear Configuration Semantics

#### `cache_size` (authoritative master switch)

| Value | Meaning |
|-------|---------|
| `-1` | Unlimited cache size |
| `0` | **Caching disabled entirely** — no files written, regardless of other settings |
| `> 0` | Enforced size limit (supports `M` and `G` suffixes) |

`cache_size = 0` is authoritative: when disabled, no cache files are written regardless of `cache_nfiles` value. `cache_size = -1` means no suffix parsing (just the integer).

#### `cache_nfiles` (secondary constraint)

| Value | Meaning |
|-------|---------|
| `0` | No file count limit (only `cache_size` is enforced) |
| `> 0` | Enforced file count limit |

Note: `cache_nfiles` does **not** use `-1`. It uses simpler semantics: `0` = no limit, `>0` = limit. The `cache_size` setting is the only one that can disable caching entirely.

#### Invalid values

Any value < `-1` for `cache_size` or < `0` for `cache_nfiles` produces a clear error at startup. Non-numeric strings produce a clear error. `cache_size = '0M'` is treated as `0` (disabled).

### F5: Configuration Rename (with Grace Period)

Rename all cache config keys to consistent `cache_*` snake_case:

| Old Key | New Key |
|---------|---------|
| `cachedir` | `cache_dir` |
| `cachesize` | `cache_size` |
| `cache_hysteresis` | _(removed entirely)_ |
| `cache_nfiles` | `cache_nfiles` _(unchanged)_ |

**Grace period (one release)**: Old keys `cachedir` and `cachesize` are accepted with a deprecation warning at startup directing the operator to the new key names. After one release, old keys produce a hard error.

`cache_hysteresis` is removed immediately with a warning if present (it is replaced by the built-in 80% low-water mark).

Deprecated keys and their removal timeline are tracked in `DEPRECATIONS.md` at the repo root and noted in `CLAUDE.md`.

### F6: Oversized File Handling

If a single converted image file exceeds `cache_size`, skip caching for that file and serve it directly. Log a warning indicating the file size and configured limit so operators can adjust `cache_size` if needed.

### F7: Prometheus Metrics Endpoint

Add a `/metrics` HTTP endpoint serving Prometheus text format with the following cache metrics:

**Counters:**
- `sipi_cache_hits_total` — requests served from cache
- `sipi_cache_misses_total` — requests that required processing from source
- `sipi_cache_evictions_total` — files evicted
- `sipi_cache_skips_total` — files too large to cache

**Gauges:**
- `sipi_cache_size_bytes` — current cache size in bytes
- `sipi_cache_files` — current number of cached files
- `sipi_cache_size_limit_bytes` — configured size limit (`-1` if unlimited, `0` if disabled)
- `sipi_cache_files_limit` — configured file count limit (`0` if no limit)

**Security consideration**: The `/metrics` endpoint exposes internal operational data and must not be publicly accessible. This is expected to be handled at the infrastructure level (reverse proxy blocking, network policy, or binding to an internal port). See ops-deploy Repository Impact section. Reference: INFRA-637/640/641/642 flagged exposed metrics on other services as a security concern.

### F8: Documentation Update

Update sipi.io docs and config examples to accurately reflect:
- New config key names (`cache_dir`, `cache_size`, `cache_nfiles`)
- Default values (`./cache`, `200M`, `200`)
- The `cache_size` semantics (`-1` unlimited, `0` disabled, `>0` limit)
- The `cache_nfiles` semantics (`0` no limit, `>0` limit)
- Cache behavior across restarts (indexed from `.sipicache`, crash = cold start)
- LRU eviction with 100%/80% high-water/low-water marks
- Oversized file handling
- Prometheus metrics endpoint and available metrics

## User Stories

**US1**: As an operator, I want SIPI to respect cache limits across restarts so that disk space usage is predictable and I don't get paged for full disks.

**US2**: As an operator, I want sensible defaults (200MB / 200 files, auto-created cache directory) so that SIPI behaves safely out of the box without explicit cache configuration.

**US3**: As an operator, I want clear configuration semantics so that I can confidently configure the cache without guessing what values mean.

**US4**: As an operator, I want SIPI to cache on local disk with reliable limits so that startup is fast and crash recovery is quick (removing the need for NFS-mounted cache).

**US5**: As an operator, I want Prometheus metrics for cache utilization and hit rates so that I can monitor cache health and tune configuration.

## Acceptance Criteria

### AC1: Startup Indexing

- On startup with a non-empty cache directory and valid `.sipicache` index, SIPI loads the index, reconciles with disk, and counts all files toward limits
- If existing cache exceeds limits, LRU eviction runs down to 80% before serving begins
- After 10 restarts with continuous load, cache size never exceeds configured limit
- After a kill -9 and restart (missing `.sipicache`), cache directory is cleared and SIPI starts with an empty cache

### AC2: Eviction

- During normal operation, when cache reaches 100% of `cache_size` or `cache_nfiles`, LRU eviction runs down to 80% of the triggered limit
- Files currently being served are skipped during eviction
- Eviction is triggered before writing a new cache entry, not asynchronously

### AC3: Defaults

- With no explicit cache config, `cache_dir` defaults to `./cache`, `cache_size` defaults to `200M`, and `cache_nfiles` defaults to `200`
- If `cache_dir` does not exist, it is created automatically
- Existing configs with explicit values are unaffected

### AC4: Configuration Semantics

- `cache_size = -1` → unlimited size; `cache_nfiles` still enforced if set
- `cache_size = 0` → caching disabled entirely — no files written regardless of `cache_nfiles` value
- `cache_size > 0` → enforced size limit
- `cache_nfiles = 0` → no file count limit
- `cache_nfiles > 0` → enforced file count limit
- Invalid values produce a clear error at startup with guidance on valid values

### AC5: Grace Period for Renamed Keys

- Old config keys `cachedir` and `cachesize` are accepted with a deprecation warning at startup
- Warning message directs operator to the new key name
- `cache_hysteresis` produces a warning that it is no longer supported
- `DEPRECATIONS.md` tracks all deprecated keys and their removal timeline

### AC6: Oversized Files

- A single file larger than `cache_size` is served directly without caching
- A warning is logged with the file size and configured limit

### AC7: Prometheus Metrics

- `/metrics` endpoint returns Prometheus text format with all specified counters and gauges
- Metrics are updated in real-time as cache operations occur
- Endpoint is accessible only from internal network (infrastructure responsibility)

### AC8: Documentation

- sipi.io cache configuration docs reflect all new config keys, semantics, defaults, and behavior
- Config example files include comments explaining the configuration conventions
- `DEPRECATIONS.md` exists in the repo root tracking deprecated keys

## Constraints

- **Startup time**: Cache indexing on startup must complete in under 5 seconds for up to 10,000 cached files on local filesystem. Worst case (indexing + evicting 9,800 of 10,000 files) must complete in under 10 seconds.
- **No external dependencies**: Solution must not introduce new dependencies for caching. A lightweight Prometheus client library is acceptable for the metrics endpoint (or hand-rolled text format output).
- **Config format**: Must remain compatible with the existing Lua-based configuration.
- **Concurrency**: Cache operations must remain thread-safe. The existing mutex-based locking and `blocked_files` mechanism for files being served must be preserved or improved.

## Out of Scope

- Cache sharing across multiple SIPI instances
- Cache warming / pre-population
- NFS mount removal in ops-deploy (separate infra issue — see Repository Impact)
- Changes to dsp-api
- Grafana dashboards or alerting rules (infra team responsibility)
- Metrics endpoint security (reverse proxy / network policy — infra team responsibility)

## Success Criteria

- Cache size on production stays within configured limits over a 30-day period including multiple restarts/deployments
- SIPI startup time on local disk under 30 seconds (currently 3+ minutes on NFS)
- No `/var` disk pressure incidents related to SIPI cache
- Cache hit rate and utilization visible via Prometheus metrics
- INFRA-1058 (Observe SIPI cache) can be closed based on metrics data

## Repository Impact

### sipi

Primary changes:
- **Cache management** (`SipiCache.cpp`, `SipiCache.h`): Startup indexing with `.sipicache` reconciliation, LRU eviction with 80% low-water mark, oversized file handling, crash recovery (clear cache if index missing)
- **Configuration** (`SipiConf.cpp`, `SipiConf.h`): `cache_dir`/`cache_size` renames with grace period, new semantics (`-1`/`0`/`>0` for size, `0`/`>0` for nfiles), new defaults, validation
- **HTTP server** (`SipiHttpServer.cpp`): `/metrics` Prometheus endpoint
- **Config files**: Update all example/test/prod configs to use new key names
- **Documentation**: Update sipi.io cache configuration page
- **New files**: `DEPRECATIONS.md` at repo root, update `CLAUDE.md` to reference it

### ops-deploy (Infra team — separate Linear issue)

Once the SIPI cache fix is deployed and verified:

1. **`roles/dsp-deploy/templates/iiif/conf/sipi.prod-config.lua.j2`**:
   - Rename `cachedir` → `cache_dir`, `cachesize` → `cache_size`
   - Remove `cache_hysteresis`
   - Review cache size value (currently `1000M`)

2. **`roles/dsp-deploy/templates/docker-compose-iiif.yml.j2`**:
   - Mount `/sipi/cache` to **local disk** instead of NFS volume
   - Keep `/tmp` on NFS if needed for other purposes
   - This eliminates the 3+ minute startup penalty

3. **Health check timeouts**: Can be tightened back to normal once cache is on local disk

4. **Metrics endpoint security**: Ensure `/metrics` is not publicly accessible (reverse proxy rule or network policy). See INFRA-637/640/641/642 for precedent on exposed metrics.

**Deployment note**: During the grace period, the new SIPI binary accepts both old and new config key names. This means ops-deploy config changes can be deployed independently of the SIPI binary update — no atomic deployment required. After the grace period ends (next major release), the ops-deploy config **must** use the new key names.

## Follow-Up Work

### RED Metrics for SIPI (separate Linear issue)

The `/metrics` endpoint introduced by this PRD covers cache-specific metrics only. A follow-up issue should be created to add **RED metrics** (Rate, Errors, Duration) for SIPI's core IIIF request handling:

- **Rate**: `sipi_requests_total` — total IIIF requests, labeled by endpoint/method
- **Errors**: `sipi_request_errors_total` — failed requests, labeled by error type (4xx, 5xx, timeout)
- **Duration**: `sipi_request_duration_seconds` — histogram of request processing time, labeled by endpoint

These are the minimum metrics needed for meaningful service-level monitoring and alerting. The `/metrics` endpoint from F7 provides the infrastructure; RED metrics populate it with request-level observability.

Additional metrics to consider in that follow-up:
- `sipi_image_processing_duration_seconds` — histogram of image conversion time (separate from total request time)
- `sipi_request_size_bytes` — response size distribution

## Open Questions

_None — all resolved during discovery._
