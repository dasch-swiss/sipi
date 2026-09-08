---
title: "A red fuzz leg reports one finding at a time — fix it, re-dispatch, and expect the next one behind it"
date: 2026-09-08
category: "debugging"
component: "fuzzing"
module: "fuzz.yml (libFuzzer stops at the first crash per leg), `SipiIOTiff::readExif` / `read_tiled_data`, `SipiIOJ2k::write`"
problem_type: "triage-process"
severity: "high"
symptoms: "A single red `tiff` / `tiff_roundtrip` fuzz leg was read as one bug each. Fixing the visible finding and re-dispatching surfaced a different, more severe finding on the same leg twice in a row: behind 24 bytes of EXIF garbage sat a heap over-read in tiled TIFF decode; behind an ASan false positive sat a heap use-after-free that segfaults the production `sipi convert`."
root_cause: "libFuzzer aborts the leg on its first crash, so one nightly run can only ever show the shallowest finding per leg. A red leg is a queue, not a single defect, and the queue is only visible one element at a time. The triage loop must be fix → dispatch → read the next artifact, until the leg is green."
tags: [fuzzing, libfuzzer, triage, workflow_dispatch, crash-artifacts, asan, heap-use-after-free, heap-buffer-overflow, exif, tiff, kakadu, sipi, primary-evidence]
related:
  - ./asan-container-overflow-in-fuzz-binaries-is-a-link-artifact.md
  - ../testing/seam-mitigation-does-not-green-a-codec-fuzz-leg.md
  - ../architecture/kakadu-teardown-order-and-pod-templates-accepting-std-string.md
issue: "DEV-7080"
---

# A red fuzz leg reports one finding at a time — fix it, re-dispatch, and expect the next one

From the SIPI fuzz-nightly triage of 2026-09-08 (PR #804). The nightly had been red for nine
days. The initial triage found four causes across four legs. Three `workflow_dispatch` runs later,
the branch had fixed **four real bugs**, two of which were invisible in the initial artifacts
because a shallower finding on the same leg had stopped the fuzzer first.

## Problem

The 2026-09-08 nightly (run 34183871288) showed:

| Leg | Step | Finding |
|---|---|---|
| `tiff` | ASan | container-overflow in `Exif::addKeyVal<std::string>` via `SipiIOTiff::readExif` |
| `tiff_roundtrip` | ASan | container-overflow in `Exiv2::append` (a false positive, see the related learning) |
| `j2k`, `j2k_roundtrip` | libFuzzer | timeout in Kakadu's box parser (known class) |

After fixing the EXIF bug and neutralising the false-positive class, the first dispatch
(34203829133) turned the `tiff` leg into a **heap-buffer-overflow READ in `read_tiled_data`**.
After fixing that, the second dispatch (34208084507) turned `tiff_roundtrip` into a
**heap-use-after-free in `SipiIOJ2k::write`** that also segfaults the production CLI. Neither had
appeared in any earlier artifact.

## Investigation

Each round followed the same shape; the value was in doing it quickly and not stopping early:

1. **Get the artifact, not the log stream.** `gh run view --log` on these runs hits GitHub
   `stream error: CANCEL` roughly half the time. `gh run download <run> --name fuzz-crashes-<leg>`
   is reliable and contains the reproducers plus `libfuzzer.log` / `libfuzzer-asan.log` /
   `merge.log`. Artifacts expire after 30 days.
2. **Read the `SUMMARY:` lines, all of them.** The ASan logs also carry recoverable UBSan
   `runtime error:` lines from vendor code (Kakadu AVX2 alignment, lcms2 function-pointer types)
   that print and continue; the line that failed the step was further down. `grep -n 'SUMMARY\|ERROR: AddressSanitizer\|ERROR: libFuzzer'`
   first, then read around the right one.
3. **Reproduce with the production binary before reading code.** Every real finding here reproduced
   with `./bazel-bin/src/cli/sipi query|convert <artifact>` on macOS without any sanitizer: EXIF
   garbage visible in `query` output (`LensMake Ascii 24 <garbage>`), the tiled TIFF decoding to
   nonsense, the JPX write exiting 139. The false positive did not reproduce, which is what marked
   it as one.
4. **Fix, pin the verbatim reproducer as a fixture + seed + test, dispatch again.** Each round
   took roughly 40 minutes of CI. Three rounds: 4 legs red → 4 legs red (different reasons) →
   4 legs red (different reasons again) → 9 legs green.

## Root Cause

libFuzzer exits the process on the first crash or sanitizer report per leg, and the nightly runs
each leg once. So a leg's artifact shows exactly one finding, the one the mutator reached first,
which is usually the *shallowest* one on that leg's code paths. Fixing it does not make the leg
green; it makes the next finding reachable. Reading the initial red nightly as "four bugs" was
correct for that morning and wrong as a plan: the true count was six, two of them severe.

The four real bugs, for the record (all fixed in PR #804):

| Commit | Bug | Age |
|---|---|---|
| `d9c7704d` fix(metadata) | `Exif::addKeyVal<T>` read `&val, sizeof(T)` for `T = std::string`, storing the 24-byte string object as every TIFF EXIF ASCII tag | since 2016 |
| `ac3bf789` fix(cli) | offline verbs had no decode deadline; `sipi query/convert` on a hostile JP2 hung forever | since the watchdog landed |
| `1de76df9` fix(format_handlers) | `read_tiled_data` copied `TileWidth * TileLength * SamplesPerPixel` elements out of a `TIFFTileSize()`-sized buffer (32 bytes on the reproducer) | old |
| `6cd46ff0` fix(format_handlers) | `SipiIOJ2k::write` destroyed the codestream after `~jpx_target` freed its output on a Kakadu error → UAF, SIGSEGV in `convert` | old |

## Solution

Process, not code:

- Treat a red fuzz leg as a **queue**. Budget for at least two or three fix-and-dispatch rounds
  before declaring the nightly green, and say so in the plan or PR.
- **Dispatch on the branch after every fix** (`gh workflow run fuzz.yml --ref <branch>`), do not
  wait for the nightly. Each dispatch is ~40 min wall-clock and answers the question "what is
  behind this one".
- **Pin every reproducer** as `test/_test_data/images/malformed/<defect>.tif` + README row +
  `fuzz_seeds_<fmt>` entry + a `malformed_*_test.cpp` case asserting the clean rejection. The seed
  makes the next dispatch deterministic on that input; the test keeps the fix honest under the
  non-fuzz ASan job.
- Keep the **PR description** as the journal of what each round found (the commits carry only the
  fixes).

## Prevention

- **Never read a fuzz artifact as the complete list of defects on a leg.** It is the first one.
- **A green plain loop with a red ASan pass is normal**, not a contradiction: the ASan build is
  slower and catches classes the plain build cannot. Read the ASan artifact separately.
- **Reproduce with the production binary first.** Five minutes with `bazel-bin/src/cli/sipi`
  separates real defects from sanitizer artifacts before any code is read, and gives the exact
  user-visible symptom for the commit message.
- **Watch for shadowing between findings.** The `tiff_roundtrip` UAF sat behind a false positive
  for at least two nightlies; a suppression applied *without* re-dispatching would have shipped
  the UAF as "resolved".
- **Corollary for reviewers:** a fuzz-triage PR that fixes exactly the findings visible in the
  first red run, with no re-dispatch evidence, is probably incomplete.

## References

- PR #804, merged 2026-09-08. Fuzz runs: 34183871288 (nightly, initial triage), 34203829133
  (dispatch 1: tiled over-read + false-positive confirmation), 34208084507 (dispatch 2: JPX write
  UAF, fork-mode exit code, encode throughput), 34211319181 (dispatch 3: nine legs green).
- Fixtures: `tiff_tile_undersized.tif`, `tiff_minisblack_three_channel.tif`,
  `tiff_exif_make_jp2_roundtrip.tif` under `test/_test_data/images/malformed/`.
- Precedent from wave-2: `tiff_scanline_undersized.tif` (S2-08), the same class as the tiled
  over-read one strip-vs-tile away.
