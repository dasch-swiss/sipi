---
title: "Fix SIPI segfault in bilinear interpolation during image scaling"
date: 2026-03-03
author: "Ivan Subotic"
status: draft
repositories:
  - sipi
---

# Fix SIPI segfault in bilinear interpolation during image scaling

## Context

Sentry issue **SIPI-E** (`sentry__unwind_stack_libbacktrace`) reports a recurring SIGSEGV in Sipi's image scaling pipeline. The crash occurs in `SipiImage::bilinn()` during bilinear interpolation of JPEG2000 images on `dsp-stage-01` (release v3.17.1). The related issue **SIPI-A** has accumulated 1,185+ crash events since 2024-11-28.

The correctly symbolized stack trace from SIPI-E shows:

```
SIGSEGV: Segfault
  at SipiImage::bilinn          (SipiImage.cpp:781)
  at SipiImage::scale           (SipiImage.cpp:962)
  at SipiIOJ2k::read            (SipiIOJ2k.cpp:621)
  at SipiImage::read            (SipiImage.cpp:233)
  at Sipi::serve_iiif           (SipiHttpServer.cpp:1460)
  at Sipi::iiif_handler         (SipiHttpServer.cpp:1668)
  at shttps::Server::process_request (Server.cpp:1199)
```

A codebase audit revealed this is not an isolated bug but a **class of boundary-access vulnerabilities** affecting multiple methods in Sipi's image processing pipeline.

## Problem

### Root Cause

The `bilinn()` function performs bilinear interpolation by accessing the four neighboring pixels around a coordinate `(x, y)`:

```cpp
#define POSITION(x, y, c, n) ((n) * ((y)*nx + (x)) + c)

// Accesses: (ix, iy), (ix+1, iy), (ix, iy+1), (ix+1, iy+1)
```

The `scale()` method generates lookup tables (LUTs) that map output pixel positions to input coordinates in the range `[0, nx-1]` and `[0, ny-1]`:

```cpp
xlut[i] = (double)(i * (nx - 1)) / (double)(nnnx - 1);
ylut[j] = (double)(j * (ny - 1)) / (double)(nnny - 1);
```

When the LUT produces a value at or near `nx-1`, `bilinn()` casts it to integer `ix = nx-1` and attempts to read `ix+1 = nx` — **one element past the end of the buffer**. The threshold check (`rx < 1.0e-2`) was intended to catch the exact boundary case, but floating-point rounding causes the fractional remainder `rx` to exceed the threshold even when `ix` is at the boundary.

### Affected Locations

| Location | File:Line | Bug | Severity |
|----------|-----------|-----|----------|
| `bilinn()` byte overload | SipiImage.cpp:781-796 | OOB read at ix+1/iy+1 | CRITICAL |
| `bilinn()` word overload | SipiImage.cpp:810-826 | OOB read at ix+1/iy+1 | CRITICAL |
| `scale()` LUT division | SipiImage.cpp:950-951 | Division by zero when nnnx=1 or nnny=1 | CRITICAL |
| `scaleFast()` LUT division | SipiImage.cpp:837-838 | Division by zero when nnx=1 or nny=1 | CRITICAL |
| `scaleMedium()` LUT division | SipiImage.cpp:877-878 | Division by zero when nnx=1 or nny=1 | CRITICAL |
| `add_watermark()` | SipiImage.cpp:1408,1411 | Passes unclamped coords to bilinn() | CRITICAL |
| `doReduce()` template | SipiIOTiff.cpp:1742 | Division by cnt=0 at image edges | HIGH |

### Comparison with Safe Code

The `rotate()` method (SipiImage.cpp:1233) already has a correct boundary guard:

```cpp
if ((rx < 0.0) || (rx >= (double)(nx - 1)) || (ry < 0.0) || (ry >= (double)(ny - 1))) {
    // fill with background color
} else {
    // safe to call bilinn()
}
```

The `scaleFast()` method uses direct indexing without interpolation and is safe. The `crop()` methods use pre-validated bounds and are safe.

## Goals

1. Eliminate the SIGSEGV crash in `bilinn()` that produces Sentry issues SIPI-E and SIPI-A
2. Fix all related boundary-access and division-by-zero vulnerabilities found during the audit
3. Add regression tests to prevent reintroduction of boundary bugs in the scaling pipeline
4. Preserve image quality — the fix must not alter the visual output for well-formed inputs

## Non-Goals

- Refactoring the scaling pipeline architecture (e.g., replacing raw pointer arithmetic with safer abstractions)
- Modernizing memory management (e.g., replacing `new[]`/`delete[]` with `std::vector`)
- Adding IIIF-level input validation for size parameters (separate concern)

## Constraints

- C++23 codebase, compiled with GCC >= 13.0 or Clang >= 15.0
- Build via Nix development shell (`nix develop` + `make nix-build`)
- Tests: GoogleTest unit tests (`make nix-test`), Python e2e tests (`make nix-test-e2e`), Docker smoke tests (`make test-smoke`)
- Changes must not break existing test suites or alter behavior for valid inputs

## Success Criteria

- [ ] Sentry issues SIPI-E and SIPI-A stop receiving new events after deployment
- [ ] All existing unit tests pass without modification
- [ ] New boundary-condition tests cover the crash scenario and edge cases (1-pixel dimensions, exact boundary coordinates)
- [ ] Docker smoke tests pass
