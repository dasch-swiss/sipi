---
title: "Verify a 'duplicated code' finding with a set-diff before acting — SIPI's two Rust FFI mirrors are disjoint, not duplicated"
date: 2026-09-06
category: "architecture"
component: "ffi"
module: "src/ffi seam (sipi_ffi.h) ↔ src/server/rust/src/ffi.rs + src/scripting/rust/engine_ffi.rs"
problem_type: "review-finding-verification-and-ffi-abi-drift"
severity: "medium"
symptoms: "A dune review reported that engine_ffi.rs 'independently mirrors the exact same sipi_image_* family' as server/ffi.rs, implying a two-copies duplication bug. The ARCH-MAP was edited to record a 'hand-mirrored in two files' boundary rule off that claim — before anyone confirmed the two files actually share any declaration."
root_cause: "The finding was factually wrong: the two Rust mirrors of the C ABI are DISJOINT (zero shared functions), partitioned by consumer. The real, smaller residual is that extern-fn signatures are unchecked against the C header (only #[repr(C)] struct layouts are guarded), and consolidation onto one mirror is blocked by a dependency cycle."
tags: [ffi, seam, engine_sys, sipi_image, extern-c, repr-c, abi-drift, offset_of, code-review, primary-evidence, cbindgen, bindgen, dune, sipi]
related:
  - ../debugging/parallel-e2e-shared-state-and-primary-evidence.md
issue: "DEV-7131"
---

# Verify a "duplicated code" finding with a set-diff before acting on it

During a `/dune:review` of SIPI (DEV-7131), a reviewer sub-agent reported that
`src/scripting/rust/engine_ffi.rs` is "a second, independent mirror of the **exact same**
`sipi_image_*` family" already declared in `src/server/rust/src/ffi.rs" — framed as a
two-writers duplication trap with silent drift. Acting on it, `ARCH-MAP.md` gained a boundary
rule asserting the `sipi_image_*` handle family "is hand-mirrored in **two** Rust files." The
map was edited **before** anyone checked whether the two files share a single declaration. They
don't. This is a near-miss: a plausible, specific-sounding review finding that was wrong on the
facts, propagated into the architecture map, and had to be corrected.

## Problem

The Rust shell drives the C++ image engine over a hand-mirrored `extern "C"` seam
(`src/ffi/cpp/sipi_ffi.h`, built as `//src/ffi:sipi_ffi`). The Rust side of that seam lives in
two crates. The review claimed they duplicate the `sipi_image_*` family; the natural follow-up
question was "then can scripting just reuse `server/ffi.rs` and delete the copy?" Neither the
duplication claim nor the reuse assumption had been verified.

## Investigation

One deterministic command settled it — the set-difference of the `extern "C"` function names each
file declares:

```sh
comm -12 \
  <(grep -oE 'fn sipi_[a-z_]+' src/server/rust/src/ffi.rs        | sort -u) \
  <(grep -oE 'fn sipi_[a-z_]+' src/scripting/rust/engine_ffi.rs  | sort -u)
# → empty
```

The overlap is **empty**:

- `server/ffi.rs` declares **21** functions — the serve + config-getter seam: `sipi_serve_image`,
  `sipi_serve_file`, `sipi_init`, `sipi_metrics_snapshot`, `sipi_docroot`, `sipi_admission_mode`,
  `sipi_image_dims`, …
- `scripting/engine_ffi.rs` declares **18** functions — the image-handle family the Lua bindings
  call: `sipi_image_new`, `sipi_image_crop`, `sipi_image_scale`, `sipi_image_rotate`,
  `sipi_image_watermark`, `sipi_image_write`, `sipi_image_send`, `sipi_image_free`,
  `sipi_file_mimetype`, `sipi_filename_hash`, …

The `sipi_image_` *prefix* appears in both, which is what the review pattern-matched on — but
`sipi_image_dims` (server) and `sipi_image_handle_dims`/`sipi_image_file_dims` (scripting) are
**different functions**. No function is declared twice. The mirrors are disjoint partitions of the
C ABI, each owned by its sole consumer.

The reuse idea was doubly impossible, also verifiable in seconds:

1. The handle decls scripting needs **aren't in `server/ffi.rs` at all** — server never calls them.
2. `server` **depends on** `scripting` (`src/server/rust/BUILD.bazel` → `//src/scripting/rust`,
   because server builds the `LuaEnv`), so scripting depending back on server would be a build
   cycle.

## Root Cause

The finding conflated "shares a name prefix" with "declares the same symbol." The genuine, much
smaller issues underneath it:

- **Function signatures are unguarded against the C header on *both* sides.** Only the `#[repr(C)]`
  struct *layouts* are pinned (via `offset_of!`/`size_of` asserts) — `server/ffi.rs` had **97** such
  asserts; `scripting/engine_ffi.rs` had **0**. A C++ signature change not mirrored by hand is a
  silent ABI mismatch, but that is a property of hand-mirrored FFI generally, not a duplication bug.
- **The Rust half of the seam has no home in the `ffi` component** — it is split across two other
  components (`server`, `scripting`), so "the single contract" the map claims is not structurally
  true.

## Solution

- **Correct the map to the verified facts.** The boundary rule now states the seam is hand-mirrored
  in **two disjoint** files partitioned by consumer, with no function in both, and that only struct
  layouts (not fn signatures) are mechanically guarded.
- **Downgrade the review finding.** It was recorded as a legibility/clarity item, not a
  duplication/correctness bug (no duplicated declaration exists to drift).
- **Record the real improvement as [ADR-0026](../../adr/0026-engine-seam.md)** — a *clarity*
  investment, not a bug fix: rename the `ffi` boundary component to `engine`, single-source the
  Rust seam in `//src/engine/rust:engine_sys` (a leaf both `server` and `scripting` depend on —
  cycle-free), and add a signature-drift guard (cbindgen-diff, with bindgen as the recorded
  fallback). Consolidation is worthwhile for the single-home clarity and the uniform guard, framed
  honestly as such rather than as fixing duplication that isn't there.

## Prevention

- **Before editing anything off a "duplicated / same code in two places" finding, run the
  set-diff.** `comm -12` of the two symbol sets (or a `diff` of the two regions) is a seconds-long
  deterministic check. A specific-sounding claim ("the *exact same* family") is still a claim —
  verify the fact before it propagates into the map, an ADR, or a refactor. Sub-agent and reviewer
  findings are inputs to verify, not conclusions to act on. (Same primary-evidence discipline as
  [the wave-2 debugging goose chase](../debugging/parallel-e2e-shared-state-and-primary-evidence.md).)
- **When asked "can X reuse Y's declarations?", check two things independently:** does Y actually
  contain what X needs, and does the dependency direction allow X → Y without a cycle? Either alone
  can kill the idea; here both did.
- **Distinguish "shares a name prefix" from "declares the same symbol."** Prefix overlap
  (`sipi_image_*`) is not duplication; compare the full symbol names.
- **A hand-mirrored FFI seam guards struct layouts but not fn signatures.** If you want the
  signature guarantee, that is a deliberate mechanism (generate-and-diff via cbindgen or bindgen),
  not something the existing `offset_of!` tests give you. Know which guarantee you actually have.
- **Frame a consolidation honestly.** "Single home + uniform guard" is a real clarity win; do not
  sell it as fixing a duplication bug when the code is disjoint — the justification changes the
  scope and the urgency.

## Verification

The disjoint-declarations claim is a code fact at the reviewed commit, re-derivable with the
`comm -12` command above (empty output = disjoint). The dependency-cycle claim:
`grep -n scripting src/server/rust/BUILD.bazel` shows `//src/scripting/rust` as a `server`
dependency (so scripting cannot depend on server). Both checked 2026-09-06 on the DEV-7131 branch.

## References

- [ADR-0026](../../adr/0026-engine-seam.md) (engine-seam extraction) — and the phased implementation plan it links.
- The seam files: `src/ffi/cpp/sipi_ffi.h`, `src/server/rust/src/ffi.rs`,
  `src/scripting/rust/engine_ffi.rs`.
- Companion learning: [primary evidence over plausible hypotheses](../debugging/parallel-e2e-shared-state-and-primary-evidence.md).
