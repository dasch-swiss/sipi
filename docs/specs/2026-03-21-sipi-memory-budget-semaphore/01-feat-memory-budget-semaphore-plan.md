---
title: "feat: Memory budget semaphore for concurrent image decode throttling"
type: feat
status: draft
linear: DEV-6060
repositories:
  - name: sipi
  - name: ops-deploy
---

# Memory Budget Semaphore — Implementation Plan

Fixes DEV-6060

## Motivation

The thread pool controls CPU concurrency but has no awareness of memory. 8 concurrent full-resolution JP2 decodes (e.g., 20000x30000 pixels) can each consume 1-2GB, exhausting a 4GB container. The per-request pixel limit (R20) caps individual requests and the per-client rate limiter throttles per-client throughput, but neither prevents aggregate memory exhaustion from multiple legitimate clients requesting large images simultaneously.

## Technical Considerations

### Memory Estimation — Precise from Header

`SipiImgInfo` (from `getDim()` / cache lookup) currently returns only `{width, height}`. However, all four format-specific `getDim()` implementations already open the file header and have access to channels and bit depth — they just don't extract them today:

| Format | nc (channels) | bps (bits/sample) | API |
|--------|--------------|-------------------|-----|
| JP2 | `codestream.get_num_components()` | `codestream.get_bit_depth(0)` | Kakadu, already opened in `getDim()` |
| TIFF | `TIFFTAG_SAMPLESPERPIXEL` | `TIFFTAG_BITSPERSAMPLE` | libtiff `TIFFGetField`, trivial |
| JPEG | `cinfo.num_components` | `cinfo.data_precision` | libjpeg, from header parse |
| PNG | `png_get_channels()` | `png_get_bit_depth()` | libpng, from header parse |

**Approach:** Extend `SipiImgInfo` with `nc` and `bps` fields, and add 1-2 lines to each format's `getDim()`. This gives exact memory estimation (`width * height * nc * bps/8`) with no guessing. The change is small (~8 lines across 4 files) and eliminates the need for conservative per-format defaults.

### JP2 Reduce Levels and Region-of-Interest — Actual Decode Size

**Critical finding:** For JP2 (the production format), Kakadu does NOT decode the full source image. The `read()` method:

1. Computes the optimal DWT reduce level from the IIIF size parameter (`SipiSize::get_size()` returns the `reduce` value)
2. Computes the region-of-interest (ROI) from the IIIF region parameter
3. Calls `codestream.apply_input_restrictions(reduce, roi)` — tells Kakadu to decode only the ROI at the reduce level
4. Allocates the buffer for the **restricted dimensions** (`dims.area()`), not the full source

This means:
- `/0,0,1024,1024/1024,/0/default.jpg` on a 20000×30000 source: decode buffer = ~1024×1024 × nc × bps/8 = **~4 MB** (not 4.8 GB)
- `/full/,128/0/default.jpg`: decode buffer at reduce level ≈ 128×192 × nc × bps/8 = **~100 KB**
- Only `/full/max/0/default.jpg` decodes the full 20000×30000 source

**The reduce level is already computed in the IIIF handler** (`SipiHttpServer.cpp:1593`) via `size->get_size(img_w, img_h, tmp_r_w, tmp_r_h, tmp_red, tmp_ro)` — the `tmp_red` output IS the reduce level, and `clevels` (max DWT levels) is already in `SipiImgInfo`. So the actual decode dimensions can be computed precisely:

```
decode_w = (region_w > 0 ? region_w : src_w) >> reduce
decode_h = (region_h > 0 ? region_h : src_h) >> reduce
decode_buffer = decode_w × decode_h × nc × bps/8
```

The budget must use these **actual decode dimensions**, not the full source. This is what makes the memory budget accurate for tiles (tiny budget, passes instantly) and correctly large for full-resolution requests.

**Note:** For TIFF (future pyramidal TIFF support), the pattern will be similar — only the relevant pyramid level/tiles are loaded. For non-pyramidal formats (JPEG, PNG), the full source must be decoded.

### Shared Decode Dimension Logic — No Duplication

The reduce level + ROI computation is currently duplicated:
1. `SipiHttpServer.cpp` — calls `size->get_size()` for pixel limit / rate limiter checks
2. `SipiIOJ2k::read()` — calls `region->crop_coords()` + `size->get_size()` to set Kakadu restrictions
3. Future `SipiIOTiff::read()` — will need the same computation for pyramidal levels

Both computations are **pure arithmetic** on dimensions — no file I/O needed. Extract into a shared utility:

```cpp
/// Compute actual decode dimensions given source info + IIIF region/size.
/// Used by: memory budget estimation, JP2 read, pyramidal TIFF read.
struct DecodeDims {
  size_t width;       // decode buffer width (after reduce + ROI)
  size_t height;      // decode buffer height
  int reduce;         // DWT reduce level (0 = full resolution)
  size_t region_x;    // ROI origin x in source coords
  size_t region_y;    // ROI origin y
  size_t region_w;    // ROI width in source coords
  size_t region_h;    // ROI height in source coords
};

[[nodiscard]] DecodeDims compute_decode_dims(
    const SipiImgInfo &info,              // from getDim(): src_w, src_h, clevels
    const std::shared_ptr<SipiRegion> &region,
    const std::shared_ptr<SipiSize> &size);
```

Implementation: calls `region->crop_coords(info.width, info.height, ...)` then `size->get_size(region_w, region_h, ..., reduce, ...)`. Returns the restricted dimensions that Kakadu (or pyramidal TIFF) will actually allocate.

This function is called:
- In the IIIF handler, before decode, for memory budget estimation
- In `SipiIOJ2k::read()`, replacing the inline computation
- In future `SipiIOTiff::read()`, for pyramid level selection

**Location:** `include/iiifparser/SipiDecodeDims.h` + `src/iiifparser/SipiDecodeDims.cpp` — sits alongside `SipiRegion` and `SipiSize` in the IIIF parser module.

### Integration Point

The budget acquisition must happen **after** the cache check and passthrough paths (which don't decode), but **before** `img.read()`. This avoids budget consumption on cache hits.

```
Parse IIIF URL → getDim() → pixel limit check → rate limiter check
  → cache check → passthrough check
  → [ACQUIRE memory budget]      ← HERE (after cache/passthrough, before decode)
  → peerConnected() check
  → img.read()  (actual decode)
  → rotate / color convert / watermark
  → write response
  → [RELEASE memory budget]      ← on SipiImage destruction or scope exit
```

### RAII for Exception Safety

`img.read()` can throw `std::bad_alloc`, `SipiImageError`, or `SipiSizeError`. The budget **must** be released on all exit paths. Use a `MemoryBudgetGuard` RAII class whose destructor calls release. This differs from the rate limiter (which records permanently).

### Precise Peak Memory from IIIF Parameters

Every operation in the processing pipeline allocates a NEW buffer and frees the old one. No operation is truly in-place. Peak memory at each step is `old_buffer + new_buffer`. Since the pipeline is sequential, the overall peak is the maximum of any single step's pair.

**Allocation patterns traced from source:**

| Operation | New Buffer Size | Peak (old + new) | Source |
|-----------|----------------|------------------|--------|
| `read()` | `src_w × src_h × nc × bps/8` | 1x (first alloc) | `SipiIO*.cpp` |
| `crop(region)` | `reg_w × reg_h × nc × bps/8` | src + region | `SipiImage.cpp:714` |
| `scale()` (high quality) | `out_w × out_h × nc × bps/8` + intermediate | Up to **3x** during 2-stage downscale | `SipiImage.cpp:1011` |
| `scaleFast/Medium()` | `out_w × out_h × nc × bps/8` | input + output | `SipiImage.cpp:911/956` |
| `rotate(90/270)` | `ny × nx × nc × bps/8` (dims swapped) | 2x same total | `SipiImage.cpp:1172/1251` |
| `rotate(180)` | `nx × ny × nc × bps/8` | 2x | `SipiImage.cpp:1212` |
| `rotate(arbitrary)` | `nnx × nny × nc × bps/8` (expanded) | 2x+ (rotated dims > original) | `SipiImage.cpp:1288` |
| `convertToIcc()` | `nx × ny × nnc × new_bps/8` | 2x (channels may change) | `SipiImage.cpp:478` |
| `to8bps()` | `nx × ny × nc × 1` | old 16-bit + new 8-bit = **3x data** | `SipiImage.cpp:1402` |
| `add_watermark()` | watermark image only | main + watermark (small) | `SipiImage.cpp:1479` |

**Since all IIIF parameters are known before decode, we can compute exact peak memory:**

Given request `{id}/{region}/{size}/{rotation}/{quality}.{format}` and source info `{src_w, src_h, nc, bps}`:

```
Step 1 (decode):   buf_decode  = src_w × src_h × nc × bps/8
Step 2 (crop):     buf_region  = reg_w × reg_h × nc × bps/8
Step 3 (scale):    buf_scaled  = out_w × out_h × nc × bps/8
Step 4 (rotate):   buf_rotated = rot_w × rot_h × nc × bps/8  (swap dims for 90/270)
Step 5 (quality):  buf_final   = rot_w × rot_h × nc' × bps'/8

peak = max(
  buf_decode,                         // read (first alloc)
  buf_decode  + buf_region,           // crop
  buf_region  + buf_scaled × 1.5,     // scale (1.5x for high-quality 2-stage downscale)
  buf_scaled  + buf_rotated,          // rotate
  buf_rotated + buf_final             // quality/ICC conversion
)
```

**Practical examples** (20000×30000 JP2, 4ch 16-bit, 5 DWT levels):

| Request | Decode Buffer | Peak Memory | Notes |
|---------|--------------|-------------|-------|
| `/full/max/0/default.jpg` | 4.8 GB (reduce=0) | **4.8 GB** | Full resolution, no transforms |
| `/full/max/90/default.jpg` | 4.8 GB (reduce=0) | **9.6 GB** | Decode + rotation buffer |
| `/0,0,1024,1024/1024,/0/default.jpg` | ~4 MB (ROI + reduce) | **~10 MB** | Tile: Kakadu decodes only the ROI at optimal reduce level |
| `/full/,128/0/default.jpg` | ~200 KB (reduce=5) | **~500 KB** | Thumbnail: Kakadu decodes at highest reduce level |
| `/full/,2000/0/default.jpg` | ~48 MB (reduce=2) | **~120 MB** | Medium: 2-stage downscale peak |

The difference is dramatic: tiles and thumbnails use kilobytes of budget, not gigabytes. This means the memory budget won't interfere with normal tile traffic at all — only full-resolution requests consume significant budget.

**Implementation:** Add `estimate_peak_memory()` that takes `SipiImgInfo` + parsed IIIF parameters and walks the pipeline stages to compute the exact peak. This replaces a fixed multiplier with precise per-request accounting.

## Implementation Approach

### Phase 1: MemoryBudget class

New files: `include/SipiMemoryBudget.h`, `src/SipiMemoryBudget.cpp`

```cpp
class SipiMemoryBudget {
public:
  enum class Mode { OFF, MONITOR, ENFORCE };

  SipiMemoryBudget(size_t total_budget, Mode mode);

  struct AcquireResult {
    bool allowed;       // false if over budget (and mode == ENFORCE)
    bool over_budget;   // true if would exceed budget (logged in MONITOR mode)
    size_t used;        // current usage after this request
    size_t budget;      // total budget
  };

  [[nodiscard]] AcquireResult try_acquire(size_t bytes);
  void release(size_t bytes);

  [[nodiscard]] size_t used() const;
  [[nodiscard]] size_t budget() const;

private:
  std::atomic<size_t> _used{0};
  size_t _budget;
  Mode _mode;
};

// RAII guard — releases on destruction (exception-safe)
class MemoryBudgetGuard {
public:
  MemoryBudgetGuard(SipiMemoryBudget &budget, size_t bytes);
  ~MemoryBudgetGuard();
  MemoryBudgetGuard(const MemoryBudgetGuard &) = delete;
  MemoryBudgetGuard &operator=(const MemoryBudgetGuard &) = delete;
private:
  SipiMemoryBudget &_budget;
  size_t _bytes;
  bool _acquired;
};
```

**Synchronization:** Use `std::atomic<size_t>` with `compare_exchange_weak` loop for lock-free acquire. The operation is simple (subtract if sufficient) and the rate limiter's mutex is unnecessary here — there's no sliding window, just a counter.

**Monitor mode:** In MONITOR mode, `try_acquire()` returns `allowed=true` but sets `over_budget=true` for logging. Budget is still tracked (so metrics are accurate) but requests proceed.

### Phase 2: Container memory detection

Add `detect_available_memory()` in `src/sipi.cpp`, following the existing `detect_available_cores()` pattern:

1. **cgroups v2:** Read `/sys/fs/cgroup/memory.max` — if `max` (literal string), treat as unlimited
2. **cgroups v1:** Read `/sys/fs/cgroup/memory/memory.limit_in_bytes` — if `9223372036854771712` (kernel max), treat as unlimited
3. **Fallback (Linux):** Parse `/proc/meminfo` for `MemTotal`
4. **Fallback (macOS):** `sysctl hw.memsize`

Default budget: `detected_memory * 0.75` (leaves 25% for kernel, Sipi heap, cache, Lua, threads).

### Phase 3: Extend SipiImgInfo + memory estimation

Add `nc` and `bps` to `SipiImgInfo` (`include/SipiIO.h`):

```cpp
class SipiImgInfo {
public:
  // ... existing fields ...
  int nc{ 0 };    //!< number of channels (samples per pixel)
  int bps{ 0 };   //!< bits per sample (8 or 16 typically)
};
```

Update each format's `getDim()` (1-2 lines each):
- `SipiIOJ2k.cpp`: `info.nc = codestream.get_num_components(); info.bps = codestream.get_bit_depth(0);`
- `SipiIOTiff.cpp`: `TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &nc); TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps);`
- `SipiIOJpeg.cpp`: `info.nc = cinfo.num_components; info.bps = cinfo.data_precision;`
- `SipiIOPng.cpp`: `info.nc = png_get_channels(...); info.bps = png_get_bit_depth(...);`

Add `estimate_peak_memory()` in `src/SipiHttpServer.cpp` that uses `DecodeDims`:

```cpp
/// Estimate peak memory for a complete IIIF processing pipeline.
/// Uses actual decode dimensions (with reduce levels + ROI), not full source.
static size_t estimate_peak_memory(
  const SipiImgInfo &info,        // from getDim(): nc, bps
  const DecodeDims &ddims,        // from compute_decode_dims(): actual decode w/h
  size_t out_w, size_t out_h,     // output dimensions (after scale)
  double rotation,                // rotation angle
  bool needs_icc_convert)         // quality requires ICC conversion
{
  size_t nc = (info.nc > 0) ? info.nc : 4;
  size_t bytes_per_pixel = nc * std::max(size_t(1), static_cast<size_t>(info.bps) / 8);

  // Decode buffer: actual dimensions after reduce + ROI (NOT full source)
  size_t buf_decode = ddims.width * ddims.height * bytes_per_pixel;

  // Scale: 0 means no scaling (output = decode dims)
  if (out_w == 0) out_w = ddims.width;
  if (out_h == 0) out_h = ddims.height;
  size_t buf_scaled = out_w * out_h * bytes_per_pixel;

  // Rotate: 90/270 swap dims, arbitrary angles expand
  size_t rot_w = out_w, rot_h = out_h;
  int rot_int = static_cast<int>(rotation) % 360;
  if (rot_int == 90 || rot_int == 270) { std::swap(rot_w, rot_h); }
  if (rot_int != 0 && rot_int != 90 && rot_int != 180 && rot_int != 270) {
    double diag = std::sqrt(out_w * out_w + out_h * out_h);
    rot_w = rot_h = static_cast<size_t>(diag);
  }
  size_t buf_rotated = rot_w * rot_h * bytes_per_pixel;

  // ICC conversion: channels may change but dimensions don't
  size_t buf_final = needs_icc_convert ? rot_w * rot_h * bytes_per_pixel : 0;

  // Peak = max of any (old + new) pair across pipeline stages.
  // Note: for JP2/pyramidal TIFF, region extraction happens INSIDE the decoder
  // (via ROI restriction), so there is no separate crop buffer — buf_decode
  // already reflects the cropped region at the reduce level.
  // scale() can use 2-stage downscale: ~1.5x intermediate buffer.
  size_t scale_peak = buf_decode + buf_scaled + buf_scaled / 2;
  return std::max({
    buf_decode,                       // read (first alloc)
    scale_peak,                       // scale (2-stage downscale worst case)
    buf_scaled + buf_rotated,         // rotate
    buf_rotated + buf_final           // ICC conversion
  });
}
```

All inputs come from:
- `getDim()` → `SipiImgInfo` (nc, bps, clevels)
- `compute_decode_dims()` → `DecodeDims` (actual decode w/h after reduce + ROI)
- Parsed IIIF URL → output size, rotation, quality

No guessing, no fixed multipliers, no duplication of reduce/ROI logic.

### Phase 4: Integration in IIIF handler

In `src/SipiHttpServer.cpp`, after cache check (around line 1797), before `img.read()`:

```cpp
// Memory budget check — only for requests that will decode
size_t estimated_memory = estimate_decode_memory(img_w, img_h, mimetype);
auto budget_result = server->memory_budget()->try_acquire(estimated_memory);
if (!budget_result.allowed) {
  SipiMetrics::instance().decode_memory_rejected_total.Increment();
  conn_obj.header("Retry-After", "5");
  send_error(conn_obj, Connection::SERVICE_UNAVAILABLE, "Server memory budget exhausted");
  return;
}
MemoryBudgetGuard budget_guard(*server->memory_budget(), estimated_memory);
// ... img.read(), rotate, watermark, send response ...
// budget_guard releases automatically on scope exit or exception
```

### Phase 5: Config wiring (4 surfaces)

| Surface | File | Addition |
|---------|------|----------|
| Field | `SipiConf.h` | `size_t max_decode_memory{0};` `std::string decode_memory_mode_str{"off"};` |
| Lua | `SipiConf.cpp` | `parseSizeString(luacfg.configString("sipi", "max_decode_memory", "0"))` |
| CLI | `sipi.cpp` | `--max-decode-memory` + `SIPI_MAX_DECODE_MEMORY`, `--decode-memory-mode` + `SIPI_DECODE_MEMORY_MODE` |
| Docs | `sipi.config.lua` | Inline comments with defaults and usage |
| Server | `SipiHttpServer.hpp` | `memory_budget()` accessor returning `SipiMemoryBudget*` (nullptr when off) |

### Phase 6: Prometheus metrics

| Metric                                | Type             | Labels                      | Description                                                                                                     |
| ------------------------------------- | ---------------- | --------------------------- | --------------------------------------------------------------------------------------------------------------- |
| `sipi_build_info`                     | gauge (info)     | `version`, `commit`         | Always value `1`. Labels carry build metadata for version correlation. Set once at startup.                      |
| `sipi_decode_memory_budget_bytes`     | gauge            | —                           | Configured budget. Set once at startup. (Gauge is the Prometheus convention for static config values — there is no "constant" type.) |
| `sipi_decode_memory_used_bytes`       | gauge            | —                           | Currently allocated to in-flight decodes                                                                        |
| `sipi_decode_memory_decisions_total`  | counter (family) | `action`                    | Core metric — `acquired`, `rejected`, `shadow_rejected` (mirrors rate limiter pattern)                          |
| `sipi_decode_memory_near_limit_total` | counter          | —                           | Acquisitions where usage > 80% of budget (early warning for capacity planning)                                  |
| `sipi_decode_memory_estimate_bytes`   | histogram        | —                           | Distribution of per-request peak memory estimates (capacity planning: shows what size requests are being served) |

**Build info metric — version correlation:**

```cpp
// In SipiMetrics.cpp:
auto &build_info = prometheus::BuildGauge()
    .Name("sipi_build_info")
    .Help("Sipi build metadata. Value is always 1.")
    .Register(*registry_)
    .Add({{"version", BUILD_SCM_TAG}, {"commit", BUILD_SCM_REVISION}});
build_info.Set(1);
```

This enables:
- `changes(sipi_build_info[1h]) > 0` — alert on version changes (deployment detection)
- Grafana annotations: overlay deploys on dashboards to correlate behavior changes with versions
- Join with other metrics: `sipi_request_duration_seconds * on() group_left(version) sipi_build_info`

**Static config gauges** (`budget_bytes`, and the existing `cache_size_limit_bytes`, `cache_files_limit`) follow the same Prometheus convention — gauges set once at startup. There is no dedicated "constant" metric type; a gauge that never changes after init is the standard pattern (used by `node_exporter`, `prometheus` itself, and most exporters).

**Label values for `sipi_decode_memory_decisions_total`:**
- `acquired` — budget available, request proceeds
- `rejected` — budget exhausted, ENFORCE mode → 503
- `shadow_rejected` — budget exhausted, MONITOR mode → allowed but logged

**Why a histogram for estimates:** Operators need to know the distribution of memory demand to size the budget correctly. If 99% of requests estimate < 1 MB (tiles) and 1% estimate > 500 MB (full-res), the budget only needs to handle the latter. Histogram buckets: `[1KB, 10KB, 100KB, 1MB, 10MB, 100MB, 500MB, 1GB, 2GB]`.

**Gauge update timing:**
- `sipi_decode_memory_used_bytes` updated on every acquire (increment) and release (decrement) via the RAII guard
- `sipi_decode_memory_budget_bytes` set once at startup

**Operational dashboards:**
- **Budget utilization:** `sipi_decode_memory_used_bytes / sipi_decode_memory_budget_bytes` — should be < 0.8 normally
- **Rejection rate:** `rate(sipi_decode_memory_decisions_total{action="rejected"}[5m])` — should be 0 under normal load
- **Early warning:** `rate(sipi_decode_memory_near_limit_total[5m])` > 0 means budget is tight
- **Request size distribution:** `histogram_quantile(0.99, sipi_decode_memory_estimate_bytes)` — shows the largest 1% of requests

### Phase 7: Tests (TDD — write tests before implementation)

Tests are written FIRST for each component, following the existing patterns in the codebase. The long-term strategy is migration to Rust, so e2e tests use the Rust test harness.

#### C++ Unit Tests — `test/unit/memory_budget/`

**CMakeLists.txt pattern:** follows `test/unit/ratelimiter/CMakeLists.txt` (GoogleTest + `libsipi_testable`).

**`memory_budget_test.cpp` — MemoryBudget class (TDD, write first):**
- `AcquireWithinBudgetSucceeds` — acquire 100 bytes from 1000-byte budget → allowed=true, used=100
- `AcquireExceedingBudgetFailsInEnforce` — acquire 1100 from 1000 → allowed=false, over_budget=true
- `AcquireExceedingBudgetSucceedsInMonitor` — same but Mode::MONITOR → allowed=true, over_budget=true
- `AcquireExceedingBudgetIgnoredWhenOff` — Mode::OFF → allowed=true, over_budget=false, used=0
- `ReleaseRestoresBudget` — acquire 500, release 500 → used=0
- `MultipleAcquiresAccumulate` — acquire 300 + 300 + 300 → used=900, then 4th acquire of 200 fails
- `ReleaseMoreThanAcquiredClampsToZero` — release 2000 when used=100 → used=0 (no underflow)
- `ConcurrentAcquireRelease` — 8 threads each acquire/release 100 times → final used=0
- `ConcurrentAcquiresRespectBudget` — 8 threads race to acquire, total never exceeds budget
- `ZeroBytesAcquireAlwaysSucceeds` — tiles with 0 estimate pass through

**`memory_budget_guard_test.cpp` — RAII guard:**
- `GuardReleasesOnScopeExit` — acquire via guard, exit scope → budget released
- `GuardReleasesOnException` — acquire via guard, throw → budget released (catch outside scope)
- `GuardNotAcquiredDoesNotRelease` — guard with failed acquire → destructor is no-op
- `GuardMoveSemantics` — moved guard releases once, not twice

#### C++ Unit Tests — `test/unit/decode_dims/`

**`decode_dims_test.cpp` — compute_decode_dims() (TDD, write first):**
- `FullRegionFullSizeReturnsSourceDims` — no region, no size → decode_w=src_w, reduce=0
- `FullRegionWithSizeReturnsReducedDims` — /full/,128/ on 20000x30000 with 5 clevels → reduce=5, dims≈625x937
- `RegionCropsBeforeReduce` — /0,0,1024,1024/1024,/ → decode_w≈1024 at optimal reduce
- `PercentRegion` — /pct:10,10,50,50/ → correct source-coord crop
- `ReduceLevelNeverExceedsClevels` — requested size smaller than max reduce → clamped to clevels
- `ReduceLevelZeroForFullMax` — /full/max/ → reduce=0, full source dims
- `SquareRegion` — /square/ → shorter side used

#### C++ Unit Tests — `test/unit/sipiimage/` (extend existing)

**`imginfo_test.cpp` — SipiImgInfo nc/bps from getDim():**
- `JP2GetDimReturnsChannelsAndBps` — getDim() on a JP2 test file → nc=3, bps=8 (or appropriate)
- `TiffGetDimReturnsChannelsAndBps` — getDim() on a TIFF test file → nc/bps correct
- `JpegGetDimReturnsChannelsAndBps` — getDim() on a JPEG test file → nc=3, bps=8
- `PngGetDimReturnsChannelsAndBps` — getDim() on a PNG test file → nc/bps correct
- `GetDimDimensionsMatchExpected` — verify width/height match known test images

#### C++ Unit Tests — `test/unit/memory_budget/`

**`peak_memory_test.cpp` — estimate_peak_memory() (TDD, write first):**
- `FullMaxNoTransformPeakEqualsDecodeBuffer` — /full/max/0/default.jpg → peak = decode only
- `FullMaxWithRotation90PeakIsDoubleDecodeBuffer` — /full/max/90/ → peak = 2x decode
- `TileRequestPeakIsTiny` — /0,0,1024,1024/1024,/0/ on 20000x30000 → peak = few MB
- `ThumbnailPeakIsTiny` — /full/,128/0/ → peak = few KB
- `HighQualityDownscalePeakIs1_5x` — scale() 2-stage → peak includes intermediate
- `ArbitraryRotationExpandsDimensions` — 45° → peak accounts for sqrt(2) expansion
- `ICCConversionAddsFinalBuffer` — quality=color with ICC → peak includes conversion buffer

#### Prerequisite: Refactor test harness for CLI arg overrides (replaces DEV-6099)

**Problem:** Every test scenario requiring different server config currently needs a full copy of the ~50-line Lua config with 2-3 lines changed. The rate limiter already has 2 near-identical configs. The memory budget would need 2 more. This doesn't scale.

**Solution:** Add `SipiServer::start_with_args()` to the Rust test harness. Sipi already supports CLI overrides for all config via CLI11 (`--nthreads`, `--max-waiting`, etc.). The harness just needs to pass them through:

```rust
impl SipiServer {
    /// Start with extra CLI arguments appended after the standard ones.
    /// CLI args override Lua config values (CLI11 precedence).
    pub fn start_with_args(config: &str, working_dir: &Path, extra_args: &[&str]) -> Self {
        // ... same as start() but appends extra_args to the Command before spawn
    }

    // Existing methods become convenience wrappers:
    pub fn start(config: &str, working_dir: &Path) -> Self {
        Self::start_with_args(config, working_dir, &[])
    }

    pub fn start_default() -> Self {
        let test_data = test_data_dir();
        Self::start("config/sipi.e2e-test-config.lua", &test_data)
    }
}
```

**Refactoring steps:**
1. Rename `sipi.fake-knora-test-config.lua` → `sipi.e2e-test-config.lua` (update 6 references: `lib.rs`, `upload.rs`, `shutdown.rs`, `iiif_compliance.rs`, `Makefile`, `loadtest.yml`)
2. Extract the body of `start()` into `start_with_args()` that accepts `extra_args: &[&str]`
3. Insert `cmd.args(extra_args)` after the standard `--config`/`--serverport`/`--sslport` args (line 63)
4. Make `start()` delegate to `start_with_args(config, working_dir, &[])`
5. Update `start_default()` to use the new config name
6. Verify all existing tests still pass (they call `start()` or `start_default()`, unchanged)

**Impact:** No more config file duplication. Tests become self-documenting:

```rust
// Memory budget: enforce mode, 10 MB budget, 2 threads
fn start_budget_enforce() -> SipiServer {
    let test_data = test_data_dir();
    SipiServer::start_with_args(
        "config/sipi.e2e-test-config.lua", &test_data,
        &["--max-decode-memory", "10M", "--decode-memory-mode", "enforce", "--nthreads", "2"]
    )
}

// Rate limiter tests could also be simplified (future cleanup):
fn start_enforce_server() -> SipiServer {
    let test_data = test_data_dir();
    SipiServer::start_with_args(
        "config/sipi.e2e-test-config.lua", &test_data,
        &["--rate-limit-mode", "enforce", "--rate-limit-max-pixels", "500000",
          "--rate-limit-window", "60", "--rate-limit-pixel-threshold", "1000"]
    )
}
```

This replaces DEV-6099 with a smaller, focused change done as part of this PR.

#### Rust E2E Tests — `test/e2e-rust/tests/memory_budget.rs`

All tests use `start_with_args()` against the default test config with CLI overrides — **no new Lua config files needed**.

**Tests:**
- `enforce_tile_request_within_budget_succeeds` — tile request uses tiny budget, always passes
- `enforce_full_resolution_within_budget_succeeds` — single large request within 10 MB budget → 200
- `enforce_budget_exhaustion_returns_503` — send concurrent requests exceeding 10 MB budget → some get 503
- `enforce_503_includes_retry_after_header` — verify `Retry-After` header on 503 response
- `enforce_budget_released_after_request_completes` — exhaust budget, wait for completion, next succeeds
- `monitor_over_budget_still_returns_200` — monitor mode logs but doesn't reject
- `monitor_metrics_show_over_budget` — `sipi_decode_memory_rejected_total` > 0 via /metrics
- `off_mode_no_budget_tracking` — OFF mode, metrics show 0 used
- `metrics_budget_gauge_reflects_config` — `sipi_decode_memory_budget_bytes` matches 10 MB
- `metrics_used_gauge_returns_to_zero` — after request completes, used bytes gauge drops back

#### Test Execution Order (TDD flow)

Tests ALWAYS come first. For new code, tests define the contract. For refactored code, tests lock in current behavior BEFORE changing the implementation — any regression is caught immediately.

**Step 0 — Lock existing behavior before refactoring:**
1. Write `imginfo_test.cpp` — test `getDim()` for each format against known test images: assert current width, height, success status. These tests pass on the EXISTING code.
2. Write `image_ops_test.cpp` — test `crop()`, `scaleFast()`, `scaleMedium()`, `scale()`, `rotate()`, `to8bps()` against known test images: load image, apply operation, verify output dimensions and pixel checksums. These tests pass on the EXISTING code.
3. Write Rust e2e tests for IIIF transform pipeline: request known transformations, verify response dimensions and byte sizes. These capture the current server behavior end-to-end.
4. Run all tests GREEN on current code. Commit the tests.

**Step 1 — Refactor old code (tests catch regressions):**
1. Apply RAII to `getDim()` file handles (TIFF, PNG, JPEG) → run Step 0 tests, still GREEN
2. Apply RAII to intermediate buffers in crop/scale/rotate → run Step 0 tests, still GREEN
3. Add `[[nodiscard]]` to virtual methods → fix any unchecked call sites
4. Migrate `getDim()` to `std::expected` return → update call sites → run Step 0 tests, still GREEN
5. Commit the refactoring separately from the tests.

**Step 2 — New feature TDD (tests define the contract):**
1. Write `memory_budget_test.cpp` + `memory_budget_guard_test.cpp` → tests RED → implement `SipiMemoryBudget` → GREEN
2. Write `decode_dims_test.cpp` → RED → implement `compute_decode_dims()` → GREEN
3. Extend `imginfo_test.cpp` with nc/bps assertions → RED → extend `getDim()` → GREEN
4. Write `peak_memory_test.cpp` → RED → implement `estimate_peak_memory()` → GREEN
5. Write Rust e2e tests for memory budget (enforce/monitor/metrics) → RED → integrate in IIIF handler + config + metrics → GREEN

**Step 3 — JP2 refactor (existing + new tests protect):**
1. Refactor `SipiIOJ2k::read()` to use `compute_decode_dims()` → run ALL tests (Step 0 + Step 2), still GREEN
2. Add fuzz targets → run nightly

#### Code Coverage Requirements

- All new code must work with the existing `gcovr`/`llvm-cov` pipeline (`make nix-coverage` → `coverage.xml` → Codecov)
- Codecov patch coverage target: **80%** (per `.github/codecov.yml`)
- New test targets must be included in the CMake build that produces `.gcda` files (the nix-clang build with `--coverage` flags)
- The `libsipi_testable` library already links all source files with coverage instrumentation — new unit tests linking against it automatically generate coverage data

**Coverage extension beyond new code:** Since we're touching `SipiImage.cpp` (crop/scale/rotate), `SipiIOJ2k.cpp` (getDim/read), `SipiIOTiff.cpp` (getDim), `SipiIOJpeg.cpp` (getDim), and `SipiIOPng.cpp` (getDim), add targeted unit tests for the specific functions we modernize:

| File | Function | Current Coverage | Tests to Add |
|------|----------|-----------------|--------------|
| `SipiImage.cpp` | `crop()` | None (0 unit tests) | Crop within bounds, crop at edge, crop full image (no-op) |
| `SipiImage.cpp` | `scaleFast()` / `scaleMedium()` / `scale()` | None | Scale up, scale down, scale to same size, 2-stage downscale |
| `SipiImage.cpp` | `rotate()` | None | 0°, 90°, 180°, 270°, arbitrary angle |
| `SipiImage.cpp` | `to8bps()` | None | 16→8 bit conversion, already-8-bit no-op |
| `SipiIOJ2k.cpp` | `getDim()` | None | JP2 with valid header, corrupt header |
| `SipiIOTiff.cpp` | `getDim()` | None | TIFF with valid tags, missing tags |
| `SipiIOJpeg.cpp` | `getDim()` | None | JPEG with valid header |
| `SipiIOPng.cpp` | `getDim()` | None | PNG with valid header |

These tests serve dual purpose: coverage improvement AND regression protection for the RAII refactoring.

#### Fuzzing

**New fuzz target: `compute_decode_dims` — `fuzz/decode_dims/`**

`compute_decode_dims()` takes parsed IIIF region/size parameters and source dimensions, and computes decode dimensions with reduce levels. It's pure computation with potential for:
- Integer overflow in dimension calculations (`width * height * nc * bps`)
- Division by zero (reduce level computation)
- Edge cases in region parsing (percent regions, square regions on non-square images)

```cpp
// fuzz/decode_dims/decode_dims_fuzz_target.cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size < 20) return 0; // need enough bytes for parameters
  // Deserialize fuzz input into SipiImgInfo + region/size strings
  // Call compute_decode_dims() and estimate_peak_memory()
  // No crash expected — both are noexcept pure computation
  return 0;
}
```

**Seed corpus:** derive from the existing IIIF URI parser corpus (which already contains region/size combinations).

**New fuzz target: `estimate_peak_memory` — same binary**

Chain the output of `compute_decode_dims()` into `estimate_peak_memory()` to fuzz the full estimation pipeline. Look for integer overflows that could cause underestimation (leading to OOM) or wild overestimation (leading to false 503s).

**Integration with CI:** Add both targets to the existing `fuzz.yml` workflow (or extend the build to compile them alongside the IIIF URI parser fuzzer). The nightly run already handles corpus persistence and crash reporting.

#### ASan/UBSan

The existing `sanitizer.yml` workflow runs ASan+UBSan on all PRs touching `src/`, `shttps/`, `include/`, or `test/`. All new code is automatically covered. Key things ASan will catch in this PR:
- Buffer overflows from incorrect dimension calculations
- Use-after-free if RAII guard releases prematurely
- Memory leaks from the old `new`/`delete` patterns being refactored

### Phase 8b: Code Modernization (touch-and-improve)

Apply C++23 conventions to all old code we touch, per CLAUDE.md: "When modifying existing code, apply modernization opportunistically."

#### RAII for getDim() file handles

Each format's `getDim()` opens a file handle (`TIFF*`, `FILE*`, `int fd`) and manually closes it on every exit path. Refactor to RAII:

```cpp
// SipiIOTiff.cpp getDim() — before:
TIFF *tif = TIFFOpen(filepath.c_str(), "r");
// ... 3 different TIFFClose(tif) on error paths + 1 on success

// After:
auto tif = std::unique_ptr<TIFF, decltype(&TIFFClose)>(
    TIFFOpen(filepath.c_str(), "r"), TIFFClose);
if (!tif) { info.success = SipiImgInfo::FAILURE; return info; }
// ... no manual TIFFClose needed, destructor handles all paths
```

Same pattern for:
- `SipiIOPng.cpp`: `FILE *infile` → `std::unique_ptr<FILE, decltype(&fclose)>`
- `SipiIOJpeg.cpp`: `int fd` → RAII wrapper with `::close`
- `SipiIOJ2k.cpp`: Kakadu handles are already RAII-ish (stack objects with `.close()`) — verify `.close()` is called on all throw paths

#### RAII for pixel buffer allocations in SipiImage

The crop/scale/rotate methods use `new byte[size]` + `delete[] inbuf`. Wrap the intermediate buffer:

```cpp
// Before (crop, scale, rotate):
byte *outbuf = new byte[width * height * nc];
// ... copy loop ...
delete[] inbuf;
pixels = outbuf;

// After:
auto outbuf = std::make_unique<byte[]>(width * height * nc);
// ... copy loop using outbuf.get() ...
// Transfer ownership:
delete[] pixels;
pixels = outbuf.release();
```

This is incremental — the `pixels` member itself stays as `byte*` for now (changing it to `unique_ptr` would be a much larger refactoring across the entire SipiImage class). The intermediate buffers get RAII protection against exceptions during the copy loop.

#### [[nodiscard]] on virtual methods

Add to `SipiIO` base class:
- `[[nodiscard]] virtual bool read(...)`
- `[[nodiscard]] virtual SipiImgInfo getDim(...)`

And to `SipiImage`:
- `[[nodiscard]] bool crop(...)`
- `[[nodiscard]] bool scaleFast(...)`, `scaleMedium(...)`, `scale(...)`
- `[[nodiscard]] bool rotate(...)`
- `[[nodiscard]] bool to8bps()`

#### getDim() error model

Replace the `SipiImgInfo::success` enum with `std::expected`:

```cpp
// Before:
SipiImgInfo getDim(const std::string &filepath);
// Caller checks: if (info.success == SipiImgInfo::FAILURE) ...

// After:
[[nodiscard]] std::expected<SipiImgInfo, std::string> getDim(const std::string &filepath);
// Caller: auto info = img.getDim(infile); if (!info) { send_error(..., info.error()); return; }
```

This change propagates to all 4 format implementations and all call sites. Do it as a separate commit for clean review.

### Phase 8: Documentation (per reviewer-guidelines.md and CONVENTIONS.md)

**reviewer-guidelines.md checklist:**
- [ ] New config keys documented in `sipi.md`, `running.md`, and config file inline comments
- [ ] New CLI flags/env vars `--help` text updated, documented in `running.md`
- [ ] Thread-safety guarantees documented on `SipiMemoryBudget` class (accessed from multiple worker threads)
- [ ] Feature discoverable without reading source

**Config documentation (all 5 surfaces per CONVENTIONS.md):**

| Surface | File | What to add |
|---------|------|-------------|
| Field + accessor | `SipiConf.h`, `SipiHttpServer.hpp` | `max_decode_memory`, `decode_memory_mode` with doc comments |
| Lua config | `SipiConf.cpp` | `luacfg.configString("sipi", "max_decode_memory", "0")` |
| CLI | `sipi.cpp` | `--max-decode-memory` + `SIPI_MAX_DECODE_MEMORY`, `--decode-memory-mode` + `SIPI_DECODE_MEMORY_MODE` |
| Config file | `config/sipi.config.lua` | Inline comments with defaults, valid values, operational guidance |
| Config file | `config/sipi.localdev-config.lua` | Same keys with localdev defaults |

**New documentation files:**

| File | Content |
|------|---------|
| `docs/src/operation/memory-budget.md` | Operational guide matching `rate-limiter.md` pattern: why, config table, monitor-to-enforce workflow, Prometheus metrics, Loki queries, traffic patterns, troubleshooting |

**Existing documentation updates:**

| File | What to update |
|------|----------------|
| `docs/src/guide/sipi.md` | Add `max_decode_memory` and `decode_memory_mode` to config reference section |
| `docs/src/guide/running.md` | Add CLI flags to flags table + env vars to env var table |
| `docs/src/operation/health-endpoint.md` | Remove Traefik health router section (being deleted in PR #1237 review), add note about queue-level 503 risk under extreme load and DEV-6101 |
| `docs/src/development/testing-strategy.md` | Update coverage matrix with new test areas (MemoryBudget, DecodeDims, getDim nc/bps, peak estimation) |
| `docs/src/development/fuzzing.md` | Add `compute_decode_dims` and `estimate_peak_memory` fuzz targets |

**Project-level documentation updates:**

| File | What to update |
|------|----------------|
| `CONVENTIONS.md` | Add 503 for memory budget exhaustion to HTTP Status Codes table |
| `REVIEW.md` | Add to "Resource exhaustion" section: "Memory budget acquired before decode, released via RAII guard" |
| `REVIEW.md` | Add to "Thread safety" section: "`SipiMemoryBudget` uses `std::atomic<size_t>` — lock-free acquire/release" |
| `CLAUDE.md` | Add `SipiMemoryBudget` to Core Components table in High-Level Architecture |

### Phase 9: ops-deploy config (PR #1237) + health check mitigation

Resolve review comments on [ops-deploy PR #1237](https://github.com/dasch-swiss/ops-deploy/pull/1237) and extend with memory budget config.

**Review comments to address:**

1. **Remove `iiif-health` Traefik router** (SamuelBoerlin, LukasStoeckli): The `/health` endpoint is already served by the default `{{ STACK }}-iiif` router — the extra router is unnecessary. Remove the 6 health-specific Traefik labels from `docker-compose-iiif.yml.j2`.

2. **Per-client identification clarified** (LukasStoeckli): Traefik already sends `X-Forwarded-For` automatically. Sipi's `resolve_client_id()` reads the rightmost entry. Add a comment in the Lua config template explaining this:
   ```lua
   -- Rate limiting: per-client pixel budget
   -- Client identified via X-Forwarded-For header (set by Traefik automatically).
   -- mode: "off" (disabled), "monitor" (log only), "enforce" (return 429)
   ```

**New config for memory budget:**

`roles/dsp-deploy/defaults/main.yml`:
```yaml
DSP_IIIF_MAX_DECODE_MEMORY: "0"          # 0 = auto (75% of container memory)
DSP_IIIF_DECODE_MEMORY_MODE: "monitor"   # off, monitor, enforce
```

`roles/dsp-deploy/templates/iiif/conf/sipi.prod-config.lua.j2`:
```lua
    --
    -- Memory budget: global decode memory limit (prevents OOM from concurrent large decodes)
    -- 0 = auto-detect from container memory limit (75% of cgroup memory.max)
    -- mode: "off" (disabled), "monitor" (log only), "enforce" (return 503)
    --
    max_decode_memory = "{{ DSP_IIIF_MAX_DECODE_MEMORY | default('0') }}",
    decode_memory_mode = "{{ DSP_IIIF_DECODE_MEMORY_MODE | default('monitor') }}",
```

**Health check interaction (DEV-6101):** The `/health` handler always returns 200, but under extreme load it can be queue-delayed at the connection layer. The memory budget is the primary mitigation — by limiting concurrent heavy decodes, it keeps threads available for lightweight requests like `/health`. Do NOT weaken the Docker HEALTHCHECK timing (3 retries × 30s = 90s detection) — that's the right tolerance for detecting genuinely hung containers. A proper architectural fix (health bypasses backpressure) is tracked in DEV-6101.

**Rollout strategy:** Ship in `monitor` mode (same as rate limiter). Observe `sipi_decode_memory_decisions_total{action="shadow_rejected"}` for 1-2 weeks, tune budget if needed, then switch to `enforce`.

## Dependencies

- **DEV-6024** (merged) — thread pool fixes that this builds on
- **DEV-6099** — superseded by the `start_with_args()` refactoring included in this plan (can be closed)

## Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Over-estimation rejects legitimate requests | Medium | Medium | Ship in MONITOR mode first; tune with metrics |
| Under-estimation allows OOM | Low | High | 2x multiplier + conservative per-format bpp |
| Atomic contention under high load | Low | Low | Single atomic CAS is nanoseconds vs. millisecond decode |
| cgroups detection fails in exotic environments | Low | Low | Fallback to system memory; explicit config override |

## Success Metrics

- Zero OOM kills under concurrent load with budget enabled
- `sipi_decode_memory_rejected_total` > 0 only under genuine overload (not normal tile traffic)
- No measurable latency increase for cache-hit or tile requests (budget check bypassed)
- Monitor mode provides accurate budget usage data for capacity planning

## Files to Modify

| File | Action | Description |
|------|--------|-------------|
| `include/SipiMemoryBudget.h` | Create | MemoryBudget class + RAII guard |
| `src/SipiMemoryBudget.cpp` | Create | Implementation |
| `include/iiifparser/SipiDecodeDims.h` | Create | `DecodeDims` struct + `compute_decode_dims()` |
| `src/iiifparser/SipiDecodeDims.cpp` | Create | Shared reduce/ROI computation (used by budget, JP2, TIFF) |
| `include/SipiIO.h` | Modify | Add `nc` and `bps` fields to `SipiImgInfo` |
| `src/formats/SipiIOJ2k.cpp` | Modify | Extract nc/bps in `getDim()` + refactor `read()` to use `compute_decode_dims()` |
| `src/formats/SipiIOTiff.cpp` | Modify | Extract nc/bps in `getDim()` + (future) use `compute_decode_dims()` for pyramidal |
| `src/formats/SipiIOJpeg.cpp` | Modify | Extract nc/bps in `getDim()` |
| `src/formats/SipiIOPng.cpp` | Modify | Extract nc/bps in `getDim()` |
| `include/SipiConf.h` | Modify | Add config fields |
| `src/SipiConf.cpp` | Modify | Add Lua config parsing |
| `src/sipi.cpp` | Modify | Add CLI options + `detect_available_memory()` + wiring |
| `src/SipiHttpServer.cpp` | Modify | Integration point + estimation helper |
| `src/SipiHttpServer.hpp` | Modify | Add `memory_budget()` accessor |
| `include/SipiMetrics.h` | Modify | Add 4 new metrics |
| `src/SipiMetrics.cpp` | Modify | Initialize metrics |
| `config/sipi.config.lua` | Modify | Add config entries |
| `config/sipi.localdev-config.lua` | Modify | Add config entries |
| `docs/src/guide/sipi.md` | Modify | Config reference |
| `docs/src/guide/running.md` | Modify | CLI/env var tables |
| `test/unit/memory_budget/CMakeLists.txt` | Create | Test target for MemoryBudget + peak estimation |
| `test/unit/memory_budget/memory_budget_test.cpp` | Create | MemoryBudget acquire/release/concurrent tests |
| `test/unit/memory_budget/memory_budget_guard_test.cpp` | Create | RAII guard exception safety tests |
| `test/unit/memory_budget/peak_memory_test.cpp` | Create | estimate_peak_memory() pipeline stage tests |
| `test/unit/decode_dims/CMakeLists.txt` | Create | Test target for DecodeDims |
| `test/unit/decode_dims/decode_dims_test.cpp` | Create | compute_decode_dims() reduce/ROI tests |
| `test/unit/sipiimage/imginfo_test.cpp` | Create | getDim() nc/bps extraction per format |
| `test/e2e-rust/src/lib.rs` | Modify | Refactor: extract `start_with_args()` from `start()` |
| `test/e2e-rust/tests/memory_budget.rs` | Create | Rust e2e: enforce/monitor/off + metrics (uses CLI overrides, no new Lua configs) |
| `test/CMakeLists.txt` | Modify | Add new test subdirectories |
| `CMakeLists.txt` | Modify | Add new source files |
| **ops-deploy** | | |
| `roles/dsp-deploy/defaults/main.yml` | Modify | Add `DSP_IIIF_MAX_DECODE_MEMORY`, `DSP_IIIF_DECODE_MEMORY_MODE` |
| `roles/dsp-deploy/templates/iiif/conf/sipi.prod-config.lua.j2` | Modify | Add memory budget config + fix review comments |
| `roles/dsp-deploy/templates/docker-compose-iiif.yml.j2` | Modify | Remove unnecessary iiif-health Traefik router |
