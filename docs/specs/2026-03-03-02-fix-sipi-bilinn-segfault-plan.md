---
title: "fix: SIPI segfault in bilinear interpolation during image scaling"
type: fix
date: 2026-03-03
author: "Ivan Subotic"
status: reviewed
repositories:
  - sipi
---

# fix: SIPI segfault in bilinear interpolation during image scaling

## Overview

Fix out-of-bounds buffer reads in `bilinn()` and division-by-zero bugs in scaling LUT calculations. The primary fix adds a `ny` parameter to `bilinn()` and clamps both coordinates inside the function, which protects all call sites. Secondary fixes add guards to each scaling method and the TIFF `doReduce()` template.

## Files to Modify

| File | Changes |
|------|---------|
| `sipi/include/SipiImage.hpp` | Add `ny` parameter to both `bilinn()` declarations (lines 129-130) |
| `sipi/src/SipiImage.cpp` | Add `ny` param to both `bilinn()` definitions; clamp ix/iy/rx/ry; add early-return guards with logging to `scale()`, `scaleFast()`, `scaleMedium()`; update all 10 `bilinn()` call sites |
| `sipi/src/formats/SipiIOTiff.cpp` | Guard `cnt == 0` in `doReduce()` (line 1742) |
| `sipi/test/unit/sipiimage/sipiimage.cpp` | Add 5-6 boundary-condition regression tests |

## Implementation

### Step 1: Add `ny` parameter to `bilinn()` declarations

**File:** `sipi/include/SipiImage.hpp`, lines 129-130

`bilinn()` is a `static` method — it has no `this` pointer and cannot access the member variable `ny`. The function currently receives only `nx` (used by the `POSITION` macro as the row stride). To clamp the `y` coordinate, `ny` must be passed explicitly.

Change:
```cpp
static byte bilinn(byte buf[], int nx, double x, double y, int c, int n);
static word bilinn(word buf[], int nx, double x, double y, int c, int n);
```

To:
```cpp
static byte bilinn(byte buf[], int nx, int ny, double x, double y, int c, int n);
static word bilinn(word buf[], int nx, int ny, double x, double y, int c, int n);
```

### Step 2: Fix `bilinn()` definitions — clamp coordinates (both overloads)

**File:** `sipi/src/SipiImage.cpp`, lines 772-797 (byte) and 801-826 (word)

For both overloads, add the `ny` parameter, a degenerate-buffer guard, coordinate clamping, and fractional-part clamping:

```cpp
byte SipiImage::bilinn(byte buf[], const int nx, const int ny, const double x, const double y, const int c, const int n)
{
  // Degenerate buffer — interpolation needs at least 2x2
  if (nx < 2 || ny < 2) { return buf[n * c]; }

  auto ix = static_cast<int>(x);
  auto iy = static_cast<int>(y);

  // Clamp to valid interpolation range: ix in [0, nx-2], iy in [0, ny-2]
  // This ensures ix+1 <= nx-1 and iy+1 <= ny-1 (always in-bounds)
  if (ix < 0) { ix = 0; }
  if (iy < 0) { iy = 0; }
  if (ix >= nx - 1) { ix = nx - 2; }
  if (iy >= ny - 1) { iy = ny - 2; }

  // Clamp fractional parts to [0.0, 1.0] — after clamping ix/iy, the raw
  // difference (x - ix) can exceed 1.0, which would produce negative
  // interpolation weights and wrap-around artifacts in byte/word values.
  const auto rx = std::clamp(x - static_cast<double>(ix), 0.0, 1.0);
  const auto ry = std::clamp(y - static_cast<double>(iy), 0.0, 1.0);

  constexpr double Threshold = 1.0e-2;
  // ... rest of function unchanged (threshold checks and interpolation math)
```

**Key changes:**
- `const auto ix/iy` become `auto ix/iy` (mutable for clamping)
- `rx`/`ry` computation moves after the clamp
- `rx`/`ry` are clamped to `[0.0, 1.0]` via `std::clamp` to prevent negative interpolation weights
- Degenerate guard returns `buf[n * c]` (first pixel, channel `c`) — safe for any buffer of size >= `n`

Apply the identical change to the `word` overload (lines 801-826).

### Step 3: Update all `bilinn()` call sites to pass `ny`

**File:** `sipi/src/SipiImage.cpp`

There are 10 call sites. Each needs the `ny` value inserted after `nx`:

**`scaleMedium()` — lines 889, 904** (8bps and 16bps paths):
```cpp
// Before: bilinn(inbuf, nx, rx, ry, k, nc)
// After:  bilinn(inbuf, nx, ny, rx, ry, k, nc)
```
Here `ny` is the member variable (accessible since `scaleMedium()` is a non-static method).

**`scale()` — lines 962, 977** (8bps and 16bps paths):
```cpp
// Before: bilinn(inbuf, nx, rx, ry, k, nc)
// After:  bilinn(inbuf, nx, ny, rx, ry, k, nc)
```

**`rotate()` — lines 1236, 1256** (8bps and 16bps paths):
```cpp
// Before: bilinn(inbuf, nx, rx, ry, k, nc)
// After:  bilinn(inbuf, nx, ny, rx, ry, k, nc)
```

**`add_watermark()` — lines 1408, 1411**:
```cpp
// Before: bilinn(wmbuf, wm_nx, wm_i, wm_j, 3, wm_nc)
// After:  bilinn(wmbuf, wm_nx, wm_ny, wm_i, wm_j, 3, wm_nc)

// Before: bilinn(wmbuf, wm_nx, wm_i, wm_j, k, wm_nc)
// After:  bilinn(wmbuf, wm_nx, wm_ny, wm_i, wm_j, k, wm_nc)
```
Note: `wm_ny` is the watermark buffer height (local variable in `add_watermark()`).

### Step 4: Add early-return guards with logging to scaling methods

**File:** `sipi/src/SipiImage.cpp`

Add `#include "Logger.h"` at the top of the file if not already present.

Add dimension guards to prevent division by zero in LUT calculations. Include a warning log so operators can diagnose why images are served unscaled (silent failure could mask an authorization bypass if `restricted_size` is in use).

**`scaleFast()` (line 832):**
```cpp
bool SipiImage::scaleFast(size_t nnx, size_t nny)
{
  if (nnx <= 1 || nny <= 1 || nx <= 1 || ny <= 1) {
    log_warn("scaleFast: degenerate dimensions (src=%zux%zu, dst=%zux%zu), skipping", nx, ny, nnx, nny);
    return false;
  }
  // ... existing code
```

**`scaleMedium()` (line 872):**
```cpp
bool SipiImage::scaleMedium(size_t nnx, size_t nny)
{
  if (nnx <= 1 || nny <= 1 || nx <= 1 || ny <= 1) {
    log_warn("scaleMedium: degenerate dimensions (src=%zux%zu, dst=%zux%zu), skipping", nx, ny, nnx, nny);
    return false;
  }
  // ... existing code
```

**`scale()` (line 922):**
```cpp
bool SipiImage::scale(size_t nnx, size_t nny)
{
  if (nnx <= 1 || nny <= 1 || nx <= 1 || ny <= 1) {
    log_warn("scale: degenerate dimensions (src=%zux%zu, dst=%zux%zu), skipping", nx, ny, nnx, nny);
    return false;
  }
  // ... existing code
```

### Step 5: Fix `doReduce()` — guard cnt=0

**File:** `sipi/src/formats/SipiIOTiff.cpp`, line 1742

Replace:
```cpp
outbuf[nc * (y * nnx + x) + c] = tmp / cnt;
```

With:
```cpp
outbuf[nc * (y * nnx + x) + c] = (cnt > 0) ? tmp / cnt : 0;
```

This handles the edge case where all source pixels for a reduce block fall outside the image dimensions.

### Step 6: Add boundary-condition regression tests

**File:** `sipi/test/unit/sipiimage/sipiimage.cpp`

Add tests that exercise the crash scenario and edge cases. The exact test image paths and `SipiSize` syntax will be confirmed by reading the existing test setup during implementation.

```cpp
// Test 1: Scale to very small dimensions (hits LUT boundary values)
TEST(SipiImage, ScaleBoundaryDoesNotCrash)
{
  Sipi::SipiImage img;
  auto region = std::shared_ptr<Sipi::SipiRegion>();
  auto size = std::make_shared<Sipi::SipiSize>("2,2");
  EXPECT_NO_THROW(img.read(leavesSmall, region, size));
}

// Test 2: pct:1 produces very small target dimensions
TEST(SipiImage, ScaleToSmallPercentDoesNotCrash)
{
  Sipi::SipiImage img;
  auto region = std::shared_ptr<Sipi::SipiRegion>();
  auto size = std::make_shared<Sipi::SipiSize>("pct:1");
  EXPECT_NO_THROW(img.read(leavesSmall, region, size));
}

// Test 3: Upscale — LUT values at exact boundary
TEST(SipiImage, ScaleUpDoesNotCrash)
{
  Sipi::SipiImage img;
  auto region = std::shared_ptr<Sipi::SipiRegion>();
  auto size = std::make_shared<Sipi::SipiSize>("^1000,1000");
  EXPECT_NO_THROW(img.read(leavesSmall, region, size));
}

// Test 4: Non-square asymmetric scaling (y-boundary independent of x)
TEST(SipiImage, ScaleAsymmetricDoesNotCrash)
{
  Sipi::SipiImage img;
  auto region = std::shared_ptr<Sipi::SipiRegion>();
  auto size = std::make_shared<Sipi::SipiSize>("2,100");
  EXPECT_NO_THROW(img.read(leavesSmall, region, size));
}

// Test 5: Scale to 1x1 — exercises degenerate guard (returns false, no crash)
TEST(SipiImage, ScaleToOnePixelDoesNotCrash)
{
  Sipi::SipiImage img;
  auto region = std::shared_ptr<Sipi::SipiRegion>();
  auto size = std::make_shared<Sipi::SipiSize>("1,1");
  EXPECT_NO_THROW(img.read(leavesSmall, region, size));
}
```

If a 16bps test image is available in the test data, add a sixth test for the word overload path.

## Verification

1. **Build:** `nix develop` then `make nix-build` — must compile cleanly
2. **Unit tests:** `make nix-test` — all existing + new tests pass
3. **Docker build + smoke tests:** `make docker-build && make test-smoke` — no regressions
4. **Manual check:** Start server with `make nix-run`, request a JP2 image at scale `2,2` and `pct:1` — no crash, valid image returned
