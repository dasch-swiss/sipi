---
status: proposed
---

# Format handler API modernization (Rust-aligned, transitional)

The format-handler API (`Sipi::SipiIO` base class + `SipiIOJ2k` / `SipiIOTiff` / `SipiIOJpeg` / `SipiIOPng` subclasses) is modernized as a coordinated set of changes that bring the C++ surface closer to the Rust idioms it will eventually be ported to under [ADR-0001](./0001-shttps-as-strangler-fig-target.md):

1. **`std::expected<T, E>` for fallible operations** — replacing `bool` returns + out-parameters and the tri-state `SipiImgInfo::success` enum.
2. **C++ default arguments instead of overload sets** for `read()` and `write()` — replacing the five `read()` overloads in `SipiIO.h`.
3. **A typed sum type for output sinks** — replacing the magic-string filepath sentinels (`"-"` for stdout, `"HTTP"` for the HTTP server output) with `std::variant<FilePath, StdoutSink, HttpSink>`.
4. **Per-codec typed parameter structs in a `std::variant`** — replacing the stringly-typed `SipiCompressionParams = std::unordered_map<int, std::string>` with `std::variant<JpegParams, J2kParams, TiffParams, PngParams>`.
5. **A SIPI-local `SIPI_TRY` macro** providing Rust-`?`-style early return for `std::expected` chains — addressing the loudest ergonomic gap with `std::expected` without adopting Abseil.

We accept these because the [Rust-aligned, transitional C++](../deep-modules.md#method-invariants-dont-drift-on-these-across-sessions) method invariant mandates that C++ improvements should preference patterns that translate cleanly to Rust over patterns that would harden the C++ for the long term. Each of the five changes maps onto an established Rust idiom:

| C++ pattern (current) | C++ pattern (target) | Rust equivalent (post-port) |
| --- | --- | --- |
| `bool read(...)` + out-params | `std::expected<void, IoError>` | `Result<(), IoError>` |
| `SipiImgInfo::success` enum | `std::expected<SipiImgInfo, ImgInfoError>` | `Result<ImgInfo, ImgInfoError>` |
| 5 `read()` overloads | C++ default args | Default args / Builder |
| Magic strings (`"-"`, `"HTTP"`) | `std::variant<FilePath, StdoutSink, HttpSink>` | Rust enum |
| `unordered_map<int, string>` params | `std::variant<JpegParams, J2kParams, ...>` | Rust enum |
| `if (e) ... else e.error() ...` | `SIPI_TRY(var, expr)` | `let var = expr?;` |

The `SIPI_TRY` macro is the only piece that doesn't survive the Rust port (Rust has the `?` operator natively). Everything else becomes a mechanical rename when the relevant module is reimplemented in Rust.

## SIPI_TRY macro definition

```cpp
// src/util/expected_macros.h
#define SIPI_TRY(var, expr)                                                  \
  auto&& _sipi_try_##var = (expr);                                            \
  if (!_sipi_try_##var)                                                       \
    return std::unexpected(std::move(_sipi_try_##var).error());               \
  auto&& var = *std::move(_sipi_try_##var)
```

Usage:

```cpp
std::expected<Thumbnail, RenderError> render_thumbnail(const std::string& path) {
  SIPI_TRY(handler, choose_handler(path));        // Result<Handler> ?
  SIPI_TRY(shape,   handler->read_shape(path));   // Result<Shape>   ?
  SIPI_TRY(decoded, handler->decode(path));       // Result<Image>   ?
  return scale(decoded, shape, kThumbSize);
}
```

Functionally equivalent to:

```rust
fn render_thumbnail(path: &Path) -> Result<Thumbnail, RenderError> {
    let handler = choose_handler(path)?;
    let shape = handler.read_shape(path)?;
    let decoded = handler.decode(path)?;
    Ok(scale(decoded, shape, THUMB_SIZE))
}
```

## Considered Options

- **Adopt Abseil's `absl::StatusOr<T>` instead of `std::expected<T, E>`** — rejected per the [Rust-aligned, transitional C++](../deep-modules.md#method-invariants-dont-drift-on-these-across-sessions) method invariant. Adopting Abseil would be a substantive C++-only commitment whose benefits (`StatusOr`, `flat_hash_map`, `StrCat`, etc.) all need re-evaluation for the Rust rewrite. `std::expected` maps directly onto Rust's `Result<T, E>`; `absl::StatusOr` does not (Rust has nothing like `absl::Status`).

- **Defer the modernization to the Rust rewrite itself** — rejected. The strangler-fig migration is incremental, not big-bang. Each Rust-rewritten module needs to interoperate with still-C++ surrounding modules through stable shims; the cleaner the C++ API at the seam, the cleaner the shim. Modernizing now reduces shim complexity later.

- **Status quo (keep current API)** — rejected. The five identified issues compound at every consumer (`SipiHttpServer`, `SipiImage`, `SipiLua`, every format handler), making the eventual refactor strictly more expensive.

- **Adopt `boost::leaf` or `boost::outcome`** — rejected. Neither maps cleanly to Rust's `Result<T, E>`; both add a Boost dependency for marginal C++-only ergonomic wins.

- **Custom `sipi::Result<T, E>` type** — rejected. Would duplicate `std::expected` for negligible gain; deviates from the stdlib path the broader C++ ecosystem is converging on.

## Consequences

- **Every fallible function in `format_handlers/`** is rewritten to return `std::expected<T, E>`. Callers update from `if (!handler->read(...)) { ... }` to `SIPI_TRY` or monadic `.and_then` chains.

- **`SipiImgInfo::success` enum is removed**. The "DIMS" partial-success case becomes a `bool partial` field on the success branch: `std::expected<SipiImgInfo, ImgInfoError>` where `SipiImgInfo` carries `partial = true` when only dimensions could be read (no full shape).

- **`SipiCompressionParams` becomes a `std::variant`** of per-codec parameter structs. The existing `SipiCompressionParamName` enum collapses into the variant alternatives. Each format handler's `write` accepts only its own variant alternative; mismatch is a compile-time error via `std::get` or `std::visit`.

- **Magic-string filepaths are replaced with `std::variant<FilePath, StdoutSink, HttpSink>`** at the `read()` / `write()` surface. Internal handling of stdout-vs-HTTP-vs-file is unchanged; only the API typing tightens. Resolves the ambiguity where `"HTTP"` was a special filename sentinel.

- **`SIPI_TRY` macro lives at `src/util/expected_macros.h`** (or wherever a small util module lands). Single header, no implementation.

- **Migration is staged**, not big-bang: one PR per format handler after `format_handlers/` is promoted to a Bazel package per [ADR-0003](./0003-module-co-located-source-and-tests.md). Tracked as child issues under the `format_handlers` parent issue in Linear.

- **No behaviour change is intended** by this ADR alone. All five changes are API-shape-only. Decode/encode internals stay unchanged. Approval-test surface is unaffected.

- **Rust port preview**: when a `format_handlers/` module is eventually rewritten in Rust under the strangler-fig plan, the API's shape requires no redesign — only a syntax translation. This is the load-bearing benefit of the modernization.

- **Coupled with [ADR-0004](./0004-image-shape-ownership.md)**: `read_shape` (the renamed `getDim`) is the first method to land the new return-type style — `std::expected<SipiImgInfo, ImgInfoError>`. The `read_shape` modernization rides ADR-0004's amendment commit; the rest of the format-handler API follows in subsequent PRs.
