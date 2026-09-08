---
title: "An ASan container-overflow report from a fuzz binary is a link artifact until a non-fuzz ASan build reproduces it"
date: 2026-09-08
category: "debugging"
component: "fuzzing"
module: "fuzz.yml ASan pass (`--config=fuzz --config=asan`), libFuzzer runtime from hermetic-llvm, libc++ container annotations"
problem_type: "sanitizer-false-positive"
severity: "medium"
symptoms: "The `tiff_roundtrip` fuzz leg failed under ASan with `container-overflow` WRITE in `Exiv2::append` on the memcpy immediately after `blob.resize()`. Later the same detector killed both `j2k` ASan legs at startup inside libFuzzer's own fork driver (`fuzzer::DirPlusFile` → `basic_string::append`) with zero inputs executed. Earlier, the `parse_request` leg had the same report class (DEV-7081)."
root_cause: "hermetic-llvm builds the libFuzzer runtime through `cc_unsanitized_library` (a transition that forces `//config:asan` off) against the same libc++ headers the instrumented code uses. libc++'s container annotations are then updated by some objects in the link and checked by others — the documented false-positive shape for this one ASan detector. No defect existed behind any of the three reports."
tags: [asan, container-overflow, false-positive, libfuzzer, fork-mode, libc++, annotations, hermetic-llvm, cc_unsanitized_library, exiv2, fuzzing, sipi, primary-evidence]
related:
  - ./parallel-e2e-shared-state-and-primary-evidence.md
  - ./fuzz-leg-reports-one-finding-at-a-time.md
  - ../testing/seam-mitigation-does-not-green-a-codec-fuzz-leg.md
issue: "DEV-7080"
---

# An ASan container-overflow report from a fuzz binary is a link artifact until a non-fuzz ASan build reproduces it

From the SIPI fuzz-nightly triage of 2026-09-08 (PR #804). The `fuzz.yml` nightly had been red
since 2026-08-31, and one of the reasons was an ASan `container-overflow` inside `Exiv2::append`
that contradicted the code it pointed at. Settling it took one deliberately designed CI
experiment, after which two more reports of the same detector appeared and confirmed the
mechanism. The transferable lesson: **ASan's container-overflow detector has a precondition that
the fuzz binaries in this repo do not meet, so its reports from them are not evidence of a bug.**

## Problem

The `tiff_roundtrip` ASan leg reported:

```
ERROR: AddressSanitizer: container-overflow on address 0x7ef0c6010800
WRITE of size 26 at 0x7ef0c6010800 thread T0
    #0 __asan_memcpy
    #1 Exiv2::append(std::vector<unsigned char>&, unsigned char const*, unsigned long) image.cpp:894
    #2 Exiv2::ExifParser::encode(...) exif.cpp:571
    #3 Sipi::Exif::exifBytes() src/metadata/cpp/exif.cpp:174
    #4 Sipi::SipiIOJ2k::write(...)
0x7ef0c6010800 is located 0 bytes inside of 65536-byte region  (allocated by blob.reserve in append)
```

exiv2 0.28.9's `append` is standards-correct:

```cpp
void append(Blob& blob, const byte* buf, size_t len) {
  if (len != 0) {
    Blob::size_type size = blob.size();
    if (blob.capacity() - size < len) blob.reserve(size + 65536);
    blob.resize(size + len);
    std::memcpy(&blob[size], buf, len);
  }
}
```

The shadow bytes said `[0, 26)` was still poisoned *after* `resize(26)`. Either libc++'s `resize`
did not update the annotation, or the report was wrong. The same 161-byte input converted to all
four output formats cleanly with the production binary.

## Investigation

1. **Read the code the report pointed at**, at the exact pinned version (upstream
   `v0.28.9/src/image.cpp`, not the possibly stale copy in the local Bazel output base — the two
   differed by 25 lines because the branch had bumped exiv2). Standards-correct; the report could
   not be true as stated.
2. **Ran the reproducer outside the fuzz binary** (`sipi convert repro.tif out.jp2` and three other
   formats). Clean. So whatever it was, the fuzz *link* was a variable.
3. **Checked how the fuzz link differs**: `--config=fuzz` links the libFuzzer runtime. In the
   hermetic-llvm module, `compiler-rt/BUILD.bazel` builds it via `cc_unsanitized_library`, whose
   `_reset_sanitizers` transition forces `//config:asan` (and every other sanitizer) to `False`,
   with `-O3` and `-fvisibility=hidden`, against the shared `:libcxx_headers`. libc++'s
   `__config_site` there carries `_LIBCPP_INSTRUMENTED_WITH_ASAN __has_feature(address_sanitizer)`,
   i.e. per-TU. That is precisely the "container used from code compiled without ASan" condition
   the sanitizer wiki names as the false-positive cause for this detector.
4. **Designed a decisive experiment instead of suppressing**: pinned the verbatim reproducer as a
   fixture (`tiff_exif_make_jp2_roundtrip.tif`), added a `formats_test` case that runs the exact
   decode-then-JP2-write path, and let the CI `asan-ubsan` job — which builds the same code under
   ASan but links **no libFuzzer** — be the arbiter. Green there plus red on the fuzz leg would mean
   link artifact; red there would mean a real defect.
5. **Outcome**: `asan-ubsan / amd64` green with the new test (PR #804 run 34203828500). The
   dispatched fuzz run (34203829133) then reproduced the same `Exiv2::append` report on a *second*
   input, and additionally killed both `j2k` ASan legs at startup with a `container-overflow` inside
   libFuzzer's own fork driver (`fuzzer::DirPlusFile` → `std::basic_string::append`), reported with
   `number_of_executed_units: 0`. Our code was not on that stack at all. Three reports, one
   detector, one mechanism, zero defects.

## Root Cause

ASan's container-overflow detector relies on libc++ calling `__sanitizer_annotate_contiguous_container`
from every code path that changes a container's `[size, capacity)` boundary. That only holds when
**every object in the link** that touches the container was compiled with ASan. The fuzz binaries
link the libFuzzer runtime unsanitized (by design of the toolchain — the fuzzer must not be
instrumented), sharing libc++ with the instrumented code. Annotations are then updated by some
objects and checked by others, and the detector fires on perfectly valid accesses. This is not
specific to exiv2 or to any SIPI code; the third report came from libFuzzer's own string handling.

## Solution

`chore(ci): keep the nightly fuzz legs green on known non-defects` (`8bb3b431`), the
container-overflow half:

- The `Fuzz under ASan (300s)` step composes `ASAN_OPTIONS` in the shell with
  `detect_container_overflow=0` for **every** leg, then appends the matrix `asan_options`
  (`detect_leaks=0` for the libjpeg/libpng legs). The earlier per-leg suppression on
  `parse_request` (DEV-7081) is folded into it.
- Heap, stack, and global overflow detection are unaffected. The container detector stays **on**
  for the `asan-ubsan` CI job's unit and e2e tests, which cover the same code without libFuzzer.
- The reproducer stays as fixture + `MalformedTiff.ExifMakeTiffEncodesToJp2Cleanly`, so the
  non-fuzz ASan job keeps proving the path clean, and `fuzzing.md` § Nightly CI records the
  reasoning.

## Prevention

- **Do not triage a `container-overflow` from a fuzz binary as a bug.** First run the reproducer
  through a non-fuzz ASan build (in this repo: pin it as a fixture, add a unit test on the same
  path, read the `asan-ubsan` job). Only a report that reproduces there is a defect. Every other
  ASan report class (heap/stack/global overflow, use-after-free) from the fuzz legs is real and
  has been in this same triage: two of them were.
- **When a report contradicts the code, suspect the instrumentation contract before the code.** The
  container detector, LSan (setjmp/longjmp libraries), and ASan's thread registry (short-lived
  threads in the Rust shell) all have documented false-positive shapes that this repo has now hit.
- **Read the vendor source at the pinned version.** The local Bazel output base may hold an
  earlier fetch; the ASan line numbers only matched upstream `v0.28.9`.
- **Prefer a decisive experiment over a suppression.** A suppression with no evidence behind it is
  a guess; a suppression with a green non-fuzz ASan test behind it is a documented decision, and the
  test keeps guarding the path.

## References

- PR #804 (`fix(metadata,cli,format_handlers): the four bugs behind the red fuzz nightly (DEV-7080)`),
  merged 2026-09-08.
- CI evidence: `asan-ubsan / amd64` green with the new test, run 34203828500; fuzz dispatch run
  34203829133 (second `Exiv2::append` instance and the j2k fork-driver instance); final green
  dispatch 34211319181.
- Fixture: `test/_test_data/images/malformed/tiff_exif_make_jp2_roundtrip.tif` (README row explains
  the outcome).
- Toolchain: hermetic-llvm `toolchain/runtimes/cc_unsanitized_library.bzl` (`_reset_sanitizers`),
  `compiler-rt/BUILD.bazel` `cc_library(name = "fuzzer", copts = FUZZER_CFLAGS)`.
- Sanitizer wiki: AddressSanitizerContainerOverflow (false positives when not all code is
  instrumented).
