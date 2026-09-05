---
title: "Rename the FFI boundary to the `engine` component and single-source its Rust seam in engine_sys"
date: 2026-09-05
author: Ivan Subotic
status: draft
repositories: []
---

# The `engine` seam: rename `ffi` → `engine`, single-source the Rust seam in `engine_sys`, guard it

Implements [ADR-0026](../adr/0026-engine-seam.md). Resolves the structural half
of the DUNE-001 review finding (the Rust view of the C++ boundary is split across two
consumer crates with no home in the component and no signature guard) and the "ffi is a
generic, uninformative name" cost.

**Ships as one PR**, one commit per phase (all-or-nothing, per standing practice). The
phases are the orchestrator's chunks; they are ordered so the wide mechanical rename lands
and verifies *before* the logic move.

**Nature:** behaviour-preserving rename + refactor + one new CI gate. No change to the seam
surface (same functions, structs, enums — relocated, renamed, and guarded). No hot-path
change → no benchmark. The `sipi_` prefix on the `extern "C"` symbols is kept (C link
namespace).

## Current state (verified at HEAD e71938bf)

- Component `ffi` = `src/ffi/`; C++ ABI lib `//src/ffi:sipi_ffi`, header `sipi_ffi.h`
  (`#include "ffi/…"`), plus `engine_context.{h,cpp}`.
- `server/ffi.rs`: 21 `extern "C"` fns (serve + config) + `#[repr(C)]` `SipiResponse`/
  `SipiIiifParams`/`SipiMetricsSnapshot`/`SipiServeRequest`/`SipiImageDims`/`SipiServeTimings`/
  `SipiImageErrorReport` + enums + **97** layout asserts.
- `scripting/engine_ffi.rs`: 18 `extern "C"` fns (`sipi_image_*` handle family + file
  helpers) + **1** `#[repr(C)]` handle type + **0** layout asserts. `ImageHandle`,
  `GpsValue`, `ExifValue` are consumer wrappers, NOT seam ABI — they stay.
- Decl-set overlap: **empty**. Both crates depend on `//src/ffi:sipi_ffi`. `server` is the
  first Rust→C++ link and carries the C++-stdlib link bracket + transitive linkopts.
- Neither cbindgen nor bindgen is in `MODULE.bazel`.

## Target naming

| Piece | Now | Target |
|---|---|---|
| Component dir | `src/ffi/` | `src/engine/` |
| C++ ABI lib | `//src/ffi:sipi_ffi` | `//src/engine:abi` |
| C++ header | `sipi_ffi.h` (`ffi/…`) | `engine/abi.h` (`engine/…`) |
| Rust raw bindings | *(new)* | `//src/engine/rust:engine_sys` (crate `engine_sys`) |
| Rust safe wrappers | `server/ffi.rs`, `scripting/engine_ffi.rs` | `server/engine.rs`, `scripting/engine.rs` |
| C ABI symbols | `sipi_*` | unchanged |

## Phase 0 — signature-guard feasibility spike (decision gate)

- [ ] Prototype whether **cbindgen** can emit the seam's `extern "C"` *import* surface +
      `#[repr(C)]` types, or only *exported* Rust symbols.
- [ ] Define a robust diff/normalization against `engine/abi.h`'s `extern "C"` surface
      (type spellings, ordering, comments) — or conclude a byte-diff needs a canonicalizer.
- [ ] **Ruling:** cbindgen-diff vs the bindgen fallback (ADR-0026). Record it in
      `2026-09-05-01-engine-seam-journal.md`; if the fallback is chosen, amend ADR-0026's mechanism line.
- **Gate:** the guard mechanism is chosen before Phase 4. Phases 1–3 may proceed
      regardless (they don't depend on the guard).

## Phase 1 — rename component `ffi` → `engine` (pure mechanical, behaviour-preserving)

- [ ] `git mv src/ffi src/engine`; C++ side stays under `src/engine/cpp/` (incl.
      `engine_context.{h,cpp}`).
- [ ] Rename the ABI lib target `sipi_ffi` → `abi` and the header `sipi_ffi.h` →
      `engine/abi.h`; set the include prefix to `engine/` (`strip_include_prefix` +
      `include_prefix = "engine"`).
- [ ] Update every `#include "ffi/sipi_ffi.h"` → `#include "engine/abi.h"` and every
      `//src/ffi:sipi_ffi` dep (server, scripting, cli BUILD files) → `//src/engine:abi`.
- [ ] No `.rs`/logic changes; the consumer `*ffi.rs` files are renamed in Phase 3 with
      their rewrite.
- [ ] `just bazel-build` + `//src/...` sweep green on all three platforms — proves the
      rename is behaviour-preserving before any logic moves.

## Phase 2 — create `//src/engine/rust:engine_sys` (raw seam; consumers not yet rewired)

- [ ] New `src/engine/rust/` package with a `rust_library` `engine_sys` `BUILD.bazel`
      (colocated per ADR-0021; docstring).
- [ ] Move every `extern "C"` decl from `server/ffi.rs` + `scripting/engine_ffi.rs` into
      `engine_sys` (raw seam only — leave the consumer wrappers behind for Phase 3).
- [ ] Move every `#[repr(C)]` struct/enum crossing the seam into `engine_sys`.
- [ ] Move the 97 layout tests; **add** layout tests for `scripting`'s seam handle type
      (closing the 0-guard gap).
- [ ] `engine_sys` links `//src/engine:abi` and carries the C++-stdlib link bracket +
      transitive linkopts (moved off `server`), incl. the sanitizer-build exclusions.
- [ ] `engine_sys` builds + layout tests pass on all three platforms (the cross-platform
      `sizeof` divergence must still assert per platform).

## Phase 3 — rewire consumers

- [ ] `git mv server/ffi.rs server/engine.rs`: delete the moved decls; `use engine_sys::*`;
      keep `From<IiifParams>` flattening + server-only helpers. `server` depends on
      `engine_sys` and is no longer the first Rust→C++ link.
- [ ] `git mv scripting/engine_ffi.rs scripting/engine.rs`: delete the moved decls;
      `use engine_sys::*`; keep RAII `ImageHandle` + `GpsValue`/`ExifValue` wrappers.
- [ ] Update both `BUILD.bazel` deps (+ `//src/engine/rust:engine_sys`; relocate the link
      bracket) and any `mod`/`use` paths.
- [ ] Full `bazel-build` + `//src/...` + e2e green on all three platforms; **both** lint
      gates (`bazel-rustfmt-check`, `bazel-clippy-check`).

## Phase 4 — signature-drift guard (mechanism from Phase 0)

- [ ] Add the chosen codegen tool (cbindgen or bindgen) to `MODULE.bazel` (dev/CI-only).
- [ ] Wire the generate-and-diff check as a Bazel test/target **and** a
      `just engine-seam-check` recipe (local mirror of the CI gate).
- [ ] Add it to CI (`ci.yml`); document it in `developing.md` + the `engine_sys` docstring.
- [ ] Prove it **fails** on an injected one-off signature drift, then reverts green.

## Phase 5 — map + docs

- [ ] `/dune:map update`: rename the `ffi` component → `engine` (paths/lib/header/entities);
      add `engine_sys` as the single Rust seam home (**replaces** the "two disjoint mirrors"
      boundary rule); reword the `image` entry "image engine hub" → "`SipiImage`
      value-type hub" (reserve "engine" for the boundary); update `server`/`scripting`
      entries (they `use engine_sys`); refresh the top-level dependency arrow
      (`… → //src/engine:abi → //src/image`) and the cross-cutting "FFI seam" note.
- [ ] Scrub the "strangler-fig; ADR-0013" oracle-era framing from the (renamed) `engine`
      BUILD docstring while touching it (a known DUNE FYI).
- [ ] `CONVENTIONS.md`/`CONTEXT.md`/`UBIQUITOUS_LANGUAGE.md`: update any `ffi`/`sipi_ffi`
      references to the `engine` names.

## Acceptance criteria

- [ ] No `src/ffi/`, `//src/ffi:sipi_ffi`, or `#include "ffi/…"` remains; the boundary is
      the `engine` component (`//src/engine:abi`, `engine/abi.h`).
- [ ] One Rust crate (`//src/engine/rust:engine_sys`) declares the entire seam; `server`
      and `scripting` hold zero raw `extern "C"` seam decls and `use engine_sys`.
- [ ] Every seam `#[repr(C)]` type is layout-guarded (incl. `scripting`'s, previously not).
- [ ] The signature-drift guard is a CI gate proven to fail on drift, with a local `just`
      mirror.
- [ ] Three-platform `bazel-build` + `bazel-coverage` + e2e green; both lint gates green.
- [ ] `ARCH-MAP.md` reflects the `engine` component + `engine_sys`; no two-"engine"
      collision (the `image` entry is reworded).

## Risks & notes

- **The Phase-1 rename is wide** (every `#include`, the lib label, the map, ADRs
  0013/0020/0021/0022). It is mechanical and behaviour-preserving; isolating it as its own
  phase/commit keeps the diff reviewable and the logic move clean.
- **cbindgen import-surface feasibility** is the main unknown → Phase-0 gate; bindgen
  fallback recorded.
- **The link-bracket move** (first Rust→C++ link relocates `server` → `engine_sys`) is the
  riskiest BUILD step — verify three-platform and under the sanitizer config; move the
  asan/ubsan exclusions too.
- **Cross-platform `sizeof` divergence** (mtime layout etc.) — moved layout tests must keep
  asserting per platform.
- **No behaviour change** — pure rename/relocation + a new gate. No benchmark required.
- **Effort (conservative):** a multi-day change dominated by Phase 1 (rename) and Phases
  2–3 (extraction). One PR; per-phase commits.

## Out of scope

- cbindgen vs bindgen is Phase 0's ruling, not decided up front.
- Full generated bindings beyond the seam surface; no whole-engine codegen switch.
- Any change to the seam surface itself, or renaming the `sipi_` C symbols.
