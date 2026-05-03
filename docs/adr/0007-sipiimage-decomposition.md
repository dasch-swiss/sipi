---
status: proposed
---

# `SipiImage` decomposition into `image/` + `image_processing/`

The `SipiImage` god-object (~2,526 lines of `.cpp` + `.hpp`, six distinct responsibility groups in one class) is decomposed into:

- **`src/image/`** — a pure value type (geometry, photometric, RAII pixel buffer, metadata composite) with ~15 public methods. No image-processing behaviour, no I/O facade, no HTTP integration.
- **`src/image_processing/`** — free functions over `const Image&` for crop, scale, rotate, colour conversion, channel ops, bit-depth reduction, dithering, watermark application, comparison, arithmetic.

Three concerns leave the class entirely:

- The static `io` registry (extension → format handler) moves to `format_handlers/` (registry of self).
- The `shttps::Connection*` field disappears (replaced by `OutputSink::HttpSink` per ADR-0006).
- The `app14_transform` JPEG-specific marker field moves into the JPEG decode pipeline (consumed at decode, inverted before `Image` is "complete"; downstream sees standard CMYK).

We accept this for five reasons.

**1. Six responsibility groups in one class is the textbook god-object shape.** Image data, metadata, format I/O facade, image processing, HTTP integration, and arithmetic are conceptually separable; co-locating them costs every consumer (heavy header transitively-includes, broken encapsulation via 5 friend declarations, raw-pointer pixel ownership requiring manual memory-management ceremony, leaky JPEG-specific field in the universal type). The accumulation is historical (the class is the SIPI core since 2016 and has absorbed every responsibility that touched images) rather than designed.

**2. Free functions over a value type is the Rust-aligned shape.** `image.crop(...)` doesn't survive the Rust port directly (Rust doesn't have inheritance-based dispatch on values). `Sipi::processing::crop(image, ...)` maps cleanly to a Rust trait method or free function. Per the [Rust-aligned, transitional C++ method invariant](../deep-modules.md#method-invariants-dont-drift-on-these-across-sessions).

**3. The HTTP-output dual-write optimization is preserved through `OutputSink::TeeSink` composition.** Today's encoder writes its byte stream simultaneously to the HTTP socket *and* a cache file. The current implementation uses the magic-string filepath sentinel `"HTTP"` to encode the destination, with the cache-write happening outside the format handler. Under ADR-0006's typed `OutputSink` variant, a `TeeSink` alternative composes other sinks: the request handler hands the format handler one sink — `TeeSink{HttpSink, FilePath}` — and the format handler writes once, with each chunk broadcast to all sub-sinks. Same semantic; cleaner location of the tee (inside the format handler's write loop, not in the request handler orchestrating two passes). Generalises naturally — an `S3Sink` alternative joining the tee covers write-through to remote storage if that ever lands.

**4. Migration is staged into 8 small PRs**, each independently reversible. The first is a *non-code audit* of every internal use of the raw `pixels` pointer (catalog read / write / pointer-pass / arithmetic / `new[]`/`delete[]` cases) plus a benchmark of representative decode + scale + encode workloads. Output is a doc; gates the rest. This addresses the legitimate concern that pixel-buffer code may do "crazy stuff" depending on raw-pointer semantics — the audit produces explicit confidence (or surfaces an actual blocker) before any byte moves.

**5. Performance is preserved.** `std::vector<byte>::operator[]` and `byte_ptr[i]` compile to identical machine code in optimized builds; `vec.data()` returns a `byte*` indistinguishable from the raw pointer for any C-API use (libtiff, libpng, libjpeg-turbo, Kakadu). The existing pixel-touching algorithms (Floyd-Steinberg dither, in-place colour conversion, libtiff strip writes, etc.) run at identical speed. No measurable regression expected; verified by the audit PR's benchmark before any swap. Fallback: if benchmarking shows any regression, use `std::unique_ptr<byte[]>` instead — same RAII property, no `vector` metadata, ownership-transferable semantics.

## Considered Options

- **Keep `SipiImage` as a god-object** — rejected. Six responsibility groups is the textbook over-loaded class; every consumer pays for behaviours they don't use. The Rust port has nothing to map this onto.

- **Split into many tiny modules (one per behaviour)** — rejected. `image_processing/` as a single Bazel package with multiple sub-headers (`crop.h`, `scale.h`, etc.) is sufficient. Per-operation packages would multiply Bazel boilerplate without adding boundary value; nothing meaningful is enforced by separating `crop` from `scale`.

- **Subclass-based decomposition** (e.g., `Image` base, `ProcessableImage` derived) — rejected. Inheritance hierarchies don't survive the Rust port (Rust uses traits, not class hierarchies). Free-functions-over-value-type maps to Rust traits cleanly.

- **Keep `byte *pixels`; replace only ownership semantics** — rejected. Manual `new[]`/`delete[]` ownership requires explicit deep-copy ctor, move ctor, move-assignment, and dtor; ~100 lines of mechanical code that vanishes with `vector<byte>`. The maintenance cost is permanent.

- **Use `std::unique_ptr<byte[]>` instead of `std::vector<byte>`** — kept as fallback. `vector` is preferred for inherent size-tracking (consistency check vs. claimed dimensions); `unique_ptr<byte[]>` is the second-best if benchmarking shows any regression. The audit PR (step 1) decides empirically.

- **Defer the decomposition to the Rust rewrite itself** — rejected. The strangler-fig migration is incremental; each Rust-rewritten module needs to interoperate with still-C++ surrounding modules through stable shims. Decomposing now reduces the size of the eventual Rust translation unit and the shim surface.

## Consequences

- **`SipiImage` shrinks dramatically**. Public API ~15 methods (geometry, pixel access, metadata accessors, move/copy semantics) instead of ~50. Heavy header transitively-includes (shttps, format-handler types, 5 metadata standards) reduce to ~3 forward declarations + the value-type basics. Bazel `--strict_deps` becomes practical at the `image/` boundary.

- **Free-function-style image processing**. `image.crop(...)` becomes `Sipi::processing::crop(image, ...)`. ~12 method-to-free-function rewrites at every call site. Lua bindings (`SipiLua.cpp`) need updating; thin facade methods may be retained on `Image` purely for binding ergonomics if Probe 6 finds it cleaner.

- **No more friend classes**. The 5 `friend class` declarations (`SipiIcc` + 4 format handlers) all go away. Format handlers gain a public `pixels_writable()` API + metadata setters; `SipiIcc::iccFormatter` gains 1-2 new public Image accessors (whatever it needed via friend access today).

- **No more raw `byte *pixels`**. Replaced by `std::vector<byte>` (or `std::unique_ptr<byte[]>` if benchmarking demands). RAII eliminates the explicit deep-copy ctor / move ctor / move-assignment / dtor dance — the compiler's defaults work correctly with a vector member. `cpp-style-guide.md`'s "no raw owning new/delete" rule honoured.

- **No more HTTP coupling in `SipiImage`**. The `shttps::Connection*` field disappears; `connection()` accessor methods deleted. The HTTP-output sink is a parameter to format-handler `write()` per ADR-0006, not a property of `Image`. Resolves the SIPI ↔ shttps boundary leak this represents.

- **Static `io` registry leaves**. `Sipi::format_handlers::dispatch(extension) → SipiIO*` (or similar) lives in the format-handler module, where it conceptually belongs.

- **`app14_transform` field removed**. The JPEG handler inverts CMYK/YCCK at decode time so downstream code sees standard CMYK; no transient flag needed on the universal Image type.

- **TeeSink for dual-write preservation**. ADR-0006's `OutputSink` variant gains a `TeeSink { std::vector<OutputSink> outputs; }` alternative. Tracked in [DEV-6382](https://linear.app/dasch/issue/DEV-6382).

- **Migration is gated on [DEV-6341](https://linear.app/dasch/issue/DEV-6341) reaching Y+6** for the Bazel-package-dependent steps (move static `io` map, remove friends with public API, extract image-processing). The non-Bazel-dependent steps (raw-pointer audit, vector swap, `app14_transform` removal) can land in CMake era.

- **Approval-test surface unchanged**. Behaviour preservation is intended throughout the decomposition. Approval goldens stay valid.

- **Lua-binding surface ([SipiLua.cpp](../../src/SipiLua.cpp))** — Probe 6 resolves. Potentially the Lua API exposes `image:crop(...)` as method-style calls; if so, the Lua binding layer absorbs the C++-method-to-free-function translation transparently. Probe 6 outcome may add facade methods on `Image` purely for binding convenience.

- **Glossary deltas land in [`UBIQUITOUS_LANGUAGE.md`](../../UBIQUITOUS_LANGUAGE.md)** in the batched edit pass: add **Image processing** (umbrella for the free-function module). Sharpen **Image** (the code-level class becomes a narrow value type; domain term stays correct).

- **Tracked in Linear** under a new parent issue (sibling to [DEV-6374](https://linear.app/dasch/issue/DEV-6374)) with 8 children corresponding to the staged sub-PRs. Inter-issue dependencies modelled via Linear's `blockedBy` relations.
