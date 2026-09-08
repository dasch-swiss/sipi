---
title: "A mitigation at the FFI seam does not make a codec-level fuzz leg green — the harness runs below the seam, so tolerate the known class in fork mode"
date: 2026-09-08
category: "testing"
component: "fuzzing"
module: "fuzz.yml j2k / j2k_roundtrip legs, `codec_fuzz_harness.h`, `run_with_deadline` in `serve_image.cpp` / `decode_guard.h`"
problem_type: "fuzz-gate-design"
severity: "medium"
symptoms: "Every fuzz.yml nightly from 2026-08-31 to 2026-09-08 was red on the j2k legs with `libFuzzer: timeout` inside Kakadu `jp2_input_box::read_box_header`, although the DEV-7080 decode-hang watchdog had landed and the design note said the leg would go green once it did. After switching to fork mode, the legs still exited 70 with zero crashes; and the jpeg_roundtrip leg timed out on a 621-byte input."
root_cause: "The watchdog is a wall-clock deadline at the FFI seam. The codec fuzz harness calls `SipiIOJ2k::read_shape` directly, below that seam, so every fresh instance of the unpatchable Kakadu hang class still hits libFuzzer's per-input timeout. Fork mode tolerates timeouts but the parent exits with its last child's exit code. Tiny inputs can declare huge frames, so re-encoding under ASan blows the per-input budget on throughput alone."
tags: [fuzzing, libfuzzer, fork-mode, ignore_timeouts, timeout_exitcode, kakadu, hang, watchdog, seam, deadline, harness, encode-budget, ci, sipi]
related:
  - ../debugging/asan-container-overflow-in-fuzz-binaries-is-a-link-artifact.md
  - ../debugging/fuzz-leg-reports-one-finding-at-a-time.md
issue: "DEV-7080"
---

# A mitigation at the FFI seam does not make a codec-level fuzz leg green

From the SIPI fuzz-nightly triage of 2026-09-08 (PR #804). The security-hardening wave-2 work had
"fixed" the DEV-7080 JP2 decode hang and removed the `continue-on-error` from the j2k fuzz leg,
expecting it to go green. It could not: the fix and the fuzz harness live on different sides of a
boundary. Three concrete things follow, all of which are now encoded in `fuzz.yml` and
`docs/src/development/fuzzing.md`.

## Problem

Nightly `fuzz.yml` runs 34354275318 (2026-08-31) through 34183871288 (2026-09-08) all failed the
`j2k` leg (and `j2k_roundtrip` once it existed) in the plain libFuzzer step:

```
==NNN== ERROR: libFuzzer: timeout after 25 seconds
    #7  kdu_supp::jp2_input_box::read(unsigned char*, int)
    #8  kdu_supp::jp2_input_box::read_box_header(bool)
    #10 kd_supp_local::jx_source::finish_jp2_header_box()
    #13 kdu_supp::jpx_source::access_codestream(int, bool)
    #14 Sipi::SipiIOJ2k::read_shape(std::string const&)
    #15 sipi::fuzz::run_decode<Sipi::SipiIOJ2k>(...)
```

Each night produced a fresh `timeout-*` reproducer of the same class already pinned in
`test/_test_data/images/hang/`. `sipi query` on any of them burns CPU until killed (reproduced
locally at 20 s and, after the CLI fix, at exactly the 120 s deadline).

## Root Cause

Three separate facts, discovered in sequence:

1. **The mitigation is at the seam; the harness is below it.** `run_with_deadline` (now in
   `src/ffi/cpp/decode_guard.h`) bounds `read_shape`/`read` for requests that enter through
   `serve_image.cpp` (and, after PR #804, for the offline CLI verbs through
   `cli/commands/decode_deadline.h`). `codec_fuzz_harness.h` constructs the handler and calls
   `handler.read_shape(path)` directly. No deadline exists on that path, by design: the harness is
   meant to exercise the codec, not the seam. Kakadu is licence-gated and cannot be patched, so the
   hang class itself is permanent. The design note's "leg goes green once the deadline lands" was
   a category error.
2. **libFuzzer fork mode exits with its last child's code.** `-fork=1 -ignore_timeouts=1` made the
   parent keep fuzzing past timed-out children (`FuzzerFork.cpp`: `if (Options.IgnoreTimeouts &&
   ExitCode == Options.TimeoutExitCode) Env.NumTimeouts++;`), but at the end it does
   `exit(ExitCode)` where `ExitCode` is whatever the *last* job returned. When the final child had
   timed out, the step failed with 70 despite `oom/timeout/crash: 0/8/0`. Dispatch run 34208084507
   showed this on both j2k legs.
3. **A few hundred bytes can declare a 50 MB frame.** A 621-byte JPEG with a 65056x255 header
   decodes in milliseconds into ~50 MB of pixels. The round-trip harness then re-encodes to tif,
   jpx, png and jpg; under the `-c dbg` ASan build those four encodes exceed `-timeout=25`. On the
   optimised production binary the same four conversions take 0.5–1.8 s each. Not a hang, a
   throughput budget.

## Solution / the pattern

`chore(ci): keep the nightly fuzz legs green on known non-defects` (`8bb3b431`) and
`test(format_handlers): cap the round-trip fuzz harness re-encode at 16 MiB of pixels` (`499523fd`):

- **Fork mode, timeouts tolerated, on the j2k legs only.** Matrix column `libfuzzer_extra` set to
  `-fork=1 -ignore_timeouts=1 -ignore_ooms=0 -timeout_exitcode=0` for `j2k` and `j2k_roundtrip`,
  applied to both the plain and the ASan loop. The timed-out child still runs
  `DumpCurrentUnit("timeout-")` before `_Exit(Options.TimeoutExitCode)`, so every instance is
  recorded in the `fuzz-crashes-<target>` artifact of a **green** leg. Crashes (`-ignore_crashes`
  stays 0) and OOMs still fail. The other seven legs keep the strict per-input timeout: a hang in
  libtiff/libjpeg/libpng or in SIPI's own code is a fixable finding.
- **`-timeout_exitcode=0`** on top of `-ignore_timeouts=1`, because of fact 2. Confirmed by reading
  `FuzzerFork.cpp` and `FuzzerLoop.cpp::AlarmCallback` in the vendored llvm-project, not assumed.
- **`kMaxRoundtripEncodeBytes = 16 MiB`** in `codec_fuzz_harness.h`: `run_roundtrip` skips its four
  re-encodes when `nx * ny * nc * bytes_per_sample` exceeds it, mirroring the existing
  `kMaxHarnessDecodeBytes` budget. The round-trip legs hunt encoder header/marker/metadata bugs
  (S2-02, S2-14), which do not need bulk pixel volume.

Verified: dispatch 34211319181 — all nine legs green, plain and ASan; the j2k plain loops recorded
their tolerated timeouts as artifacts.

## Prevention

- **Ask where the fix lives relative to the test that is red.** A seam-level guard (deadline,
  memory budget, admission control) protects requests, not the codec entry points a unit test or
  fuzz harness calls directly. Before writing "leg goes green once X lands" into a plan, check that
  the leg's call path actually passes through X.
- **A known, unfixable hang class needs an explicit policy in the gate, not a red badge.** Options
  were: tolerate at the harness (hides hangs in *our* JP2 code too, duplicates the seam mechanism),
  keep red (zero signal), or fork mode with tolerated timeouts (records instances, still fails on
  crashes). The third is the only one that keeps the leg informative.
- **Read the fuzzer's exit-code semantics from the source you build.** libFuzzer's flags have
  interactions (`ignore_timeouts` vs the parent's final `exit(ExitCode)`) that the flag help does
  not spell out. The vendored `compiler-rt/lib/fuzzer/` under the Bazel output base is the primary
  source.
- **Budget the harness by decoded volume, not input size.** Fuzz inputs are small by construction;
  what they *declare* is not. Any encode/transform step in a harness needs a cap derived from the
  decoded geometry.
- **Keep hang fixtures out of every corpus.** `test/_test_data/images/hang/` is loaded by no
  corpus-replay target; seeding one there would hang `bazel test //src/...`.

## References

- PR #804, merged 2026-09-08; commits `8bb3b431` (ci), `499523fd` (harness cap).
- `docs/src/development/fuzzing.md` § Nightly CI (fork-mode and encode-budget paragraphs).
- `docs/specs/2026-09-04-02-jp2-decode-watchdog-design.md` — the seam deadline design whose
  "Follow-through" expected the j2k leg to go green.
- Fuzz runs: last green nightly before the fix 33290462514 (2026-08-30); first red 33354275318;
  fork-mode exit-code failure 34208084507; first fully green dispatch 34211319181.
- libFuzzer: `FuzzerFork.cpp` (`exit(ExitCode)` after the job loop), `FuzzerFlags.def`
  (`ignore_timeouts` default 1 in fork mode, `timeout_exitcode` default 70).
