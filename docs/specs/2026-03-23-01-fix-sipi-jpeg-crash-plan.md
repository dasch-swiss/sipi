---
title: "fix: C++ exception-through-C UB in image format handlers (SIPI-G/SIPI-H)"
type: fix
date: 2026-03-23
author: "Ivan Subotic"
status: reviewed
repositories:
  - sipi
linear: DEV-6132
---

# fix: C++ exception-through-C UB in image format handlers (SIPI-G/SIPI-H)

## Overview

Fix a **class of bugs** across Sipi's image format handlers where C++ exceptions propagate through C library stack frames — undefined behavior that causes SIGABRT crashes in production. The trigger was Sentry issues SIPI-G/SIPI-H (JPEG write path), but the same pattern exists in the PNG and TIFF handlers.

**Scope:**
- **JPEG** (`SipiIOJpeg.cpp`): Three interrelated bugs — incorrect `jpeg_finish_compress` on error paths, uncaught exceptions in libjpeg C callbacks, and missing server-level catch-all
- **PNG** (`SipiIOPng.cpp`): `sipi_error_fn` throws `SipiError` through libpng's C frames — identical UB. libpng provides `png_jmpbuf()` which the code does not use
- **TIFF** (`SipiIOTiff.cpp`): `memTiffWriteProc`/`memTiffSeekProc` throw `SipiImageError` through libtiff's C callbacks on `realloc` failure
- **Server** (`Server.cpp`): Missing catch-all lets any of the above escape to `std::terminate`, crashing the whole process
- **J2K**: Safe — Kakadu is C++, no C boundary crossing

## Problem Statement / Motivation

**Production impact:** SIPI-G/H appeared on `dsp-prod-01` (v3.18.0) on 2026-03-23. While only 1 event so far, the crash **terminates the entire server process** — any unhandled exception in a request thread kills all in-flight requests.

**Three bugs compound into the crash:**

1. **Bug 1 — `jpeg_finish_compress` on error paths** (`SipiIOJpeg.cpp:1189,1217,1239,1282`): When `jpeg_start_compress` or `jpeg_write_marker` fails, the error handler calls `jpeg_finish_compress(&cinfo)`. The compressor is in `CSTATE_START` (state 100), but `jpeg_finish_compress` expects `CSTATE_WRCOEFS`. This triggers libjpeg's error_exit again, throwing a second `JpegError` during stack unwinding → `std::terminate`.

2. **Bug 2 — Uncaught exceptions in libjpeg C callbacks** (`SipiIOJpeg.cpp:305-318`): The `empty_html_buffer` callback only catches `int` exceptions from `Connection::sendAndFlush()`. But `sendAndFlush` also throws `shttps::Error` (e.g., "Sending data already terminated!"). C++ exceptions propagating through libjpeg's C stack frames is **undefined behavior** — corrupts the stack, causing SIGSEGV or unpredictable crashes.

3. **Bug 3 — Missing catch-all in server request chain** (`Server.cpp:689,1280`): `process_request` catches `InputFailure` (int) and `shttps::Error&` but not `std::exception`. `socket_request_processor` has no try/catch at all. `JpegError` (inherits `std::runtime_error`) and `SipiImageError` (inherits `std::exception` directly) both escape to `std::terminate`.

**Exception hierarchy** (verified from source — `shttps::Error` inherits `std::runtime_error` at `shttps/Error.h:26`):

```
std::exception
  └── std::runtime_error
        ├── shttps::Error        ← caught by process_request
        │     └── Sipi::SipiError
        └── JpegError            ← NOT caught, reaches std::terminate
  └── Sipi::SipiImageError       ← NOT caught, reaches std::terminate
```

## Proposed Solution

### Strategy: Defense in Depth

Fix all three layers so that (a) JPEG errors don't cascade, (b) C++ exceptions never cross C boundaries, and (c) the server never crashes from an uncaught request-thread exception.

### Fix 1: Remove `jpeg_finish_compress` from error paths

Replace `jpeg_finish_compress(&cinfo)` with nothing on all four error-path catch blocks. `jpeg_destroy_compress(&cinfo)` (already called after) safely cleans up regardless of compressor state.

**Files:** `src/formats/SipiIOJpeg.cpp` — lines 1189, 1217, 1239, 1282

### Fix 2: Safe error signaling in libjpeg C callbacks

The fundamental problem: libjpeg callbacks are called from C code. Throwing C++ exceptions through C frames is UB. The current code already does this (catches `int`, re-throws as `JpegError`), and the fix must not perpetuate the pattern.

**Approach — extended `jpeg_error_mgr` with `setjmp`/`longjmp`:**

This is the **canonical libjpeg error handling pattern** documented by IJG. It stores the `jmp_buf` inside an extended error manager struct, leaving `client_data` untouched for the destination managers.

1. Define an extended error manager:
   ```cpp
   struct JpegErrorMgr {
       jpeg_error_mgr pub;  // must be first — libjpeg casts to this
       jmp_buf error_jmp;
       char error_message[JMSG_LENGTH_MAX]{};  // char[], not std::string — safe across longjmp
   };
   ```

2. In `jpegWriteErrorExit` (NEW — write-path only), use `longjmp`:
   ```cpp
   static void jpegWriteErrorExit(j_common_ptr cinfo) {
       auto *myerr = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
       (*(cinfo->err->format_message))(cinfo, myerr->error_message);
       longjmp(myerr->error_jmp, 1);
   }
   ```

3. **Temporarily** keep the existing `jpegErrorExit` (with `throw`) for the read/getDim paths, renamed to `jpegReadErrorExit`. The read path also has throw-through-C UB, but is addressed later in Phase H5. The phased approach fixes the production crash (write path) first, then systematically converts the read path.

4. In destination callbacks, catch ALL exception types and call `ERREXIT` to trigger the `longjmp` path. **Note:** Simply returning `FALSE` from `empty_output_buffer` does NOT call `error_exit` in IJG libjpeg 9f — it returns to the caller with incomplete state. The `ERREXIT` macro is the standard libjpeg mechanism to signal a fatal error from within a callback:

   ```cpp
   static boolean empty_html_buffer(j_compress_ptr cinfo) {
       auto *html_buffer = reinterpret_cast<HtmlBuffer *>(cinfo->client_data);
       try {
           html_buffer->conn->sendAndFlush(html_buffer->buffer, html_buffer->buflen);
       } catch (...) {
           ERREXIT(cinfo, JERR_FILE_WRITE);  // calls jpegWriteErrorExit → longjmp
           return FALSE;  // unreachable, but satisfies return type
       }
       html_buffer->datasize = 0;
       return TRUE;
   }
   ```

5. In `SipiIOJpeg::write()`, set up `setjmp` before any libjpeg call:
   ```cpp
   JpegErrorMgr jerr;
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = jpegWriteErrorExit;

   if (setjmp(jerr.error_jmp)) {
       // longjmp landed here — clean up and throw in C++ context
       if (filepath == "HTTP") {
           cleanup_html_destination(&cinfo);
       }
       jpeg_destroy_compress(&cinfo);
       throw SipiImageError("JPEG write failed: " + std::string(jerr.error_message));
   }
   ```

**Why this approach:**
- `setjmp`/`longjmp` is the libjpeg-documented error handling pattern — guaranteed safe through C frames
- Extended `jpeg_error_mgr` is the canonical way to pass `jmp_buf` — **does NOT touch `client_data`**, avoiding conflict with `FileBuffer`/`HtmlBuffer` destination managers
- `char[]` error message avoids C++ object semantics across `longjmp` (no `std::string` destructor issues)
- Separate `jpegWriteErrorExit` / `jpegReadErrorExit` avoids breaking the read path
- `ERREXIT()` macro in callbacks is the standard libjpeg error signaling mechanism

**Files:** `src/formats/SipiIOJpeg.cpp` — `jpegWriteErrorExit` (new), `jpegReadErrorExit` (renamed), `empty_html_buffer`, `term_html_destination`, `empty_file_buffer`, `term_file_destination`, `write()`

### Fix 3: Catch-all in server request pipeline

Add `catch (std::exception&)` and `catch (...)` to both `process_request` and `socket_request_processor`:

```cpp
// In Server::process_request (Server.cpp:1280)
catch (std::exception &ex) {
    spdlog::error("Unhandled exception in request processing: {}", ex.what());
    sentry_capture_event(/* ... */);
    try {
        conn_obj.send_error(500, "Internal Server Error");
    } catch (...) {
        // Headers may already be sent (partial chunked response).
        // Nothing to do — just close the connection.
    }
    conn_obj.cleanupUploads();  // prevent temp file leak
    return CLOSE;
}
catch (...) {
    spdlog::error("Unknown exception in request processing");
    sentry_capture_event(/* ... */);
    try {
        conn_obj.send_error(500, "Internal Server Error");
    } catch (...) {}
    conn_obj.cleanupUploads();
    return CLOSE;
}

// In socket_request_processor (Server.cpp:689)
// Wrap the process_request call in a try/catch as a last-resort safety net
```

**Important edge case — partially-sent responses:** When the JPEG write has already started sending chunked HTTP data (200 OK + partial JPEG bytes), `send_error` cannot retroactively change the status code. The catch block must handle this by:
- Attempting `send_error` in a nested try/catch
- If that throws (headers already sent), simply close the connection
- The client receives a truncated response, which is the only correct behavior

**Cleanup considerations:** The catch-all must also handle:
- `conn_obj.cleanupUploads()` — prevents temp file leaks on the error path
- The `_active_connections` counter in the thread function (line 744) runs AFTER `process_request` returns, so it remains correct even when exceptions are caught inside `process_request`

**Files:** `shttps/Server.cpp` — `process_request`, `socket_request_processor`

## Technical Considerations

### Architecture impacts

- **Extended `jpeg_error_mgr` pattern:** This is the standard libjpeg idiom (see IJG documentation and `example.c`). The `JpegErrorMgr` struct's first field is `jpeg_error_mgr pub`, so libjpeg can cast `cinfo->err` to it. Our code casts back to `JpegErrorMgr*` to access `error_jmp`. This pattern is used by virtually all C++ programs that use libjpeg.

- **`client_data` remains untouched:** The destination managers (`FileBuffer` via `new`, `HtmlBuffer` via `malloc`) continue to use `client_data` exactly as before. No conflict.

- **Split error exit functions:** `jpegWriteErrorExit` uses `longjmp` (for write path with I/O callbacks). `jpegReadErrorExit` initially keeps `throw JpegError(...)` for the read/getDim paths (Phase D2), then Phase H5 converts these to `setjmp`/`longjmp` as well, completing the fix across all JPEG paths.

- **`setjmp`/`longjmp` and RAII:** `longjmp` does NOT call C++ destructors. Between `setjmp` and `longjmp`, the following RAII objects may leak:
  - `exifchunk` (`shttps::make_unique<unsigned char[]>`) at line 1210
  - `xmpchunk` (`shttps::make_unique<char[]>`) at line 1233
  - `iccchunk` (`shttps::make_unique<unsigned char[]>`) at line 1268
  - `iptcchunk` (`shttps::make_unique<char[]>`) at line 1305

  **Decision:** Accept these small leaks (each <65KB). They occur only on the error path, and the buffers are freed when the thread handles the next request (stack unwinding from the `SipiImageError` thrown after `longjmp`). Converting to raw `malloc`/`free` would reduce code clarity for marginal benefit. Document in code comments.

- **Thread safety:** Each request thread has its own `jpeg_compress_struct`, `JpegErrorMgr`, and buffer structs. The `jmp_buf` is per-request-thread on the stack. No shared state concerns.

### Performance implications

- Negligible. `setjmp` is cheap (saves registers). Error paths are cold code.

### Security considerations

- Fix 3 prevents denial-of-service via crafted inputs that trigger uncaught exceptions. Currently, a single malformed request can crash the entire server, affecting all clients.

### Resource cleanup

- **`cleanup_html_destination`** (`SipiIOJpeg.cpp:347`): Must be called on ALL error paths for HTTP writes. Currently only called at line 1101. With the `setjmp` approach, cleanup happens in one place (the `setjmp` error handler), eliminating scattered cleanup in multiple catch blocks.

- **`FdGuard` for `outfile`:** The write method manually closes `outfile` in every catch block. Use the existing `FdGuard` RAII class (line 40) to eliminate these scattered `close()` calls. **Note:** `FdGuard` is RAII, so it won't be destroyed by `longjmp`. However, since `outfile` is opened BEFORE `setjmp`, and the `FdGuard` would be constructed before `setjmp`, its destructor WILL run during normal C++ stack unwinding when we `throw SipiImageError(...)` after `longjmp`. So this is safe.

- **`term_html_destination` leak:** If `sendAndFlush` throws, the subsequent `free()` calls are skipped. With the `ERREXIT` approach, the callback triggers `longjmp` → explicit cleanup via `cleanup_html_destination`. No leak.

## Deep Call-Path Analysis

### `jpegErrorExit` is shared across three paths

The current `jpegErrorExit` (line 477) is registered in:
1. `read()` at line 535 — decompression path
2. `getDim()` at line 886 — dimension query path
3. `write()` at line 1083 — compression path

**Impact of the fix — phased approach:**
- **Phase D (write path first):** Only `write()` gets `jpegWriteErrorExit` with `longjmp`. The `read()` and `getDim()` paths temporarily continue using `jpegReadErrorExit` with `throw`. This is safe in the interim because the read path has comprehensive `try/catch(JpegError)` blocks around every libjpeg call.
- **Phase H5 (read path second):** `read()` and `getDim()` are converted to `setjmp`/`longjmp` as well, using the same `JpegErrorMgr` pattern. This completes the elimination of all throw-through-C UB in the JPEG handler.

### `empty_html_buffer` returning FALSE

In IJG libjpeg 9f, `empty_output_buffer` returning `FALSE` does NOT directly call `error_exit`. The return value is passed back to the calling function, which may continue with corrupted state. The correct pattern is to call `ERREXIT(cinfo, JERR_FILE_WRITE)` from the callback, which invokes `error_exit` → `jpegWriteErrorExit` → `longjmp`. The `ERREXIT` macro never returns.

### `sendAndFlush` throws three different types

`Connection::sendAndFlush` can throw:
1. `OUTPUT_WRITE_FAIL` (int -2) — on I/O error (broken pipe)
2. `shttps::Error("Sending data already terminated!")` — precondition check at line 1361
3. `shttps::Error("Header already sent...")` — precondition check at line 1351

The `catch(...)` in the callback catches all three. The error detail is lost (we just know "HTTP write failed"), but the `ERREXIT` macro provides a generic `JERR_FILE_WRITE` code. Specific error details can be logged in the catch block before calling `ERREXIT`.

### Resources between setjmp and longjmp

Between `setjmp` (before `jpeg_start_compress`) and potential `longjmp`:
- `outfile` (fd) — opened before `setjmp`, cleaned up explicitly in error handler
- `HtmlBuffer` (malloc'd via `jpeg_html_dest`) — cleaned up via `cleanup_html_destination`
- `FileBuffer` (new'd via `jpeg_file_dest`) — cleaned up via `jpeg_destroy_compress`
- `make_unique` marker buffers — accepted small leak (see Architecture impacts)
- No mutexes held (the mutex at line 515 is commented out)

## Similar Bugs in Other Format Handlers (IN SCOPE)

### SipiIOPng.cpp — SAME BUG PATTERN

`sipi_error_fn` at line 113-117 throws `Sipi::SipiError` through libpng's C frames — identical UB to the JPEG bug:

```cpp
void sipi_error_fn(png_structp png_ptr, png_const_charp error_msg) {
    log_err("PNG error: %s", error_msg);
    throw Sipi::SipiError(error_msg);  // UB: C++ exception through libpng C frames
}
```

Registered in: `png_create_read_struct()` (line 150, 346) and `png_create_write_struct()` (line 419).

**Fix approach:** libpng provides `png_jmpbuf()` — the canonical error handling mechanism. Replace `throw` with `longjmp(png_jmpbuf(png_ptr), 1)` and add `setjmp(png_jmpbuf(png_ptr))` in `read()`, `getDim()`, and `write()`. This is a well-documented pattern in the libpng manual.

```cpp
// Error handler — longjmp back to caller
void sipi_error_fn(png_structp png_ptr, png_const_charp error_msg) {
    log_err("PNG error: %s", error_msg);
    longjmp(png_jmpbuf(png_ptr), 1);
}

// In read()/getDim()/write():
if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    fclose(infile);
    throw SipiImageError("PNG processing failed");
}
```

**Affected paths:**
- `read()` (line 150) — registered in `png_create_read_struct`
- `getDim()` (line 346) — registered in `png_create_read_struct`
- `write()` (line 419) — registered in `png_create_write_struct`
- HTTP write callbacks `conn_write_data` (line 388) and `conn_flush_data` (line 401) — these currently swallow errors silently (`catch(int i) {}` with a TODO comment). Should call `png_error(png_ptr, "write failed")` to trigger the longjmp path.

### SipiIOTiff.cpp — SAME BUG PATTERN

`memTiffWriteProc` (line 87-105) and `memTiffSeekProc` (line 108-155) throw `SipiImageError` through libtiff's C callbacks on `realloc` failure:

```cpp
static tsize_t memTiffWriteProc(thandle_t handle, tdata_t buf, tsize_t size) {
    auto *memtif = (MEMTIFF *)handle;
    if (((tsize_t)memtif->fptr + size) > memtif->size) {
        if ((memtif->data = realloc(...)) == nullptr) {
            throw Sipi::SipiImageError("realloc failed", errno);  // UB through libtiff C
        }
    }
    // ...
}
```

Throws at lines: 93, 116, 128, 140 (4 throw sites across `memTiffWriteProc` and `memTiffSeekProc`).

**Fix approach:** libtiff does NOT have a `setjmp`/`longjmp` mechanism like libjpeg and libpng. The callbacks must return error values instead of throwing:

```cpp
static tsize_t memTiffWriteProc(thandle_t handle, tdata_t buf, tsize_t size) {
    auto *memtif = (MEMTIFF *)handle;
    if (((tsize_t)memtif->fptr + size) > memtif->size) {
        auto *newdata = (unsigned char *)realloc(memtif->data, memtif->fptr + memtif->incsiz + size);
        if (newdata == nullptr) {
            // Return 0 to signal write failure — libtiff treats this as error
            return 0;
        }
        memtif->data = newdata;
        memtif->size = memtif->fptr + memtif->incsiz + size;
    }
    memcpy(memtif->data + memtif->fptr, buf, size);
    memtif->fptr += size;
    if (memtif->fptr > memtif->flen) memtif->flen = memtif->fptr;
    return size;
}
```

Same pattern for `memTiffSeekProc` — return `(toff_t)-1` on failure instead of throwing. Also fix `memTiffOpen` (lines 50, 58) to return `nullptr` and let callers handle it, or keep the throw since `memTiffOpen` is called from Sipi's own C++ code (not from a libtiff C callback).

**Pre-existing realloc leak also fixed:** The current code does `memtif->data = realloc(memtif->data, ...)` — if `realloc` returns NULL, the original `memtif->data` pointer is lost (classic realloc leak). Using a temporary `newdata` variable (as shown above) fixes both the throw-through-C bug AND this pre-existing memory leak.

**Note:** The TIFF error/warning handlers (`tiffError`, `tiffWarning` at lines 382-397) are already safe — they're silenced no-ops that don't throw.

### SipiIOJ2k.cpp — SAFE

Kakadu is a C++ library. Error handling uses `kdu_exception` caught via standard C++ mechanisms. No C boundary crossing. No action needed.

### Additional bugs found in SipiIOJpeg.cpp (IN SCOPE)

#### Bug 4: `parse_photoshop()` bounds-checking vulnerabilities (line 385-468) — [DEV-6154](https://linear.app/dasch/issue/DEV-6154)

Parses untrusted Photoshop metadata from uploaded images. Multiple issues:
- No bounds check before reading 4-byte `sig` — `*(ptr+1)`, `*(ptr+2)`, `*(ptr+3)` without checking remaining length
- Tag ID read without bounds check at line 408-409
- Pascal string `slen` from untrusted data — could advance `ptr` past buffer
- `datalen` not validated against remaining buffer — heap buffer over-read when passed to `SipiIptc`, `SipiIcc`, `SipiExif`, `SipiXmp` constructors
- **Memory leak in default case:** `calloc` at line 457 never `free`'d
- **Missing `break` in case 0x0424 (XMP):** falls through to default, causing a useless leaking `calloc`/`memcpy`

**Fix:** Add bounds checking at each pointer advance (`ptr + N <= data + length`). Fix the missing `break`. Fix the memory leak. Self-contained function (83 lines).

#### Bug 5: `read()` fd leak on failed magic number read (line 500-504) — [DEV-6155](https://linear.app/dasch/issue/DEV-6155)

```cpp
if ((infile = ::open(filepath.c_str(), O_RDONLY)) == -1) { return false; }
unsigned char magic[2];
if (::read(infile, magic, 2) != 2) { return false; }  // infile not closed!
```

**Fix:** Add `::close(infile)` before return, or use `FdGuard` RAII. Trivial.

#### Bug 6: XMP parsing unbounded pointer arithmetic (line 661-670) — [DEV-6156](https://linear.app/dasch/issue/DEV-6156)

Parses untrusted XMP metadata. The code itself has a comment acknowledging the bug: `"ISSUE: code failes here if there are many concurrent access; data overrrun??"`.

```cpp
while (*pos != *s) pos++;   // No bounds check — reads past buffer
while (*pos != '>') { pos++; }  // No bounds check
```

**Fix:** Add bounds checking: `while (pos < data_end && *pos != *s) pos++;`. Handle missing marker gracefully.

#### Bug 7: `icc_buffer` leak in `read()` on error paths — [DEV-6157](https://linear.app/dasch/issue/DEV-6157)

`icc_buffer` is allocated via `realloc` during ICC marker collection. If a `JpegError` fires between allocation (line ~601) and cleanup (line 708-709) — e.g., if `jpeg_start_decompress` throws at line 713 — the buffer leaks.

**Fix:** Wrap `icc_buffer` in RAII (`std::unique_ptr<unsigned char[], decltype(&free)>`) or ensure `free(icc_buffer)` in all catch blocks.

#### Bug 8: JPEG read/getDim `jpegReadErrorExit` throws through C frames

The renamed `jpegReadErrorExit` still does `throw JpegError(...)` through libjpeg's C stack in `read()` and `getDim()`. Same class of UB as the write path. The read path has ~6 separate `try/catch(JpegError)` blocks (lines 537-546, 552-558, 712-718, 764-788) that would need restructuring around a single `setjmp`.

**Fix:** Add `JpegErrorMgr` + `setjmp` to `read()` and `getDim()`, same pattern as the write fix. Restructure the scattered try/catch blocks into the single setjmp error handler.

## Implementation Approach — TDD Phases

### Methodology

Follow strict TDD: capture existing good behavior → reproduce bad behavior → apply fix → verify all pass. All tests must pass locally at each checkpoint.

### Phase A: Baseline — Capture Current Good Behavior

**Checkpoint: all existing tests pass before any changes.**

```bash
make nix-build && make nix-test         # unit tests
make rust-test-e2e                       # e2e tests
make hurl-test                           # HTTP contract tests
```

#### A1: Add JPEG write happy-path unit tests

Add to `test/unit/sipiimage/` a new test file `jpeg_write_test.cpp`:

- [ ] Test `SipiIOJpeg::write()` to file with a valid image — verify output is a valid JPEG (check magic bytes `0xFFD8`)
- [ ] Test `SipiIOJpeg::write()` with different quality settings — verify files differ in size
- [ ] Test `SipiIOJpeg::read()` → `write()` roundtrip — verify dimensions preserved

#### A2: Add JPEG read error-path unit tests

- [ ] Test `SipiIOJpeg::getDim()` with a non-JPEG file — verify clean error return (existing test `JpegGetDimInvalidFileReturnFailure` covers this; verify it passes)
- [ ] Test `SipiIOJpeg::read()` with a truncated JPEG (first 100 bytes) — verify clean error, no crash

#### A3: Add E2E baseline tests

- [ ] Request a valid JPEG via IIIF (`/full/max/0/default.jpg`) — verify 200 + valid JPEG response (likely covered by existing tests; verify)
- [ ] Request JPEG after server handles 10 sequential valid requests — verify server stays healthy

**Checkpoint: all tests pass (existing + A1-A3).**

### Phase B: Reproduce Bad Behavior

Write tests that demonstrate the bugs. These tests should FAIL or show crashes with the current code.

#### B1: Reproduce Bug 1 — `jpeg_finish_compress` on invalid state

- [ ] Unit test using `EXPECT_DEATH` or signal handler: Call `SipiIOJpeg::write()` with parameters that cause `jpeg_start_compress` to fail (e.g., zero-dimension image), verify the process receives SIGABRT or the test catches the double-throw. Mark with `// Bug 1: jpeg_finish_compress on error path`.

#### B2: Reproduce Bug 2 — exception through C callback

- [ ] E2E test (isolated server): Request a large JPEG via IIIF, **disconnect the TCP socket mid-transfer** (drop connection after receiving headers + first chunk). Verify server behavior — currently this may crash or produce unclean state. Use raw TCP socket (pattern from `connection.rs`).

#### B3: Reproduce Bug 3 — uncaught exception crashes server

- [ ] E2E test (isolated server): Trigger a `JpegError` that escapes to `process_request` (e.g., via a corrupt JPEG that triggers encoding error during format conversion). Verify the server crashes (currently) — use process exit code or health check after the request.
- [ ] Un-ignore and adapt `corrupt_image_handling` test (`iiif_compliance.rs:1350`). Note: the existing test uses corrupt JP2, not JPEG. Add a JPEG variant that truncates a JPEG file and requests conversion.

**Checkpoint: Bug-reproducing tests demonstrate the failures. Other tests still pass.**

### Phase C: Fix Bug 1 — Remove `jpeg_finish_compress` from error paths (simplest fix)

#### C1: Remove `jpeg_finish_compress` from catch blocks

In `SipiIOJpeg::write()`, remove `jpeg_finish_compress(&cinfo)` from the catch blocks at lines 1189, 1217, 1239, 1282. Keep only `jpeg_destroy_compress(&cinfo)`.

**Checkpoint: B1 test should now pass (no double-throw). B2/B3 may still fail.**

### Phase D: Fix Bug 2 — setjmp/longjmp error handling (largest change)

#### D1: Define `JpegErrorMgr` struct and `jpegWriteErrorExit`

```cpp
struct JpegErrorMgr {
    jpeg_error_mgr pub;  // must be first
    jmp_buf error_jmp;
    char error_message[JMSG_LENGTH_MAX]{};
};

static void jpegWriteErrorExit(j_common_ptr cinfo) {
    auto *myerr = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
    (*(cinfo->err->format_message))(cinfo, myerr->error_message);
    longjmp(myerr->error_jmp, 1);
}
```

#### D2: Rename existing `jpegErrorExit` to `jpegReadErrorExit`

Keep the `throw JpegError(...)` behavior for `read()` and `getDim()` for now. Only the name changes — no behavioral change to the read path. **Phase H5 later converts this to `longjmp` as well**, completing the fix across all JPEG paths.

#### D3: Fix destination callbacks to catch all exceptions

Update `empty_html_buffer`, `term_html_destination`, `empty_file_buffer`, and `term_file_destination`:

```cpp
static boolean empty_html_buffer(j_compress_ptr cinfo) {
    auto *html_buffer = reinterpret_cast<HtmlBuffer *>(cinfo->client_data);
    try {
        html_buffer->conn->sendAndFlush(html_buffer->buffer, html_buffer->buflen);
    } catch (const std::exception &e) {
        spdlog::error("JPEG HTTP write failed: {}", e.what());
        ERREXIT(cinfo, JERR_FILE_WRITE);
        return FALSE;  // unreachable
    } catch (...) {
        spdlog::error("JPEG HTTP write failed: unknown error");
        ERREXIT(cinfo, JERR_FILE_WRITE);
        return FALSE;  // unreachable
    }
    html_buffer->datasize = 0;
    return TRUE;
}
```

Apply the same pattern to `term_html_destination`, `empty_file_buffer`, `term_file_destination`.

#### D4: Add `setjmp`-based error handling to `write()`

```cpp
void SipiIOJpeg::write(/* params */) {
    // ... setup code ...
    JpegErrorMgr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegWriteErrorExit;

    // FdGuard for outfile — constructed before setjmp, safe
    FdGuard outfile_guard;

    if (setjmp(jerr.error_jmp)) {
        // longjmp landed here — clean up and throw in C++ context
        if (filepath == "HTTP") {
            cleanup_html_destination(&cinfo);
        }
        jpeg_destroy_compress(&cinfo);
        throw SipiImageError("JPEG write failed: " + std::string(jerr.error_message));
    }

    // All libjpeg calls below this point — errors → longjmp → cleanup above
    jpeg_start_compress(&cinfo, TRUE);
    // ... markers, scanlines, finish ...
    jpeg_finish_compress(&cinfo);
}
```

Remove the existing per-call try/catch blocks around `jpeg_start_compress`, `jpeg_write_marker`, etc. — the `setjmp` handles all libjpeg errors uniformly.

#### D5: Use `FdGuard` for `outfile`

Replace manual `outfile` management with RAII. Since `FdGuard` is constructed before `setjmp`, its destructor runs during the `throw SipiImageError(...)` unwinding after `longjmp`.

#### D6: Cleanup for HTTP destination on all error paths

With the `setjmp` approach, `cleanup_html_destination(&cinfo)` is called in exactly one place — the `setjmp` error handler. This replaces the scattered cleanup in multiple catch blocks.

**Checkpoint: B1 + B2 tests should pass. B3 may still fail (server-level catch missing).**

### Phase E: Fix Bug 3 — Catch-all in server request pipeline (safety net)

#### E1: Add catch-all to `process_request`

After the existing `catch (Error &err)` in `Server.cpp:1280`:

```cpp
catch (std::exception &ex) {
    spdlog::error("Unhandled std::exception in request: {}", ex.what());
    try { conn_obj.send_error(500, "Internal Server Error"); }
    catch (...) { /* headers already sent, close connection */ }
    conn_obj.cleanupUploads();
    return CLOSE;
}
catch (...) {
    spdlog::error("Unknown exception in request processing");
    try { conn_obj.send_error(500, "Internal Server Error"); }
    catch (...) {}
    conn_obj.cleanupUploads();
    return CLOSE;
}
```

#### E2: Add catch-all to `socket_request_processor`

Wrap the `process_request` call at line 689:

```cpp
try {
    auto result = process_request(/* ... */);
    // ... handle result ...
} catch (std::exception &ex) {
    spdlog::critical("Exception escaped process_request: {}", ex.what());
} catch (...) {
    spdlog::critical("Unknown exception escaped process_request");
}
```

**Checkpoint: ALL JPEG tests pass — B1, B2, B3, and all existing tests.**

### Phase F: Fix PNG — setjmp/longjmp via png_jmpbuf (TDD)

#### F0: PNG baseline tests

- [ ] Test `SipiIOPng::read()` happy path — read a valid PNG, verify dimensions
- [ ] Test `SipiIOPng::write()` happy path — write a valid PNG to file, verify magic bytes `0x89504E47`
- [ ] Test `SipiIOPng::getDim()` with a non-PNG file — verify clean error return
- [ ] E2E: request a PNG via IIIF (`/full/max/0/default.png`) — verify 200 + valid PNG

**Checkpoint: PNG baseline tests pass.**

#### F1: PNG reproduce bad behavior

- [ ] Test `SipiIOPng::read()` with a truncated PNG — currently throws through C (UB). Use `EXPECT_DEATH` or signal handler to capture crash/UB behavior.
- [ ] E2E (isolated server): request conversion of a corrupt PNG — verify server behavior (currently may crash)

#### F2: Fix `sipi_error_fn` to use `longjmp`

Replace `throw Sipi::SipiError(...)` with `longjmp(png_jmpbuf(png_ptr), 1)`:

```cpp
void sipi_error_fn(png_structp png_ptr, png_const_charp error_msg) {
    log_err("PNG error: %s", error_msg);
    longjmp(png_jmpbuf(png_ptr), 1);
}
```

#### F3: Add `setjmp` to `read()`, `getDim()`, and `write()`

In each function, after `png_create_read_struct` / `png_create_write_struct`:

```cpp
if (setjmp(png_jmpbuf(png_ptr))) {
    // cleanup: destroy png structs, close file
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    if (infile) fclose(infile);
    throw SipiImageError("PNG processing failed");
}
```

#### F4: Fix HTTP write callbacks

Update `conn_write_data` (line 388) and `conn_flush_data` (line 401) to signal errors via `png_error()` instead of silently swallowing:

```cpp
void conn_write_data(png_structp png_ptr, png_bytep data, png_size_t length) {
    auto *conn = (shttps::Connection *)png_get_io_ptr(png_ptr);
    try {
        conn->sendAndFlush(data, length);
    } catch (...) {
        png_error(png_ptr, "HTTP write failed");  // triggers sipi_error_fn → longjmp
    }
}
```

**Checkpoint: PNG reproduce tests now pass. All existing tests still pass.**

### Phase G: Fix TIFF — return error codes instead of throwing (TDD)

#### G0: TIFF baseline tests

- [ ] Test `SipiIOTiff::read()` happy path — read a valid TIFF, verify dimensions
- [ ] Test `SipiIOTiff::write()` happy path — write a valid TIFF to file
- [ ] E2E: request a TIFF source image via IIIF — verify 200 + valid response

**Checkpoint: TIFF baseline tests pass.**

#### G1: TIFF reproduce bad behavior

- [ ] The `realloc` failure path is hard to trigger in tests. Instead, verify correctness by code review + ASan validation.
- [ ] E2E (isolated server): request conversion of a corrupt TIFF — verify server behavior

#### G2: Fix `memTiffWriteProc` and `memTiffSeekProc`

Replace `throw SipiImageError(...)` with error return values:
- `memTiffWriteProc`: return `0` on `realloc` failure (4 throw sites → 4 `return 0`)
- `memTiffSeekProc`: return `(toff_t)-1` on `realloc` failure

Keep `memTiffOpen` throws since it's called from C++ code directly (not from a libtiff C callback).

**Checkpoint: TIFF tests pass. All existing tests still pass.**

### Phase H: Golden tests for metadata parsing + fix metadata bugs (TDD)

Before touching `parse_photoshop()` or XMP parsing, lock down current behavior with approval/golden tests. The project already uses ApprovalTests (`test/approval/`) — follow that pattern.

#### H0: Create metadata golden tests

Use test images that have rich metadata (EXIF, IPTC, XMP, ICC, Photoshop markers). For each, capture the full parsed metadata output as the approved golden snapshot.

- [ ] Golden test: read `MaoriFigure.jpg` → dump EXIF fields → approve snapshot
- [ ] Golden test: read `img_exif_gps.jpg` → dump EXIF + GPS fields → approve snapshot
- [ ] Golden test: read `gray_with_icc_another.jpg` → dump ICC profile info → approve snapshot
- [ ] Golden test: read image with Photoshop/IPTC markers (APP13) → dump parsed `parse_photoshop()` output → approve snapshot. **Note:** verify which test fixtures contain APP13 markers at implementation time (`MaoriFigure.jpg` or `img_exif_gps.jpg` are likely candidates; if none do, create a test image with `exiftool -IPTC:Keywords="test" ...` or use one from the e2e test corpus)
- [ ] Golden test: read image with XMP → dump parsed XMP → approve snapshot (check `MaoriFigure.jpg` or `img_exif_gps.jpg` for XMP; if absent, create one)
- [ ] Golden test: read `HasCommentBlock.JPG` → dump comment → approve snapshot

These tests use ApprovalTests' `Approvals::verify()` pattern — any change to metadata parsing output causes a diff that must be explicitly approved.

**Checkpoint: golden tests capture current metadata behavior. All pass.**

#### H1: Fix `parse_photoshop()` bounds checking

- [ ] Add bounds checking: `if (ptr + 4 > data + length) break;` before each multi-byte read
- [ ] Validate `datalen` against remaining buffer: `if (ptr + datalen > data + length) break;`
- [ ] Fix missing `break` in case 0x0424 (XMP)
- [ ] Fix memory leak: remove the useless `calloc`/`memcpy` in the `default` case (or add `free(str)`)
- [ ] Verify golden tests still pass — behavior unchanged for well-formed input

#### H2: Fix XMP parsing unbounded pointer arithmetic

- [ ] Add bounds checking: `while (pos < data_end && *pos != *s) pos++;`
- [ ] Same for the `>` search: `while (pos < data_end && *pos != '>') pos++;`
- [ ] Handle missing end marker gracefully (skip XMP instead of reading past buffer)
- [ ] Verify golden tests still pass — behavior unchanged for well-formed input

#### H3: Fix `read()` fd leak

- [ ] Add `::close(infile);` before the `return false` at line 503, or use `FdGuard`

#### H4: Fix `icc_buffer` leak on error paths

- [ ] Wrap `icc_buffer` in RAII: `std::unique_ptr<unsigned char[], decltype(&free)>` or add `free(icc_buffer)` to all catch blocks between allocation and line 708

#### H5: Add `setjmp` to JPEG `read()` and `getDim()`

- [ ] Add `JpegErrorMgr` + `setjmp` to `read()` — same `JpegErrorMgr` struct as write fix, but with decompress-specific cleanup:
  ```cpp
  JpegErrorMgr jerr;
  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = jpegErrorExit;  // renamed from jpegWriteErrorExit — now serves both paths

  if (setjmp(jerr.error_jmp)) {
      // Read-path cleanup (different from write):
      if (icc_buffer) free(icc_buffer);    // Bug 7 fix
      if (infile != -1) close(infile);      // Bug 5 fix (or FdGuard)
      jpeg_destroy_decompress(&cinfo);      // NOT jpeg_destroy_compress
      throw SipiImageError("JPEG read failed: " + std::string(jerr.error_message));
  }
  ```
- [ ] Restructure the ~6 scattered `try/catch(JpegError)` blocks (lines 537-546, 552-558, 712-718, 764-788) — remove them all since the single `setjmp` handles every libjpeg error uniformly. Non-JPEG exceptions (e.g., `SipiImageError` from metadata constructors) still propagate normally via C++ stack unwinding
- [ ] Add `JpegErrorMgr` + `setjmp` to `getDim()` — simpler, only needs `jpeg_destroy_decompress` + `fclose` cleanup
- [ ] Rename `jpegWriteErrorExit` to `jpegErrorExit` since it now serves both read and write paths. Remove `jpegReadErrorExit` (no longer needed)
- [ ] Verify golden tests still pass — metadata extraction unchanged
- [ ] Verify truncated JPEG read test passes cleanly (no UB)

**Checkpoint: all metadata golden tests pass. All fixes verified under ASan.**

### Phase I: Final Verification

- [ ] `make nix-test` — all unit tests pass (including golden tests)
- [ ] `make rust-test-e2e` — all e2e tests pass (including new ones for JPEG, PNG, TIFF)
- [ ] `make hurl-test` — HTTP contract tests pass
- [ ] `make nix-build-sanitized && make nix-test-sanitized` — zero ASan/UBSan findings
- [ ] `make test-smoke` — Docker smoke test passes
- [ ] Golden tests confirm metadata output unchanged for all test images

## Acceptance Criteria

### JPEG
- [ ] Sentry issues SIPI-G and SIPI-H stop receiving new events after deployment
- [ ] Server survives JPEG write failures without crashing (no `std::terminate`, no SIGABRT)
- [ ] No C++ exceptions propagate through libjpeg C stack frames in read or write paths (UB eliminated)
- [ ] `jpeg_finish_compress` is never called on error paths
- [ ] HTTP destination buffers are properly freed on all error paths (no leaks under ASan)
- [ ] File descriptors are properly closed on all error paths (FdGuard RAII)

### PNG
- [ ] No C++ exceptions propagate through libpng C stack frames (UB eliminated)
- [ ] `sipi_error_fn` uses `longjmp(png_jmpbuf(...))` instead of `throw`
- [ ] `read()`, `getDim()`, and `write()` all have `setjmp(png_jmpbuf(...))` error recovery
- [ ] HTTP write callbacks (`conn_write_data`, `conn_flush_data`) signal errors via `png_error()` instead of silently swallowing

### TIFF
- [ ] No C++ exceptions thrown from libtiff C callbacks
- [ ] `memTiffWriteProc` returns 0 on realloc failure instead of throwing
- [ ] `memTiffSeekProc` returns `(toff_t)-1` on realloc failure instead of throwing

### Server
- [ ] Server returns HTTP 500 for image encoding errors (when headers not yet sent)
- [ ] Server closes connection cleanly for mid-stream errors (when headers already sent)
- [ ] `process_request` catches `std::exception` and `...` — no exception reaches `std::terminate`
- [ ] `socket_request_processor` has last-resort catch-all

### Metadata Parsing (Bugs 4-6)
- [ ] `parse_photoshop()` bounds-checked at every pointer advance — no read past buffer
- [ ] `parse_photoshop()` missing `break` fixed, memory leak fixed
- [ ] XMP parsing bounds-checked — no unbounded pointer arithmetic
- [ ] Golden/approval tests lock down metadata output for all test images
- [ ] All golden tests pass after fixes — behavior unchanged for well-formed input

### JPEG read/getDim (Bug 8)
- [ ] `read()` and `getDim()` use `setjmp`/`longjmp` via `JpegErrorMgr` — no `throw` through C
- [ ] `read()` fd leak fixed (Bug 5)
- [ ] `icc_buffer` leak on error paths fixed (Bug 7)

### Cross-Cutting
- [ ] All existing tests pass (unit, e2e, smoke, hurl, fuzz)
- [ ] New unit and e2e tests cover crash scenarios for JPEG, PNG, and TIFF
- [ ] Golden tests verify metadata parsing unchanged
- [ ] Zero new ASan/UBSan findings
- [ ] Tests pass at each TDD checkpoint (A through I)

## Dependencies & Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| `setjmp`/`longjmp` skips C++ destructors | `make_unique` marker buffers (exif/xmp/icc/iptc, each <65KB) leak on error path | Accept: buffers freed on next stack unwind from `throw SipiImageError`. Document in code comments |
| `longjmp` from signal handler context | Undefined behavior | Not applicable — `jpegWriteErrorExit` is called from libjpeg's own error path, not signal handlers |
| Split error exit functions (Phases D-E only) | Cognitive overhead — two similar functions | Temporary: merged back into single `jpegErrorExit` in Phase H5 |
| `ERREXIT` in callbacks | Must never be called after `jpeg_destroy_compress` | Callbacks are only called by libjpeg during active compress — destroy removes them from the call chain |
| Partially-sent chunked responses | Client receives truncated JPEG | Expected behavior — HTTP spec allows this. `send_error` nested in try/catch handles the already-sent-headers case |
| Regression in JPEG output quality | Images differ after fix | Fix only touches error paths — happy path unchanged. Phase A tests capture baseline quality |
| JPEG read path restructuring | ~6 try/catch blocks → single setjmp — risk of subtle behavior change | Golden tests lock down metadata output; ASan validates no leaks |
| `parse_photoshop()` bounds changes | Could reject previously-accepted malformed metadata | Golden tests verify identical output for well-formed input; bounds checks only trigger on truly out-of-bounds data |
| XMP parsing bounds changes | Could skip XMP from malformed images that previously "worked" | Golden tests; only changes behavior when pointer would read past buffer |
| PNG `longjmp` skips cleanup | File handles, png structs may leak on error | Explicit cleanup in `setjmp` error handler before `throw` |
| TIFF `memTiffWriteProc` returning 0 | libtiff may not handle gracefully | Test with ASan; libtiff's `TIFFWriteScanline` checks return and sets error state |
| TIFF `memTiffSeekProc` returning -1 | libtiff may continue with corrupted offset | Return -1 is the documented error convention for `lseek`-style callbacks |

**Dependencies:**
- No external dependencies. All changes are within the sipi repository
- Related to production hardening PRD (DEV-5913 family) but independent — can be deployed separately
- Built on test infrastructure from PR #519

**No follow-up issues** — all discovered bugs are now in scope (Phases A-I).

## Success Metrics

- Zero SIGABRT crashes from image format error handling in production (SIPI-G/H event count = 0 post-deploy)
- Server uptime unaffected by malformed JPEG/PNG/TIFF requests or client disconnections
- No C++ exceptions cross C library boundaries in any format handler path (JPEG read+write, PNG, TIFF)
- Zero buffer over-reads in metadata parsing (parse_photoshop, XMP) under ASan
- Golden tests confirm metadata output byte-for-byte identical after all fixes
- No new memory leaks detected by ASan in CI
- All TDD checkpoints (A through I) pass in CI pipeline

## References & Research

- **Sentry issues:** [SIPI-G](https://dasch.sentry.io/issues/SIPI-G) (JPEG state error), [SIPI-H](https://dasch.sentry.io/issues/SIPI-H) (SIGABRT)
- **JPEG error handling bugs:** `src/formats/SipiIOJpeg.cpp:1189,1217,1239,1282` (jpeg_finish_compress on error paths)
- **Callback UB:** `src/formats/SipiIOJpeg.cpp:305-318` (empty_html_buffer), `src/formats/SipiIOJpeg.cpp:326` (term_html_destination)
- **Missing catch-all:** `shttps/Server.cpp:689` (socket_request_processor), `shttps/Server.cpp:1280` (process_request)
- **Existing cleanup function:** `src/formats/SipiIOJpeg.cpp:347` (cleanup_html_destination)
- **FdGuard RAII:** `src/formats/SipiIOJpeg.cpp:40`
- **libjpeg error handling docs:** IJG libjpeg 9f — recommends `setjmp`/`longjmp` with extended `jpeg_error_mgr` for error_exit handlers
- **Similar PNG bug:** `src/formats/SipiIOPng.cpp:113-117` (sipi_error_fn throws through C)
- **Similar TIFF bug:** `src/formats/SipiIOTiff.cpp:87-155` (memTiffWriteProc throws through C)
- **Related PRD:** `specs/2026-03-14-sipi-production-hardening/01-sipi-production-hardening-PRD.md`
- **Related PRD:** `specs/2026-03-03-sipi-bilinn-segfault/01-sipi-bilinn-segfault-PRD.md` (different root cause — bilinn OOB, resolved)
- **Institutional learning:** `learnings/logic-errors/sipi-cache-auto-creation-head-request-empty-response.md` (Flush-or-Send pattern for HTTP responses)
- **Test infrastructure:** `test/e2e-rust/src/lib.rs` (SipiServer harness), `test/unit/sipiimage/imginfo_test.cpp` (existing JPEG unit tests)
- **parse_photoshop bounds bugs:** `src/formats/SipiIOJpeg.cpp:385-468`
- **XMP parsing UB:** `src/formats/SipiIOJpeg.cpp:661-670`
- **read() fd leak:** `src/formats/SipiIOJpeg.cpp:500-504`
- **icc_buffer leak:** `src/formats/SipiIOJpeg.cpp:601-709` (allocation) vs `708-709` (cleanup)
- **Approval test infrastructure:** `test/approval/` (existing ApprovalTests pattern)
- **Build commands:** `make nix-build`, `make nix-test`, `make rust-test-e2e`, `make nix-build-sanitized`, `make nix-test-sanitized`, `make test-smoke`
