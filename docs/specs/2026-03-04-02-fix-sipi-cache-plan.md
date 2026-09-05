---
title: "fix: SIPI cache management — startup indexing, LRU eviction, correct defaults"
type: fix
date: 2026-03-07
author: "subotic"
status: implemented
repositories:
  - name: sipi
linear:
  - DEV-5610
  - INFRA-940
  - INFRA-955
  - INFRA-996
  - INFRA-1058
---

# fix: SIPI cache management — startup indexing, LRU eviction, correct defaults

## Overview

Fix SIPI's broken cache management so that cache limits are reliably enforced across restarts, eviction works correctly, defaults match documentation, configuration semantics are unambiguous, and cache health is observable via Prometheus metrics. This enables removing the NFS-mounted cache directory (which causes 3+ minute startup times) and returning to fast local-disk caching.

## Problem Statement / Motivation

SIPI's file-based image cache has multiple compounding bugs:

1. **Eviction is broken**: The hysteresis calculation (`SipiCache.cpp:273`) computes the eviction target as `max_cachesize * cache_hysteresis` (e.g., `200MB * 0.15 = 30MB`), purging down to 15% of max instead of down to 85%. This effectively wipes the entire cache on every eviction trigger.
2. **Restarts lose state**: Although `.sipicache` persists the cache index, the startup code has a tile_w bug (`SipiCache.cpp:100`: `cr.tile_w = fr.img_w` instead of `fr.tile_w`), and the default config values (`cachesize="0"`, `cache_nfiles=0`) mean the Lua-config path silently disables limits.
3. **Defaults are wrong**: Lua config defaults are `cachesize="0"` and `cache_nfiles=0` (unlimited), while CLI defaults and documentation say `200M`/`200`. Production ran with `cachesize='0M'` which the code treated as unlimited.
4. **No observability**: No metrics endpoint exists. A TODO comment at `sipi.cpp:1317` is all that exists.
5. **Verbose startup logging**: Per-file INFO-level logging (`SipiCache.cpp:112,137`) produces thousands of log lines on startup (flagged in [PR #11 review](https://github.com/dasch-swiss/dasch-specs/pull/11#pullrequestreview-3895111576) by @SamuelBoerlin).

The NFS workaround (INFRA-940) moved the cache to a network share, but this causes 3+ minute startup and degrades crash recovery. Fixing the cache properly allows returning to local disk.

## Proposed Solution

Eight implementation tasks mapped to the PRD features (F1-F8), plus pre-existing bug fixes discovered during research. The work is structured in four phases: configuration fixes first (unblocks testing), then cache logic, then metrics, then documentation.

## Technical Considerations

### Type representation for `cache_size = -1` (unlimited)

The PRD specifies `cache_size`: `-1` = unlimited, `0` = disabled, `> 0` = limit. The current type chain is inconsistent:
- `SipiConf.h:41` — `size_t` (unsigned, cannot represent -1)
- `SipiHttpServer.cpp:1719` — `size_t` parameter
- `SipiCache.h:135` — `long long` parameter
- `SipiCache.h:115` — `unsigned long long` member

**Decision**: Change `cache_size` to `long long` (signed 64-bit) throughout the chain. `-1` is naturally representable. `0` = disabled. `> 0` = enforced limit. This is a minimal change that avoids adding new types or enums. All call sites (`SipiConf`, `SipiHttpServer::cache()`, `SipiCache` constructor and member) must be updated.

`cache_nfiles` stays as `unsigned` / `size_t` since its semantics are simpler (`0` = no limit, `> 0` = limit).

### Dual-limit eviction semantics

When both `cache_size` and `cache_nfiles` exist, eviction triggers when **either** limit is reached (100% high-water). Eviction continues until **both** limits are at or below 80% (low-water). This prevents a scenario where one limit stays at 100% after eviction. The current code (`SipiCache.cpp:290-291`) stops when **either** is below the goal — this must change to stop only when **both** are below.

### Oversized file handling

File size is unknown before image conversion (cache file is opened before processing at `SipiHttpServer.cpp:1549`). Two-stage approach:
1. **Pre-check heuristic**: If `cache_size > 0` and the source file size on disk exceeds `cache_size`, skip opening a cache file entirely (source is always smaller or comparable to output for typical IIIF operations).
2. **Post-write check**: After closing the cache file, if its size exceeds `cache_size`, unlink it and don't call `cache->add()`. Log a warning. Increment `sipi_cache_skips_total`.

### Binary format robustness

The `.sipicache` format has no version header and uses platform-dependent struct sizes. Rather than adding a version header (which would complicate the implementation), treat any `.sipicache` that doesn't divide evenly into `sizeof(FileCacheRecord)` records as corrupted, triggering crash recovery (clear cache, start fresh). This is safe because the cache is a performance optimization, not a correctness requirement.

### CLI/env var rename scope

The PRD addresses Lua config key renames. For consistency, CLI arguments and environment variables should also be renamed in the same grace period:
- `--cachedir` → `--cache-dir`, `--cachesize` → `--cache-size`, `--cachenfiles` → `--cache-nfiles`
- `SIPI_CACHEDIR` → `SIPI_CACHE_DIR`, `SIPI_CACHESIZE` → `SIPI_CACHE_SIZE`, `SIPI_CACHENFILES` → `SIPI_CACHE_NFILES`
- `--cachehysteresis` / `SIPI_CACHEHYSTERESIS` — removed with warning

Old names accepted with deprecation warning during grace period. If both old and new key appear in the same Lua config, emit an error and refuse to start (ambiguous configuration).

### Metrics endpoint

Use **[prometheus-cpp](https://github.com/jupp0r/prometheus-cpp)** (core only, v1.3.0+) — the well-established C++ Prometheus client library (1,100+ stars, MIT license). Build only the `core` component (registry, metric types, text serializer) with `ENABLE_PULL=OFF` and `ENABLE_PUSH=OFF` to avoid pulling in CivetWeb or libcurl. The core library's only dependency is pthreads, which SIPI already links.

The library provides thread-safe Counter, Gauge, Histogram, and Summary types with a builder API, plus a `TextSerializer` that writes Prometheus exposition format to any `std::ostream`. This integrates cleanly with SIPI's shttps server for the `/metrics` endpoint and sets up properly for future RED metrics (request duration histograms, etc.).

Add prometheus-cpp as a vendored dependency in `ext/prometheus-cpp/` following the existing ExternalProject pattern (like sentry-native). The existing `/api/cache` Lua endpoint is left unchanged (it serves a different purpose — operational inspection and manual purge).

### Thread safety fixes (pre-existing)

- `SipiCache::getSize()` (`SipiCache.cpp:519`) reads `sizetable` without holding the lock — add lock acquisition.
- `SipiHttpServer.cpp:1455` calls `deblock("")` on cache miss, polluting `blocked_files` — guard with `if (!cachefile.empty())`.

## Implementation Approach

### Phase 1: Configuration (F3, F4, F5)

#### 1.1 Fix type chain and naming for `cache_size`

Change `cache_size` from `size_t` to `long long` and rename internal members for clarity:
- `SipiConf.h:41` — member: `size_t cache_size` → `long long cache_size`
- `SipiConf.h` — getter/setter signatures: update to `long long`
- `SipiConf.cpp` — parsing logic: update to `long long`
- `SipiHttpServer.cpp:1719` / `SipiHttpServer.hpp` — `cache()` method: `size_t max_cachesize_p` → `long long max_cache_size`
- `SipiCache.h:115` — limit member: rename `unsigned long long max_cachesize` → `long long max_cache_size`
- `SipiCache.h:114` — current-total member: rename `unsigned long long cachesize` → `unsigned long long cache_used_bytes` (avoids confusion with config `cache_size`)
- `SipiCache.h:135` — constructor parameter: already `long long`, keep

**Naming convention**: `max_cache_size` = configured limit, `cache_used_bytes` = current total bytes in cache. All code examples in this plan use these names.

#### 1.2 Implement new config parsing semantics

In `SipiConf.cpp`, rewrite cache config parsing:

```cpp
// cache_size: string -> long long
// "-1" -> -1 (unlimited)
// "0" or "0M" -> 0 (disabled)
// "200M" -> 209715200, "1G" -> 1073741824
// Anything else -> startup error with guidance

// cache_nfiles: int
// 0 -> no file count limit
// > 0 -> enforced limit
// < 0 -> startup error

// cache_dir: string
// "" -> "./cache" (default)
// Any path -> used as-is, auto-created if missing
```

#### 1.3 Fix defaults in Lua config parser

In `SipiConf.cpp:35-54`, change Lua defaults to match documentation:
- `cachesize` default: `"200M"` (was `"0"`)
- `cachedir` default: `"./cache"` (was `""`)
- `cache_nfiles` default: `200` (was `0`)
- Remove `cache_hysteresis` from parsing (was `0.1`)

#### 1.4 Config key rename with grace period

In `SipiConf.cpp`, when parsing Lua config:
1. Check for new keys first (`cache_dir`, `cache_size`)
2. If not found, check for old keys (`cachedir`, `cachesize`)
3. If old key found, use its value but emit deprecation warning: `WARN: Config key 'cachedir' is deprecated. Use 'cache_dir' instead.`
4. If both old and new key present for the same setting, emit error and exit: `ERROR: Both 'cachedir' and 'cache_dir' specified. Remove the deprecated 'cachedir' key.`
5. If `cache_hysteresis` present, emit warning: `WARN: Config key 'cache_hysteresis' is no longer supported (replaced by built-in 80% low-water mark). Remove it from your config.`

In `sipi.cpp`, apply same pattern for CLI args and env vars.

#### 1.5 Validate configuration at startup

After parsing, validate:
- `cache_size < -1` → error with guidance
- `cache_nfiles < 0` → error with guidance. Since `cache_nfiles` is `unsigned`, this check must happen **at string-parsing time** (before conversion): if the Lua/CLI/env string starts with `-`, reject immediately with a clear error message.
- `cache_size = 0` → log `INFO: Caching is disabled (cache_size = 0)`
- `cache_size = -1` → log `INFO: Cache size is unlimited`
- Auto-create `cache_dir` if it doesn't exist; if creation fails, error and exit

#### 1.6 Create `DEPRECATIONS.md`

At repo root:

```markdown
# Deprecations

## Cache Configuration Keys (deprecated in {release_version}, removal in next major)

| Old Key | New Key | Notes |
|---------|---------|-------|
| `cachedir` (Lua) / `--cachedir` (CLI) / `SIPI_CACHEDIR` (env) | `cache_dir` / `--cache-dir` / `SIPI_CACHE_DIR` | |
| `cachesize` (Lua) / `--cachesize` (CLI) / `SIPI_CACHESIZE` (env) | `cache_size` / `--cache-size` / `SIPI_CACHE_SIZE` | |
| `cache_hysteresis` (Lua) / `--cachehysteresis` (CLI) / `SIPI_CACHEHYSTERESIS` (env) | _(removed)_ | Replaced by built-in 80% low-water mark |
```

#### 1.7 Update config files

Update all Lua config files to use new key names:
- `config/sipi.config.lua`
- `config/sipi.test-config.lua`
- `config/sipi.debug-config.lua`
- `test/_test_data/config/sipi.fake-knora-test-config.lua`

Remove `cache_hysteresis` from all configs.

#### 1.8 Update configuration unit tests

In `test/unit/configuration/configuration.cpp`:
- Update expected key names
- Add tests for deprecated key acceptance with warning
- Add tests for invalid values (negative, non-numeric)
- Add test for `cache_size = -1` (unlimited) and `cache_size = 0` (disabled)
- Add test for both old and new key present (should error)

### Phase 2: Cache Logic (F1, F2, F6)

#### 2.1 Fix tile_w bug

In `SipiCache.cpp:100`, change `cr.tile_w = fr.img_w;` to `cr.tile_w = fr.tile_w;`.

#### 2.2 Rewrite startup indexing

In `SipiCache` constructor (`SipiCache.cpp:50-161`):

**Note**: The `max_cache_size == 0` (disabled) check happens in `sipi.cpp` _before_ constructing SipiCache (see Phase 2.4). The constructor is only called when caching is enabled.

1. Check if `.sipicache` exists:
   - If missing → clear all files in `cache_dir`, log summary, start empty.
   - If present but size not divisible by `sizeof(FileCacheRecord)` → treat as corrupted, clear cache, log warning.
   - If present and valid → read all records.
2. For each record: verify cache file exists on disk. If not, skip (don't add to `cachetable`). Count skipped entries.
3. Scan `cache_dir` for files not in the loaded index → delete them. Count deleted orphans.
4. Sum `cache_used_bytes` and `nfiles` from loaded records.
5. Populate `sizetable` from `cachetable` (image dimensions, tile sizes — needed by `getSize()` for subsequent requests).
6. If over limits, run LRU eviction down to 80%.
7. Log a **single summary line** at INFO level:
   ```
   Cache loaded: %d files (%.1f MB), %d orphans removed, %d evicted to 80%% target
   ```
8. Per-file operations logged at **DEBUG** level only (addresses PR #11 feedback).

#### 2.3 Rewrite LRU eviction (`purge()`)

Replace `SipiCache.cpp:256-296`:

```cpp
int SipiCache::purge(bool use_lock) {
    // Note: max_cache_size == 0 (disabled) is handled upstream — SipiCache
    // is never constructed when caching is disabled.

    bool size_over = (max_cache_size > 0)
        && (cache_used_bytes >= (unsigned long long)max_cache_size);
    bool nfiles_over = (max_nfiles > 0) && (nfiles >= max_nfiles);

    if (!size_over && !nfiles_over) return 0;

    // Low-water marks: 80% of configured limits
    unsigned long long size_low = (max_cache_size > 0)
        ? (unsigned long long)(max_cache_size * 0.8) : 0;
    unsigned nfiles_low = (max_nfiles > 0)
        ? (unsigned)(max_nfiles * 0.8) : 0;

    // Sort by access_time ascending (oldest first)
    std::vector<std::pair<std::string, CacheRecord>> entries(
        cachetable.begin(), cachetable.end());
    std::sort(entries.begin(), entries.end(),
        [](const auto &a, const auto &b) {
            return a.second.access_time < b.second.access_time;
        });

    int evicted = 0;
    bool size_ok = false;
    bool nfiles_ok = false;

    for (const auto &[key, record] : entries) {
        // Check if BOTH limits are at or below low-water
        size_ok = (max_cache_size <= 0) || (cache_used_bytes <= size_low);
        nfiles_ok = (max_nfiles == 0) || (nfiles <= nfiles_low);
        if (size_ok && nfiles_ok) break;

        // Skip blocked files
        if (blocked_files.count(key) && blocked_files[key] > 0) {
            continue;
        }

        ::unlink(record.cachepath.c_str());
        cache_used_bytes -= record.fsize;
        nfiles--;
        cachetable.erase(key);
        evicted++;
    }

    // All-blocked scenario: if we couldn't free enough space, signal to caller
    if (!size_ok || !nfiles_ok) {
        log_warn("Cache full and all remaining files are blocked. "
                 "New file will not be cached.");
        return -1;  // caller should skip caching
    }

    return evicted;
}
```

#### 2.4 Cache disabled mode

When `cache_size == 0`, the cache is disabled entirely:
- In `sipi.cpp:1355-1366`: Don't create `SipiCache` instance. `_cache` remains `nullptr`.
- In the IIIF handler (`SipiHttpServer.cpp`): When `_cache == nullptr`, skip all cache check/add calls. This path already exists in the code.

#### 2.5 Oversized file handling (F6)

In `SipiHttpServer.cpp`, in the `serve_iiif` function:

Add a `getMaxCacheSize()` accessor to `SipiCache` that returns `max_cache_size`. The IIIF handler accesses this via `cache->getMaxCacheSize()` (the `cache` shared_ptr is already available in the handler via `SipiHttpServer`).

**Pre-check** (before opening cache file, around line 1549):
```cpp
bool skip_cache = false;
long long max_cs = cache->getMaxCacheSize();
if (max_cs > 0) {
    struct stat src_stat;
    if (stat(infile.c_str(), &src_stat) == 0 && src_stat.st_size > max_cs) {
        log_warn("Source file %s (%lld bytes) exceeds cache_size (%lld bytes), skipping cache",
                 infile.c_str(), (long long)src_stat.st_size, max_cs);
        skip_cache = true;
    }
}
```

**Post-write check** (after closing cache file, before `cache->add()`):
```cpp
if (!skip_cache && max_cs > 0) {
    struct stat cache_stat;
    if (stat(cachefile.c_str(), &cache_stat) == 0 && cache_stat.st_size > max_cs) {
        log_warn("Converted file %s (%lld bytes) exceeds cache_size (%lld bytes), removing",
                 cachefile.c_str(), (long long)cache_stat.st_size, max_cs);
        ::unlink(cachefile.c_str());
        skip_cache = true;
    }
}
if (!skip_cache) {
    cache->add(...);
}
```

**Note**: Metrics instrumentation (`cache_skips_total.Increment()`) is added in Phase 3.4. The code above shows the final form without metrics calls — Phase 3.4 adds them once `SipiMetrics` exists.

#### 2.6 Fix pre-existing bugs

**Thread safety** — `SipiCache::getSize()` (`SipiCache.cpp:519`): Wrap `sizetable` read in lock guard.

**deblock("") bug** — `SipiHttpServer.cpp:1455`: On cache miss, `deblock("")` is called with an empty string, polluting `blocked_files`. Guard with:
```cpp
if (!cachefile.empty()) {
    cache->deblock(cachefile);
}
```

**Lua bindings** — `SipiLua.cpp:305-313`: Update `cache_methods[]` to use renamed members (`cache_used_bytes` instead of `cachesize`, `max_cache_size` instead of `max_cachesize`).

#### 2.7 Handle purge return value in `add()`

In `SipiCache::add()`, check the return value of `purge()`. If purge returned `-1` (all-blocked), skip adding the new file to the cache. The all-blocked detection is integrated into the `purge()` code (see 2.3).

#### 2.8 Add unit tests for SipiCache

Create `test/unit/cache/` directory with:
- `CMakeLists.txt` — following existing test patterns
- `cache_test.cpp` — GoogleTest tests:
  - **Startup indexing**: Create temp dir with cache files and `.sipicache`, verify correct loading and counting
  - **Crash recovery**: Missing `.sipicache` → cache dir cleared
  - **Corrupted index**: `.sipicache` with wrong size → treated as corrupted
  - **LRU eviction**: Fill cache to limit, verify oldest files evicted first, verify 80% target
  - **Dual-limit eviction**: Both size and nfiles limits, verify both reach 80%
  - **Blocked file skip**: Mark files as blocked, verify they survive eviction
  - **Oversized file**: Verify file > cache_size is not added
  - **Disabled cache**: `cache_size = 0` → no operations
  - **Unlimited cache**: `cache_size = -1` → no eviction triggered

Add `add_subdirectory(cache)` to `test/unit/CMakeLists.txt`.

#### 2.9 Add E2E test for cache persistence across restarts

In `test/e2e/`, add a pytest test that:
1. Starts SIPI with a small `cache_size` (e.g., `5M`) and `cache_nfiles=10`
2. Makes IIIF requests to populate the cache
3. Stops SIPI gracefully (`.sipicache` written)
4. Restarts SIPI
5. Verifies cache was loaded (check `/metrics` for `sipi_cache_files > 0`)
6. Makes more requests to trigger eviction
7. Verifies cache stays within limits (check `/metrics` for `sipi_cache_size_bytes <= 5MB`)
8. Kill -9 SIPI (simulating crash — `.sipicache` not written)
9. Restart SIPI
10. Verify cache started empty (check `/metrics` for `sipi_cache_files == 0`)

### Phase 3: Prometheus Metrics (F7)

**Note**: Steps 3.1 (vendoring) and 3.2 (SipiMetrics skeleton) can start in parallel with Phase 2 since they are independent. Step 3.4 (instrumentation) depends on Phase 2 being complete.

#### 3.1 Add prometheus-cpp dependency

Create `ext/prometheus-cpp/CMakeLists.txt` following the existing ExternalProject pattern (like `ext/sentry/CMakeLists.txt`):

```cmake
include(ExternalProject)

ExternalProject_Add(
    prometheus-cpp
    GIT_REPOSITORY https://github.com/jupp0r/prometheus-cpp.git
    GIT_TAG v1.3.0
    CMAKE_ARGS
        -DENABLE_PULL=OFF
        -DENABLE_PUSH=OFF
        -DENABLE_COMPRESSION=OFF
        -DENABLE_TESTING=OFF
        -DOVERRIDE_CXX_STANDARD_FLAGS=OFF
        -DUSE_THIRDPARTY_LIBRARIES=OFF
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)
```

Link `prometheus-cpp::core` to the sipi target in the top-level `CMakeLists.txt`.

#### 3.2 Create metrics wrapper

Create `include/SipiMetrics.h` and `src/SipiMetrics.cpp`:

```cpp
// SipiMetrics.h
#pragma once
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <memory>

class SipiMetrics {
public:
    static SipiMetrics& instance();

    std::shared_ptr<prometheus::Registry> registry();

    // Counters
    prometheus::Counter& cache_hits_total;
    prometheus::Counter& cache_misses_total;
    prometheus::Counter& cache_evictions_total;
    prometheus::Counter& cache_skips_total;

    // Gauges
    prometheus::Gauge& cache_size_bytes;
    prometheus::Gauge& cache_files;
    prometheus::Gauge& cache_size_limit_bytes;
    prometheus::Gauge& cache_files_limit;

private:
    SipiMetrics();
    std::shared_ptr<prometheus::Registry> registry_;
};
```

The constructor registers all metric families and adds default label sets. The `registry()` accessor is used by the `/metrics` handler for serialization.

#### 3.3 Register `/metrics` route

In `SipiHttpServer::run()` (after existing routes):

```cpp
add_route(Connection::GET, "/metrics", metrics_handler);
```

The `metrics_handler` function uses prometheus-cpp's `TextSerializer`:

```cpp
void metrics_handler(Connection &conn, LuaServer &lua, void *handler_data, void *) {
    auto& metrics = SipiMetrics::instance();
    auto collected = metrics.registry()->Collect();
    prometheus::TextSerializer serializer;
    std::ostringstream oss;
    serializer.Serialize(oss, collected);
    // Write oss.str() to connection with Content-Type: text/plain; version=0.0.4; charset=utf-8
}
```

#### 3.4 Instrument cache operations

Add metric updates at each cache operation point:
- `SipiCache::check()` — on hit: `cache_hits_total.Increment()`; on miss: `cache_misses_total.Increment()`
- `SipiCache::purge()` — after the eviction loop: `cache_evictions_total.Increment(evicted)` (batch increment with total count)
- `SipiCache::add()` — update `cache_size_bytes.Set(cache_used_bytes)` and `cache_files.Set(nfiles)`
- Oversized skip (IIIF handler, step 2.5) — add `cache_skips_total.Increment()` at both the pre-check and post-write check skip paths
- After eviction — update `cache_size_bytes` and `cache_files` gauges
- On startup — set `cache_size_limit_bytes` and `cache_files_limit` from config

All prometheus-cpp metric types are thread-safe by design — no additional locking needed beyond what the library provides.

#### 3.5 Update build configuration

- Add `ext/prometheus-cpp` to the top-level `CMakeLists.txt` (ExternalProject or `add_subdirectory`)
- Add `SipiMetrics.cpp` and `SipiMetrics.h` to the `add_executable(sipi ...)` source list
- Link `prometheus-cpp::core` to the sipi target

**Zig build**: The Zig toolchain builds all `ext/` dependencies from source via CMake. prometheus-cpp core (14 source files, pthreads only) should compile cleanly with Zig's clang-compatible frontend. Add it to the Zig build's CMake invocation with the same flags as 3.1. Verify with `make zig-build-local`.

**Nix build**: Add prometheus-cpp to `flake.nix` as a build input (either as a Nix package if available in nixpkgs, or as a source fetch built via CMake in the derivation). Verify with `make nix-build`.

**Docker build**: Should work automatically since the Docker build uses CMake. Verify with `make docker-build`.

#### 2.10 Add startup banner

In `sipi.cpp`, just before the `server.run()` call (around line 1389), add a startup log line at INFO level:

```cpp
log_info("Claude was here. SIPI server starting on port %d...", sipiConf.getPort());
```

This serves as an operational anchor — infra uses the first log line to confirm SIPI has started and to locate logs after crashes. The previous banner ("ivan was here") was removed at some point; this restores the pattern.

### Phase 4: Documentation (F8)

#### 4.1 Update sipi.io docs

Update `docs/src/guide/sipi.md` and `docs/src/guide/running.md`:
- New config key names (`cache_dir`, `cache_size`, `cache_nfiles`)
- Default values (`./cache`, `200M`, `200`)
- `cache_size` semantics (`-1` unlimited, `0` disabled, `>0` limit)
- `cache_nfiles` semantics (`0` no limit, `>0` limit)
- Cache behavior across restarts
- LRU eviction with 100%/80% high-water/low-water marks
- Oversized file handling
- Prometheus `/metrics` endpoint and available metrics

#### 4.2 Update CLAUDE.md

Add reference to `DEPRECATIONS.md` and note the new cache configuration defaults and semantics.

#### 4.3 Update config example comments

Add inline comments to `config/sipi.config.lua` explaining each cache key and valid values.

## Acceptance Criteria

- [x] **AC1 Startup Indexing**: After 10 restarts with continuous load, cache size never exceeds configured limit
- [x] **AC1 Crash Recovery**: After kill -9 and restart (missing `.sipicache`), cache directory is cleared and SIPI starts fresh
- [x] **AC1 Corrupted Index**: `.sipicache` with invalid size treated as corrupted → crash recovery path
- [x] **AC2 LRU Eviction**: When cache reaches 100% of either limit, eviction runs down to 80% of both limits
- [x] **AC2 Blocked Files**: Files currently being served are skipped during eviction
- [x] **AC2 All-Blocked**: When all files are blocked and cache is full, new file is not cached (warning logged)
- [x] **AC3 Defaults**: No explicit config → `cache_dir=./cache`, `cache_size=200M`, `cache_nfiles=200`
- [x] **AC3 Auto-Create**: Missing `cache_dir` is created automatically
- [x] **AC4 Semantics**: `cache_size=-1` → unlimited; `cache_size=0` → disabled; `cache_size>0` → enforced
- [x] **AC4 Validation**: Invalid values produce clear startup error with guidance
- [x] **AC5 Grace Period**: Old config keys accepted with deprecation warning
- [x] **AC5 Conflict**: Both old and new key in same config → error
- [x] **AC5 Hysteresis Removal**: `cache_hysteresis` produces warning that it's no longer supported
- [x] **AC6 Oversized**: File larger than `cache_size` served directly, not cached, warning logged (post-write check only; pre-check removed per code review — source size is a poor predictor of output size)
- [x] **AC7 Metrics**: `/metrics` returns Prometheus text format with all specified counters and gauges
- [x] **AC7 Thread Safety**: Metrics counters are atomically updated from multiple request threads
- [x] **AC8 Docs**: sipi.io cache docs reflect new config, defaults, semantics, and metrics
- [x] **AC8 DEPRECATIONS.md**: Exists at repo root tracking deprecated keys and removal timeline
- [x] **Startup Logging**: Per-file cache logs are DEBUG level; single summary line at INFO level
- [x] **Bug Fixes**: tile_w assignment fixed, deblock("") guard added, getSize() lock added

## Dependencies & Risks

**Dependencies:**
- **prometheus-cpp v1.3.0+** (core only) — vendored in `ext/prometheus-cpp/`, MIT license. Only dependency is pthreads (already linked). ~50-150 KB static library.
- ops-deploy changes (config rename, NFS removal) are a follow-up by the infra team after this ships.

**Risks:**
- **Config migration**: Operators with custom Lua configs need to update key names within the grace period. Mitigated by deprecation warnings and `DEPRECATIONS.md`.
- **Cache loss on upgrade**: If `FileCacheRecord` struct changes size (unlikely — we're not adding fields), existing `.sipicache` files will be treated as corrupted and the cache starts cold. This is acceptable behavior.
- **Metrics endpoint security**: `/metrics` is on the same port as IIIF. Must be blocked at the reverse proxy level (ops-deploy responsibility, documented in PRD).
- **Production `cache_nfiles`**: With new defaults, Lua configs without explicit `cache_nfiles` will get `200`. Production's 1GB cache may need more than 200 files — infra team should review when updating ops-deploy config.

## Success Metrics

- Cache size on production stays within configured limits over 30 days including multiple restarts
- SIPI startup time on local disk under 30 seconds (currently 3+ minutes on NFS)
- No `/var` disk pressure incidents related to SIPI cache
- Cache hit rate visible via `sipi_cache_hits_total` / (`sipi_cache_hits_total` + `sipi_cache_misses_total`)
- Startup logs reduced from thousands of lines to a single summary line
- INFRA-1058 closeable based on metrics data

## Reviewer Guidelines

Use the general review checklist at [`docs/src/development/reviewer-guidelines.md`](https://github.com/dasch-swiss/sipi/blob/main/docs/src/development/reviewer-guidelines.md). In addition, verify these cache-specific items:

### Cache-Specific Checklist

- [ ] `DEPRECATIONS.md` exists at repo root with removal timeline for `cachedir`, `cachesize`, `cache_hysteresis`
- [ ] Startup indexing counts all existing files toward limits **before** serving begins
- [ ] LRU eviction targets 80% of **both** limits (not just the triggering one)
- [ ] `blocked_files` are skipped during eviction — never delete a file being served
- [ ] Oversized files: pre-check (source size) and post-write check (output size) both present
- [ ] Crash recovery: missing or corrupted `.sipicache` → clear cache dir, start fresh
- [ ] tile_w bug fix verified (`cr.tile_w = fr.tile_w`, not `fr.img_w`)
- [ ] `long long` type used consistently for `cache_size` through entire call chain (`SipiConf` → `SipiHttpServer` → `SipiCache`)
- [ ] `deblock("")` guard: only call `deblock()` when `cachefile` is non-empty
- [ ] `getSize()` lock fix: `sizetable` read protected by mutex
- [ ] prometheus-cpp linked as core-only (`ENABLE_PULL=OFF`, `ENABLE_PUSH=OFF`)
- [ ] All PRD-specified counters and gauges present with correct Prometheus names and types
- [ ] Grace period: old keys accepted with warning; both old+new in same config → hard error
- [ ] `cache_hysteresis` removal: warning emitted if present, no silent ignore

## References & Research

- PRD: `dasch-specs/specs/2026-03-04-sipi-cache-management/01-sipi-cache-PRD.md`
- PR #11 review (SamuelBoerlin): reduce startup cache logging — [comment](https://github.com/dasch-swiss/dasch-specs/pull/11#pullrequestreview-3895111576)
- Broken hysteresis: `sipi/src/SipiCache.cpp:273` — goal is `max * hysteresis` not `max * (1 - hysteresis)`
- Tile_w bug: `sipi/src/SipiCache.cpp:100` — `cr.tile_w = fr.img_w` should be `fr.tile_w`
- Default mismatch: `sipi/src/SipiConf.cpp:35-54` (Lua defaults) vs `sipi/src/sipi.cpp:596-610` (CLI defaults)
- Verbose logging: `sipi/src/SipiCache.cpp:112,137` — per-file INFO-level logs
- Config types: `sipi/include/SipiConf.h:41` — `cache_size` is `size_t` (unsigned)
- Route registration: `sipi/src/SipiHttpServer.cpp:1744-1746` — pattern for adding `/metrics`
- Existing tests: `sipi/test/unit/configuration/configuration.cpp` — config parsing tests
- Institutional learning: HashMap ordering (`dasch-specs/learnings/logic-errors/hashmap-loses-insertion-order-use-listmap.md`) — keep `unordered_map` + on-demand sort for LRU
- Institutional learning: config tribal knowledge (`dasch-specs/learnings/configuration-errors/fuseki-remote-dev-db-config-recipe.md`) — document config semantics clearly
