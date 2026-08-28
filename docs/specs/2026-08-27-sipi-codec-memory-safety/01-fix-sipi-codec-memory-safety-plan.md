---
title: "fix: SIPI codec memory-safety, error-message, and CORS bugs"
type: fix
date: 2026-08-27
author: "Ivan Subotic"
status: draft
linear: DEV-6418
---

# fix: SIPI codec memory-safety, error-message, and CORS bugs

## Overview

Fix the SIPI-side findings under the DEV-6418 review: buffer-handling bugs in the C++
format handlers (`src/formats/*`, `src/SipiImage.cpp`), a source-path leak in error
messages, and CORS credential reflection in the Rust shell. The codec layer is the part
the Rust cutover leaves in C++ (ADR-0020), so those bugs stay relevant after the
migration; the CORS bug was *ported into* the Rust shell when the C++ oracle was removed.

The findings originate from a March 2026 code review (Linear DEV-6061..6068); a re-check
against current code on 2026-08-27 (@ `6518a5e9`) found the codebase had been refactored
in the meantime (`refactor(image): own the pixel buffer as std::vector`, `955c4506` +
the oracle removal ADR-0020), which fixed some reported items, left others, moved a few
into the Rust shell, and turned up seven more of the same kind in the same codec
functions. This plan is scoped to the current code, not the stale review line numbers.

All of it lands as **one PR with separate self-contained commits** (one commit per
concern), not a phased rollout.

**Not in this plan (resolved / dead, no work):** DEV-6072's three sub-findings are settled
by the current code — S16 pixel-limit overflow is fixed (the saturating `safe_buf` at
`SipiPeakMemory.h:54-63` is on the live decode path), S25 `/metrics` is gone (the Rust
shell serves no HTTP metrics route; export is OTLP push only, `telemetry.rs:248-266`), and
S15 XFF trust changed shape (XFF is still trusted in `routes.rs client_ip()` but the
rate-limit-bypass exploit is moot — the shell has no per-client rate limiter; the residual
spoofable `server.client_ip` is already gated by the documented "must sit behind Traefik"
assumption, and a trusted-proxy allowlist would be the kind of defense-in-depth
CONVENTIONS.md rules out). Recommend closing DEV-6072 as resolved + accepted-risk.
DEV-6069/6071/6073 (shttps SSL-TLS/JWT, path traversal, framework hardening) died with the
oracle and stay Canceled. DEV-6070/5925 (Lua hardening) are Done.

## Problem Statement / Motivation

`nx`, `ny`, `nc`, `bps` and colormap/marker contents are read straight from image file
headers and then used to size buffers and index into them, with no range check. On a
malformed file this misbehaves in the usual C ways:

- **Buffer overflows (write):** 16-bit YCbCr JP2 (`SipiImage.cpp:427`), JPEG ICC APP2
  length underflow (`SipiIOJpeg.cpp:636`), JP2 palette stride mismatch
  (`SipiIOJ2k.cpp:732`), and a TIFF `TransferFunction` call with the wrong libtiff API
  shape that overwrites a stack slot and `memcpy`s from an uninitialized pointer
  (`SipiIOTiff.cpp:1170`).
- **Buffer over-reads:** JPEG XMP marker walk on the `read_shape()`/`info.json` path
  (`SipiIOJpeg.cpp:915`), JPEG APP13 identifier compare (`SipiIOJpeg.cpp:653,981`),
  TIFF/J2K palette index used as a raw offset (`SipiIOTiff.cpp:1332`,
  `SipiIOJ2k.cpp:735`).
- **Integer overflow → undersized allocation, then a full-size write:** unchecked
  `nx*ny*nc`-style multiplies at 34 sites in `SipiImage.cpp` and four genuinely
  32-bit-wide sites in `SipiIOTiff.cpp` (`523,535,586,816`).
- **Undefined behavior:** TIFF colormap written through `reserve()`d (size 0) vectors
  (`SipiIOTiff.cpp:964`); `TIFFSetField`/`TIFFGetField` variadic argument type-width
  mismatches (several sites).
- **Error messages leak source paths:** `SipiImageError`/`shttps::Error` embed
  `std::source_location` file/line in their message strings, and the Lua upload route
  writes those into the HTTP 500 body (DEV-6062).

These trip on real malformed files, not just theoretical input, and several corrupt the
heap rather than failing cleanly. That is why they are worth fixing as a batch.

## Proposed Solution

Fix at the right boundary and once per image, not per pixel:

1. **A dimension check at each codec's read entry.** After a handler reads `nx`, `ny`,
   `nc`, `bps` from the header, validate them against hard upper bounds before any
   allocation. This is validation at the C-library boundary (style-guide §4), and it
   removes the whole integer-overflow class at the source.

2. **A shared checked-multiply helper** (`src/util/checked_arith.h`), lifted from the
   local lambda in `src/throttling/cpp/SipiPeakMemory.h:57-63` but rejecting on overflow
   (returns `std::nullopt`) rather than saturating. Used at every pixel-buffer allocation.
   34+ call sites is well past the "second real caller" bar for a shared helper.

3. **Validate-once-before-loop bounds checks** for every colormap/palette lookup and
   marker walk: compute the guard (colormap size, marker end pointer) once, before the
   tight loop. No per-pixel branch, so the decode path is untouched.

4. **Fix the allocation-vs-index arithmetic** where a buffer is sized with one channel
   count and indexed with another (16-bit YCbCr, `removeChannel`, J2K palette stride).

5. **Fix the `TIFFSetField`/`TIFFGetField` argument types** to libtiff's documented
   widths, and the `TransferFunction` read to libtiff's actual (count-less) shape.

6. **Stop leaking source paths (DEV-6062):** strip `source_location` from the
   client-visible message; keep the full string on the server-side log path (`server.log`
   for the Lua lane; Sentry on the serve lane, which already works). Fix the Lua lane and
   the one `Content-Disposition` docroot echo.

7. **A regression test per fix**, driven by committed crafted fixtures, run under ASan +
   UBSan (the CI `asan-ubsan / amd64` job gates this). TDD: the fixture reproduces the
   crash/over-read first, the fix turns it green.

Each fix follows the C++23 style guide: `std::vector`/`unique_ptr` ownership (no raw
`new[]`), `throw Sipi::SipiImageError` on the invalid-input path (the existing handler
convention — `std::expected` inside the handlers is out of scope), `const`-correctness and
`[[nodiscard]]` on the new helper.

## Alternative Approaches Considered

- **Inline overflow checks at each of the 34 sites instead of a shared helper.** 34 sites
  is past the second-caller threshold, and an inlined `__builtin_mul_overflow` chain
  repeated 34 times is more error-prone than one reviewed helper. Chose the helper.
- **Rely on the memory budget / `std::bad_alloc` to catch huge allocations.** The budget
  guard at the serve seam (`src/ffi/serve_image.cpp:605-664`) handles the large-but-valid
  case. It cannot help here: a *wrapped* multiply produces a *small* allocation the budget
  admits, and the overflow bugs write past a correctly admitted buffer. The check must sit
  at the codec, on the pre-multiply dimensions.
- **Introduce `std::expected<T,E>` in the format handlers.** The handlers uniformly
  `throw SipiImageError` today (only a TODO marker toward `std::expected` at
  `SipiIOJpeg.cpp:651`). Converting the error convention is a separate refactor with its
  own blast radius; out of scope here, keep `throw`.
- **Add a codec fuzz harness as part of this.** Worth doing (it would have caught most of
  these), but it is a net-new Bazel target, not an extension of the IIIF parser harness,
  and it wants a clean baseline. Left as a follow-up, noted at the end.

## Technical Considerations

**Status of the reported items against current code (`6518a5e9`).**

| Linear | Finding | Status now | Current location |
|--------|---------|-----------|------------------|
| DEV-6063 | Unchecked `nx*ny*nc*sizeof` (~18 sites) | **Present** — 34 sites; refactor changed the mechanism (`std::vector`) not the arithmetic | `src/SipiImage.cpp` |
| DEV-6063 | PNG alloc multiply (~266) | **Fixed** (moved to `std::vector`, `955c4506`) | `SipiIOPng.cpp:284` |
| DEV-6063 | JPEG alloc multiply (~750) | **Fixed** (moved to `std::vector`) | `SipiIOJpeg.cpp:727` |
| DEV-6064 | Missing `break` in switch | **Fixed** (`c45145cf`, was a `bps` switch) | `SipiIOTiff.cpp:1276` |
| DEV-6064 | Colormap `reserve()` vs `resize()` | **Present** | `SipiIOTiff.cpp:964-985` |
| DEV-6065 | TIFF palette index unchecked | **Present** | `SipiIOTiff.cpp:1316-1344` |
| DEV-6065 | J2K palette index unchecked | **Present** (OOB read) | `SipiIOJ2k.cpp:735-737` |
| DEV-6066 | JPEG XMP walk `read()` | **Fixed** | `SipiIOJpeg.cpp:618-631` |
| DEV-6066 | JPEG XMP walk `read_shape()` | **Present** | `SipiIOJpeg.cpp:915-942` |
| DEV-6066 | `parse_photoshop` bounds + calloc leak | **Fixed** | `SipiIOJpeg.cpp:375-444` |
| DEV-6067 | `TIFFSetField` width mismatches | **Present** (several sites) | `SipiIOTiff.cpp:1629,1675,1741,928,933,991,1802,1813,1825` |
| DEV-6067 | Uninitialized `int i` in `readExif` | **Fixed / not reproducible** (init in `for`) | `SipiIOTiff.cpp:2008` |
| DEV-6068 | 16-bit YCbCr under-allocation | **Present** — heap write | `SipiImage.cpp:424-449` |

**Found during the re-check, same functions, not in the original review.** Each has a
Linear issue under DEV-6418 (DEV-7049..7055):

| # | Finding | Location | Issue |
|---|---------|----------|-------|
| N1 | JPEG ICC APP2 length underflow → heap overflow write | `SipiIOJpeg.cpp:636-645` | DEV-7049 |
| N2 | JP2 palette `tmpbuf` stride-3 vs `numcol` → heap overflow write | `SipiIOJ2k.cpp:732-737` | DEV-7050 |
| N3 | TIFF `TransferFunction` read wrong API shape → stack overwrite + uninit-ptr `memcpy` | `SipiIOTiff.cpp:1170-1192` | DEV-7051 |
| N4 | JPEG APP13 `strncmp` 14-byte over-read (both paths) | `SipiIOJpeg.cpp:653,981` | DEV-7052 |
| N5 | `removeChannel` OOB write when removing a non-terminal channel | `SipiImage.cpp:598-613` | DEV-7053 |
| N6 | TIFF colormap `colmap_len = 2^bps` computed in `int` with unbounded `bps` | `SipiIOTiff.cpp:972-976` | DEV-7054 |
| N7 | PNG 16-bit byte-swap loop uses `int` counter over a `size_t` product | `SipiIOPng.cpp:300` | DEV-7055 |

All 13 codec/error findings, plus the CORS fix below, are in scope for this PR.

**HTTP-layer finding in the Rust shell — DEV-6061 (CORS), verified present 2026-08-27.**
The "reflect any `Origin` + `Access-Control-Allow-Credentials: true`" pattern the review
flagged in the deleted C++ `Connection.cpp` was ported into the axum shell, in four spots
in `src/server-rs/src/routes.rs`: knora.json (origin derivation `1802-1804` → credentials
in `json_response` `1876-1886`), the image/`/file` stream success path (`330-338`), the
OPTIONS preflight `cors_preflight_response` (`859-869`), and the static fileserver
`push_cors` (`1662-1669`, ACAO only). There is no allowlist and no config key for one
(`config_file.rs`/`config.rs` have none). The `info.json` path is safe and stays as-is: it
sends `Access-Control-Allow-Origin: *` with no credentials (anonymous CORS,
`routes.rs:1770-1771,1881-1882`). **Design decision (Ivan): the fix is opt-in, not a
default behavior change.** With `SIPI_ALLOWED_ORIGINS` unset/empty, SIPI keeps today's
reflect-any-origin behavior byte-for-byte (so the three existing CORS tests at
`routes.rs:2040-2116` stay valid as the default contract); setting the list restricts to
those origins. Consequence: the reflect-with-credentials exposure remains the *default*
posture — a SIPI instance is hardened only once an operator configures the allowlist. DSP
is hardened by setting the var in ops-deploy.

**Boundary discipline.** Handlers receive *file bytes*, not HTTP input, so codec-internal
bounds/overflow checks against malformed image data are validation at the C-library
boundary (style-guide §4), not redundant defense-in-depth behind the HTTP handler.

**Hot-path / benchmark rule.** All fixes are validate-once-before-loop or allocation-size
corrections; none adds a per-pixel branch. Run `just bench` + `just bench-compare` on the
`src/formats` decode/encode benchmarks to confirm no regression; no measurable delta is
expected.

**Approval-test determinism.** These fixes touch decode-side bounds and error paths, not
encode output bytes, so the byte-exact goldens should not move. If a golden diffs for a
*well-formed* input, that is a signal the fix changed decode behavior — investigate before
re-approving. ASan runs `-c dbg` and does not run approval tests, so the two gates are
independent (`ci.yml:279-282`).

**Error model.** Keep `throw Sipi::SipiImageError(...)` on the invalid-input paths
(`src/SipiImageError.h`). DEV-6062 changes what reaches the *client*, not the exception
type: strip `source_location` from the client-visible string.

**Where the full string still goes (DEV-6062).** The serve lane captures handled image
errors to Sentry via `report_image_error` (`src/server-rs/src/ffi.rs:994-1084`). The Lua
lane does **not** go through that struct — `image_handle.cpp` emits the string as an
ordinary Lua failure value (`engine_ffi.rs` → `bindings/image.rs:203` `fail(...)`), so on
the upload route the full string's only server-side record is `upload.lua`'s own
`server.log(...)` calls (`:94,111,146`), which bridge into tracing
(`bindings/server.rs:1263`). Keep those `server.log` calls; do not claim the Lua lane
reaches Sentry (wiring it there is a separate follow-up if ever wanted). DSP runs no SIPI
Lua routes (uploads go via dsp-ingest), so the upload-route leak is a generic-SIPI
exposure, not a DSP one; fix it at the error classes so every consumer benefits.

## Implementation

One PR, separate commits below. Commit scope is the concern: `image` for
`src/SipiImage.*`, `formats` for `src/formats/*`, `util` for the helper, and the DEV-6062
work spans the error classes + `server-rs`. Order matters only in that C1 lands first (the
others use its helpers).

#### C1 — `feat(util)`: add checked-multiply + dimension-validation helpers and fixture tooling

- [x] Add `src/util/checked_arith.h`: `[[nodiscard]] constexpr std::optional<size_t> checked_buf_size(size_t nx, size_t ny, size_t nc, size_t elem)` using chained `__builtin_mul_overflow` (the form proposed in DEV-6063), returning `std::nullopt` on overflow. Header-only. Note in the header that this *rejects* on overflow, unlike the *saturate-to-`SIZE_MAX`* `safe_buf` lambda at `SipiPeakMemory.h:56-63` (which saturates deliberately so a wrapped estimate still fails the budget check — the wrong behavior for a throw-before-allocate gate)
- [x] Add `test/unit/util/checked_arith_test.cpp` (+ `test/unit/util/BUILD.bazel` `cc_test`) covering wrap, zero, and identity cases, asserting `nullopt` (not a clamped value) on overflow — landed co-located at `src/util/checked_arith_test.cpp` (`//src/util:checked_arith_test`) per ADR-0003 co-location and the existing `util_test` precedent
- [x] Add `validate_decode_dims(nx, ny, nc, bps, filepath)` (in `src/formats/` shared header or `SipiIO.h`) that throws `SipiImageError` when any dimension exceeds a named `constexpr` cap (`nx,ny <= 2^17`, `nc <= 32`, `bps ∈ {1,4,8,12,16}`) — landed in `src/SipiIO.h`
- [x] Add the malformed-fixture generator + `README.md` under `test/_test_data/images/malformed/` (extends the existing fixture tooling; README documents each fixture's defect and the `git lfs pull` requirement). Individual fixtures are added by the commit that uses them.

#### C2 — `fix(image)`: guard pixel-buffer allocations against integer overflow (DEV-6063)

- [x] Route every pixel-buffer allocation in `src/SipiImage.cpp` (34 sites across copy ctor, param ctor, `operator=`, `convertYCC2RGB`, `convertToIcc`, `removeChannel`, `crop`×2, `scaleFast`, `scaleMedium`, `scale`, `rotate`×5 modes, `to8bps`, `toBitonal`, `operator+=`/`-=`) through `checked_buf_size(...)`, throwing `SipiImageError` on overflow — 37 sites via a private `checked_buf_size_or_throw` wrapper
- [x] Guard the two length-only multiplies at `SipiImage.cpp:301,328` (pixel-hash `add_data`)
- [x] Fix the signed/unsigned `size_t` hazard in `crop(int x, int y, size_t w, size_t h)` (`SipiImage.cpp:696-720`): validate `x`, `y` before combining with `w`, `h`
- [x] Add `test/unit/sipiimage/overflow_regression_test.cpp` with an oversized-dimensions fixture, asserting a thrown `SipiImageError` (not a crash) under ASan — picked up via the existing `glob(["*.cpp"])` in the sipiimage BUILD; local gate non-sanitized (asserts thrown exception), ASan deferred to Linux CI

#### C3 — `fix(image)`: correct channel-count buffer sizing and indexing (DEV-6068, N5)

- [x] `[DEV-6068]` Fix `convertYCC2RGB` 16-bit path (`SipiImage.cpp:424-449`): size `outbuf_v` with `nc` to match the 8-bit path at line 402 (which correctly uses `nc`); ensure the `pixels`/`nc` invariant holds after the `std::move`
- [x] `[N5]` Fix `removeChannel` / `purge_channel_pixels` (`SipiImage.cpp:598-613`): the OOB write and channel-compaction when the removed channel is not the last (logic bug; the test constructs the multi-extra-sample image directly, no fixture)
- [x] Add `ConvertYcc16BitDoesNotOverflow` and `RemoveNonTerminalChannel` regression tests (model on `Scale*DoesNotCrash` / `Tiff1BitRoiDoesNotCorruptMemory`), failing under ASan before the fix
- [ ] `[DEV-6068]` Add a JP2 16-bit YCbCr `read()` regression test so `convertYCC2RGB` is actually exercised (today only `DISABLED_JpegYcckColorspaceReads` exists and nothing hits this path); use the 16-bit YCbCr JP2 fixture — **DEFERRED** (needs license-gated Kakadu encode tooling for the fixture; see journal Deferrals; direct in-code `ConvertYcc16BitDoesNotOverflow` covers the fix)
- [ ] Note: `convertYCC2RGB` and `removeChannel` are also touched in C2; rebase C3 on C2 so the edits don't conflict

#### C4 — `fix(formats)`: TIFF buffer and libtiff-field bugs (DEV-6064, DEV-6065, DEV-6067, N3, N6)

- [x] Call `validate_decode_dims` at TIFF read entry, right after `nx`/`ny`/`nc`/`bps` are read (`SipiIOTiff.cpp:928-941`)
- [x] `[DEV-6064]` Replace `reserve()` with `resize()` for `rcm`/`gcm`/`bcm` colormap vectors (`SipiIOTiff.cpp:978-980`)
- [x] `[N6]` Bound `colmap_len`: validate `bps` before computing `2^bps`, reject `bps` outside the supported set, compute in `size_t`
- [x] `[DEV-6065]` Bounds-check the palette lookup (`SipiIOTiff.cpp:1332-1341`): validate `img->pixels[i]` against `colmap_len` before the loop
- [x] `[N3]` Rewrite the `TransferFunction` read (`SipiIOTiff.cpp:1170-1192`). libtiff's `TIFFTAG_TRANSFERFUNCTION` returns **no count** — it writes one `uint16_t*` for grayscale (`spp - extrasamples == 1`) or three for color, each pointing to a `1 << bps`-entry table. The current code passes `&tfunc_len_ti` (an `unsigned int*`) as the first vararg, so libtiff writes an 8-byte pointer into a 4-byte slot (stack overwrite) and the invented "length" logic is meaningless. Replace it: pick channel count from `spp - extrasamples`, call `TIFFGetField` with that many `uint16_t*` out-pointers (init them), copy `1 << bps` shorts per channel into `tfunc` (buffer is already sized `3 * (1 << bps)`), replicating the single grayscale table across all three channels as today. Set `tfunc_len = 1 << bps`. Drop `tfunc_len_ti` entirely
- [x] `[DEV-6067]` Correct `TIFFSetField` widths: `SAMPLESPERPIXEL` (`1629,1741`)→`uint16_t`; `IMAGEWIDTH`/`IMAGELENGTH` (`1675,1676`)→`uint32_t`; custom-tag counts `ICCPROFILE`/`RICHTIFFIPTC`/`XMLPACKET` (`1802,1813,1825`)→`uint32_t`; fix the `if (!buf.empty() > 0)` bug at `1825`
- [x] `[DEV-6067]` Correct `TIFFGetField` out-params: `IMAGEWIDTH`/`IMAGELENGTH` (`928,933`) via a local `uint32_t`; `EXTRASAMPLES` count (`991`)→`uint16_t`; `XMLPACKET` length (`1108`)→`unsigned int`; `RESOLUTIONUNIT` (`1078`)→`uint16_t` — **RESOLUTIONUNIT kept `short`** (see journal: `uint16_t` shifted 2 approval goldens via an Exiv2 overload; same 16-bit width, no safety difference)
- [x] `[DEV-6063]` Route the four 32-bit TIFF multiplies (`523,535,586,816`) and the two unbounded 64-bit ones (`1290,1327`) through `checked_buf_size`
- [x] `[N3]` Add `static_assert(std::is_array_v<T>)` (or rename) to the `separateToContig` unique_ptr overload (`SipiIOTiff.cpp:520-530`)
- [x] Add TIFF **decode-path** regression tests (`img->read()` only, no JPX write, so unaffected by the `PALETTE_Conversion` Kakadu-encoder SIGILL): sub-8-bit palette with out-of-range index (DEV-6065), out-of-range-`bps` colormap (N6), `TransferFunction` TIFF (N3), oversized-dimension TIFF (DEV-6063)

#### C5 — `fix(formats)`: JPEG marker-parsing over-reads (DEV-6066, N1, N4)

- [x] Call `validate_decode_dims` at JPEG read entry after the dimensions are known
- [x] `[DEV-6066]` Rewrite the `read_shape()` XMP marker walk (`SipiIOJpeg.cpp:915-942`) to the bounded extraction used in `read()` (`618-631`): compute `data_end`, bound every pointer advance, fix the `break` that drops later markers — malformed-XMP now downgrades to `log_warn` matching `read()` (no test relied on the old throw)
- [x] `[N1]` Fix the ICC APP2 length underflow (`SipiIOJpeg.cpp:636-645`): reject `len < 0` / require `data_length >= offset + 14` before `realloc`/`memcpy`; unsigned length arithmetic
- [x] `[N4]` Guard the APP13 identifier compare (`SipiIOJpeg.cpp:653,981`) with `marker->data_length >= 14` before `strncmp`, mirroring the APP14 guard at `672`
- [x] `[N1]` Tighten the residual `parse_photoshop` pointer arithmetic (`ptr + datalen > end` → `datalen > (size_t)(end - ptr)`)
- [x] Add JPEG regression tests exercising `read_shape()` (not just `read()`) with truncated-XMP, short-APP2, and short-APP13 fixtures, under ASan — ICC-underflow fixture is exercised via the full `read()` path (read_shape never processed APP2/ICC)

#### C6 — `fix(formats)`: J2K palette expansion (DEV-6065, N2)

- [x] Call `validate_decode_dims` at J2K read entry after the dimensions are known
- [x] `[N2]` Fix the `tmpbuf` heap overflow write (`SipiIOJ2k.cpp:732-737`): size the buffer with the actual output stride; reconcile `numcol` vs the hardcoded `3` (reject or handle `numcol != 3`)
- [x] `[DEV-6065]` Bounds-check the palette lookup (`SipiIOJ2k.cpp:735-737`): validate `img->pixels[...]` against `nentries`; validate `nentries >= 2^bps` once
- [x] `[N2]` Handle `bps == 16` / `nc != 1` palette inputs correctly (current code assumes 8-bit single-component) or reject them with `SipiImageError` — reject via `validate_j2k_palette_mapping`
- [x] `[N2]` Initialize `numcol` at declaration (`SipiIOJ2k.cpp:508`)
- [x] Add a J2K palette regression test (no J2K entry exists in `format_error_path_test.cpp` today) with stride-mismatch and out-of-range-index fixtures, under ASan — landed as a **fixture-free** direct test of `validate_j2k_palette_mapping` (all rejection + accept paths); on-disk JP2 fixture **DEFERRED** (no palette-encode path without license-gated Kakadu; see journal Deferrals)

#### C7 — `fix(formats)`: PNG 16-bit byte-swap loop counter (N7)

- [x] `[N7]` Change the 16-bit byte-swap loop counter (`SipiIOPng.cpp:300`) from `int` to `size_t`
- [x] Call `validate_decode_dims` at PNG read entry for consistency — called post-`png_read_image` (nc/bps are only known after the libpng read), still before any buffer indexing
- [x] No fixture (a `> INT_MAX`-sample PNG is impractically large); covered by UBSan + review — the one fix without a reproducing fixture

#### C8 — `fix(observability)`: stop leaking source paths in client error messages (DEV-6062)

- [x] Split `SipiImageError` (`src/SipiImageError.h:86-96`) and `shttps::Error` (`src/util/Error.cpp:15-37`) into a client-safe `message()` (no file/line) and a `diagnostic()` / existing `to_string()` for logs; `what()` keeps full detail for logs
- [x] Update the Lua lane (`src/ffi/image_handle.cpp`, the 12 `emit_str(err, ..., e.to_string())` sites) to emit the client-safe message; the full string continues to `server.log` — 14 sites converted
- [x] Route the generic `catch (const std::exception &e) { emit_str(err, err_ctx, e.what()); }` site (`src/ffi/image_handle.cpp:136`) through the same client-safe path (`std::filesystem`/`std::system_error` `what()` embeds the offending path)
- [x] Fix `scripts/upload.lua` (`:54,63,95,112,137,147`) so `send_error` sends a generic string; keep the existing `server.log(...)` of the full detail (`:94,111,146`) — same fix also applied to the e2e fixture fork `test/_test_data/scripts/upload.lua` (see journal side finding)
- [x] Fix the `Content-Disposition` docroot echo on the `/server` static route (`src/server-rs/src/routes.rs:1572`, whose `// hardening is deferred` comment at 1568-1569 marks it): pass the **basename** of `infile` (not the full docroot path) through the existing `content_disposition(identifier: &str) -> Option<HeaderValue>` helper (`routes.rs:1957`), which already strips CR/LF/NUL/control/DEL and RFC 2616/6266-escapes; handle its `None` by omitting the header. This both stops the docroot-path leak and closes the header-injection surface on the attacker-influenced filename, and matches how the IIIF `/file` route builds the header — deferred-hardening comment removed
- [x] Extend the existing `upload_corrupt_image_reports_500_json` e2e test (`test/e2e/tests/upload.rs:335`) to assert the `message` body contains no `src/` / absolute-path / `.cpp:` substring
- [x] Confirm the IIIF serve lane stays bare-status/empty-body (no regression to `src/server-rs/src/sink.rs:344-350`)

#### C9 — `fix(server-rs)`: restrict CORS to a configured origin allowlist (DEV-6061)

- [x] Add a `SIPI_ALLOWED_ORIGINS` config knob as a **new clap/env arg** in `src/server-rs/src/config.rs` (the `ServerOverrides` env-override surface — matching how ops-deploy injects every other Rust-shell knob, `SIPI_NTHREADS`/`SIPI_MEMORY_LIMIT`/etc. via env, NOT a mounted TOML file), parsed as a comma-separated list into `Vec<String>`, threaded to the router/handlers. CORS is shell-only; it never crosses the FFI seam into the engine — landed **env-only** (`config::allowed_origins_from_env`, read in `serve()` like `SIPI_RS_PORT`), NOT a `ServerOverrides` field / CLI flag, to avoid touching cli-rs's exhaustive `From<&ServerArgs>` (out of scope); still env-injectable by ops-deploy. See journal side finding
- [x] **Default (unset/empty) preserves today's behavior exactly: reflect any request `Origin` + `Access-Control-Allow-Credentials: true`.** This is opt-in hardening, not a default behavior change — a SIPI instance is only restricted once an operator sets the list
- [x] Add one shared helper (e.g. `cors_allow(origin: Option<&str>, &allowlist) -> CorsDecision`) and route all four current sites through it — knora.json (`routes.rs:1802-1804` + `json_response` `1876-1886`), image/`/file` stream (`330-338`), `cors_preflight_response` (`859-869`), and `push_cors` (`1662-1669`). When the allowlist is **empty**: reflect the origin + credentials (unchanged). When **non-empty**: echo `Access-Control-Allow-Origin` + credentials only for a listed origin, emit no CORS headers otherwise. Four identical checks justify the one helper (same reasoning as `checked_buf_size`)
- [x] In allowlist mode only, emit `Vary: Origin` on responses whose `Access-Control-Allow-Origin` is chosen per request Origin, so shared caches don't serve one origin's ACAO to another (`Vary` is emitted nowhere in `server-rs` today; leave the empty-allowlist reflect path byte-identical to today, i.e. no new `Vary`)
- [x] Leave the `info.json` anonymous path unchanged in both modes: `Access-Control-Allow-Origin: *`, no credentials (`routes.rs:1770-1771`)
- [x] Keep the three existing CORS tests (`routes.rs:2040-2116`) as the **empty-allowlist / default** contract (they document the reflect behavior — no inversion), and **add** allowlist-mode tests: listed origin → echoed + credentials + `Vary: Origin`; unlisted origin → no CORS headers
- [x] `just bazel-rustfmt-check` + `just bazel-clippy-check` (Rust change), and an e2e or route-level test for the allowlist match/deny paths

### Before the PR merges (run once, not a commit)

- [ ] Diff every touched allocation site in `SipiImage.cpp` and `SipiIOTiff.cpp` against the Technical Considerations site enumeration (34 + 4 + 2); confirm none missed and none added un-reviewed
- [ ] `just bazel-test-sanitized --config=asan --config=ubsan` green across unit + e2e with every new fixture exercised
- [ ] `just bazel-rustfmt-check` and `just bazel-clippy-check` pass (C8 touches `server-rs`)
- [ ] `just bench` + `just bench-compare` on `decode_benchmark`/`encode_benchmark`, no regression
- [ ] Approval tests re-run; investigate any well-formed-input golden move before re-approving
- [ ] Follow-up (not this PR): a native C++ `cc_fuzz_test` codec harness over `SipiIOTiff/Jpeg/Png::read`/`read_shape`, reusing `--config=fuzz` and the `fuzz.yml` pattern, seeded from these fixtures; BCR codecs first, Kakadu later (license-gated)

## Acceptance Criteria

- [ ] Every item marked **Present** in the status table is fixed (or reclassified with justification in the PR)
- [ ] N1–N7 are fixed; DEV-7049..7055 closed with their fixing commit linked
- [ ] A committed fixture reproduces each fixed defect (crash / ASan report / over-read) before the fix and passes after — except N7 (UBSan + review)
- [ ] `validate_decode_dims` is called at the read entry of every handler that reads dimensions from a header (TIFF, JPEG, J2K, PNG)
- [ ] `checked_arith_test.cpp` exists and asserts `nullopt` (reject, not saturate) on overflow
- [ ] The pre-merge call-site reconciliation was performed and recorded
- [ ] The ASan + UBSan CI job is green with the new fixtures in the suite
- [ ] `just bazel-rustfmt-check` and `just bazel-clippy-check` pass
- [ ] No approval-test golden moved for well-formed input (or any move is investigated and justified)
- [ ] `bench-compare` shows no regression on the `src/formats` benchmarks
- [ ] A 500 error body on the upload route contains no source path, `.cpp` filename, or line number; the full detail still reaches `server.log`
- [ ] CORS: with `SIPI_ALLOWED_ORIGINS` unset/empty, behavior is byte-identical to today (reflect any Origin + credentials; existing tests still pass); with a configured list, only listed origins are echoed (+ credentials + `Vary: Origin`) and unlisted origins get no CORS headers; `info.json` still sends `*` with no credentials in both modes
- [ ] Each Linear child (DEV-6061, DEV-6062..6068, DEV-7049..7055) is closed with its fixing commit linked; DEV-6072 is closed as resolved (S16/S25) + accepted-risk (S15) with a note, not a code change

## Dependencies & Risks

- **No new external dependency.** The helper is header-only; fixtures use the existing
  fixture tooling.
- **Kakadu license** gates local building of the JP2 path; the J2K fix compiles for anyone
  with org access, and CI has it.
- **Approval-golden fragility** if a fix changes decode output — mitigated by the
  investigate-before-re-approve gate and by keeping fixes on the error/bounds path.
- **macOS local ASan link is broken** (prior learning); use the Linux CI `asan-ubsan /
  amd64` job for sanitizer verification, not local macOS runs.
- **Fixtures are Git LFS**; a fresh worktree needs `git lfs pull` or image tests fail with
  cryptic 500s. Documented in the fixture `README.md`.
- **CORS is opt-in, so no deployment breaks on rollout.** Default (unset `SIPI_ALLOWED_ORIGINS`)
  = today's reflect behavior, so shipping C9 changes nothing until an operator sets the
  list. The trade-off is that the reflect-with-credentials exposure stays the default; a
  SIPI instance (DSP included) is hardened only when the var is set. ops-deploy PR #1420
  wires the env var through (default empty); to harden DSP, set
  `DSP_IIIF_ALLOWED_ORIGINS` to the app origin(s) there. Anonymous `info.json` (`*`, no
  credentials) is unaffected in every mode.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| A fix changes decode output and silently breaks a golden | M | M | Approval re-run in CI; investigate any well-formed-input diff before re-approving |
| A bounds check lands in a per-pixel loop and regresses throughput | L | M | Design is validate-once-before-loop; `bench-compare` confirms |
| The dimension cap rejects a legitimate very-large image | L | M | Caps are generous (2^17/side ≈ 17 GP), documented, named constants easy to raise |
| A missed call site leaves one overflow path live | M | H | Enumerated 34+4+2 sites; ASan fixtures per class; pre-merge reconciliation against the table |
| DEV-6062 change drops a useful server-side log | L | M | Full string still flows to `server.log` (Lua lane) / Sentry (serve lane); C8 keeps `upload.lua`'s `server.log` calls; e2e asserts only the client body is clean |

## Success Metrics

- Zero ASan/UBSan findings on the full fixture corpus.
- All 13 Linear children closed, each with a linked regression test.
- No throughput regression on `decode_benchmark`/`encode_benchmark`.
- No source-path substring in any error response body (asserted in e2e).

## References

- Linear: DEV-6418 (parent), DEV-6061 + DEV-6062..6068 and DEV-7049..7055 (children in scope); DEV-6072 closed as resolved/accepted-risk; DEV-6069/6071/6073 Canceled (shttps, dead with the oracle); DEV-6070/5925 Done
- CORS (DEV-6061) current sites: `src/server-rs/src/routes.rs` knora.json `1802-1804` + `json_response` `1876-1886`, image/file `330-338`, preflight `859-869`, `push_cors` `1662-1669`; safe info.json path `1770-1771,1881-1882`; tests to invert `2040-2116`; config home `src/server-rs/src/config_file.rs`
- DEV-6072 resolution evidence: `SipiPeakMemory.h:54-63` (S16 saturating), `telemetry.rs:248-266` + router `lib.rs:358-418` (S25 no HTTP metrics), `routes.rs:1937-1950` `client_ip` (S15 XFF, Traefik-gated)
- Closest precedent (format-handler fix, TDD phasing): `docs/specs/2026-03-23-sipi-jpeg-crash-fix/01-fix-sipi-jpeg-crash-plan.md`
- OOB-read precedent + multi-buffer dimension rule origin: `docs/specs/2026-03-03-sipi-bilinn-segfault/01-sipi-bilinn-segfault-PRD.md`
- Error handling + C-boundary safety: `docs/src/development/cpp-style-guide.md:561-585,684-769`; `CONVENTIONS.md:239-246`
- Review checklist: `REVIEW.md:10-71`
- Checked-multiply lambda to lift: `src/throttling/cpp/SipiPeakMemory.h:57-63`
- Memory budget / RAII guard (context): `src/throttling/cpp/SipiMemoryBudget.h:49-121`
- DEV-6062 server-side logging: serve-lane Sentry `src/server-rs/src/ffi.rs:994-1084`; Lua-lane `server.log`→tracing `src/scripting/rust/bindings/server.rs:1263`, `scripts/upload.lua:94,111,146`; bare client response `src/server-rs/src/sink.rs:344-350`
- Sanitizer CI job: `.github/workflows/ci.yml:51-86,279-314`
- Approval determinism: `docs/adr/0002-icc-profile-determinism-test-only.md`
- Fuzzing infrastructure (basis for the follow-up harness): `docs/src/development/fuzzing.md`
