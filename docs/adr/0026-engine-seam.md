---
status: proposed
---

# The shell↔engine seam becomes the `engine` component: a C++ ABI side and a single Rust bindings crate, with a signature-drift guard

## Context

The boundary between the Rust shell and the C++ image engine is defined C++-side in
`src/ffi/cpp/sipi_ffi.h` (+ `serve_image.cpp`, `image_handle.cpp`, `init.cpp`, …), built
as `//src/ffi:sipi_ffi`. The **Rust** view of that same boundary — the `extern "C"`
declarations and the `#[repr(C)]` structs/enums — is hand-mirrored across **two** consumer
crates:

- `src/server/rust/src/ffi.rs` — the serve entries + config getters (`sipi_serve_image`,
  `sipi_init`, `sipi_metrics_snapshot`, …), the request/response/metrics `#[repr(C)]`
  types, and 97 `offset_of!`/`size_of` layout-lock assertions.
- `src/scripting/rust/engine_ffi.rs` — the `sipi_image_*` opaque-handle family the Lua
  `SipiImage` bindings call, plus its one `#[repr(C)]` handle type — with **no** layout
  tests. (`ImageHandle` RAII + `GpsValue`/`ExifValue` result enums there are consumer-side
  Rust ergonomics, not seam ABI.)

The two declaration sets are **disjoint** — no `sipi_*` function is declared in both — so
there is no active duplication bug. Three legibility costs motivate the change:

1. **The component that owns the C++ boundary does not own its Rust half.** The map calls
   this "the single contract between the shell and the engine", yet the Rust view of that
   contract has no home in the component — it is scattered across `server` and `scripting`.
   "Where is the Rust view of the C ABI?" has two answers.
2. **Guards are asymmetric and function signatures are unguarded everywhere.** `server`'s
   seam structs are pinned by 97 `offset_of!` asserts; `scripting`'s seam type has none.
   And on *both* sides the `extern "C"` **function signatures** are checked against the C++
   header by nothing — only struct *layouts* are guarded. An unmirrored C++ signature
   change is a silent ABI mismatch (undefined behaviour at call time).
3. **The component name `ffi` carries no information.** Everything at this boundary is FFI;
   `ffi` names the mechanism, not the thing. Combined with the lib `sipi_ffi`, the header
   `sipi_ffi.h`, and two `*ffi.rs` files, the boundary reads as an undifferentiated pile of
   "ffi"/"sipi" with no signal of which side (Rust or C++) a given name belongs to.

## Decision

1. **Rename the boundary component `ffi` → `engine`** — the thing the shell reaches, and
   the word the map already uses (`EngineContext`). The directory marks the side; the name
   marks the role:
   - `src/ffi/` → `src/engine/`; C++ side `src/engine/cpp/` (incl. `engine_context.{h,cpp}`).
   - C++ ABI lib `//src/ffi:sipi_ffi` → **`//src/engine:abi`**; header `sipi_ffi.h` →
     **`engine/abi.h`** (`#include "engine/abi.h"`, include prefix `engine/`).
   - The `extern "C"` **symbols keep the `sipi_` prefix** — that is the C link namespace
     (collision avoidance), not a side marker; renaming it is large and pointless.
   - To avoid two "engine"s, the map's `image` entry is reworded from "the image engine
     hub" to "the `SipiImage` value-type hub"; **engine** is reserved for this boundary.

2. **Extract the Rust side into one crate, `//src/engine/rust:engine_sys`** (crate
   `engine_sys`) — the ADR-0021 colocated-polyglot pattern (`engine/cpp` + `engine/rust`,
   like `iiifparser`). It holds the **raw seam only**: every `extern "C"` declaration,
   every `#[repr(C)]` type, and the layout-lock tests for all of them. It becomes the first
   Rust→C++ link and carries the C++-stdlib link bracket + the engine's transitive linkopts
   (moved off `server`). `server` and `scripting` depend on it and `use engine_sys::*`;
   consumer-ergonomic layers stay put and are renamed to the safe-wrapper role
   (`server/ffi.rs` → **`server/engine.rs`**; `scripting/engine_ffi.rs` →
   **`scripting/engine.rs`**) — the idiomatic `engine_sys` (raw) → `engine` (safe) pair.

3. **Add a mechanical signature-drift guard.** The raw seam stays hand-authored, so add a
   CI gate that fails when `engine_sys` and `engine/abi.h` disagree on any function
   signature or struct layout. Preferred mechanism: **cbindgen-diff** — generate a C header
   from `engine_sys` and diff it against `engine/abi.h`. Because cbindgen is built to emit
   headers for *exported* Rust symbols (not `extern` *import* blocks), a Phase-0 feasibility
   spike proves it can emit the import surface and defines a robust diff normalization
   **before** the mechanism is committed. Recorded fallback if it can't: **bindgen** —
   generate `engine_sys`'s raw decls *from* `engine/abi.h`, check the generated file in, and
   diff on regenerate — which closes drift by construction (Rust derived from the
   authoritative C++ header). Neither tool is in the graph today; the guard adds one,
   dev/CI-only.

## Alternatives considered

- **Status quo (two disjoint hand-mirrors, name `ffi`).** Works; no duplication. Rejected
  for the three costs above — but it is a clarity investment, not a bug fix, so deferring
  is defensible.
- **Keep the `ffi` name, only extract the crate.** Solves single-sourcing but leaves the
  uninformative name and the "no home in the component" framing half-addressed.
- **A shared crate outside the component.** Rejected — it wouldn't give the boundary a Rust
  home; `src/engine/rust/` does, and matches ADR-0021.
- **bindgen as the default (C++ header → Rust).** More natural given C++ is authoritative,
  and eliminates hand-mirroring for the raw decls. Recorded as the fallback rather than the
  default only to preserve the repo's deliberate hand-authored seam style unless the spike
  shows cbindgen is impractical.
- **Keep only layout guards, do nothing about signatures.** Leaves the silent-ABI-mismatch
  gap; since we are already restructuring the seam, closing it in the same effort is right.

## Consequences

- One home for the Rust view of the boundary; the "single contract" becomes structurally
  true; the `engine` component owns both languages, and every name signals its side.
- All seam types get uniform layout guards (closing `scripting`'s 0-guard gap), and — for
  the first time — function signatures are mechanically checked against the C++ header.
- **A wide, mechanical rename** (`ffi` → `engine`, `sipi_ffi.h` → `engine/abi.h`,
  `//src/ffi:sipi_ffi` → `//src/engine:abi`) touches every consumer `#include`, the
  server/scripting/cli BUILD deps, the map, and several ADRs (0013/0020/0021/0022). It is
  behaviour-preserving and verified by the three-platform build; it lands as its own phase
  ahead of the extraction so the mechanical move is isolated from the logic move.
- The first Rust→C++ link moves from `server` to `engine_sys` (link bracket + transitive
  linkopts + the sanitizer-build exclusions move with it).
- A new dev/CI tool (cbindgen or bindgen) enters the toolchain; the guard is a CI gate plus
  a `just` target (per the "every CI gate gets a local just-target" rule).
- `server`/`scripting` shrink to `use engine_sys::*` plus their own wrappers; a signature
  change is a single edit in `engine_sys` that both consumers inherit.
