# Code Review Guidelines

## Always check

### Security (input validation)
- IIIF identifiers validated for path traversal (`..`, `%2e%2e`, encoded variants) before any filesystem operation
- File paths resolved via `realpath()` and verified within `imgroot()` before `access()` or `open()`
- Null bytes (`\0`, `%00`) stripped or rejected from all URL components before reaching C-string operations
- HTTP response headers sanitized — no CR/LF/null from user input interpolated into headers (especially Content-Disposition)
- Error messages to clients do not leak internal file paths, server state, or stack traces
- Internal-only endpoints (`/metrics`) documented as requiring reverse proxy protection

### Memory safety (C++ ownership)
- No raw owning `new`/`delete` in new code — use `std::unique_ptr`, `std::make_unique`, or value semantics
- `shared_ptr` metadata members (`xmp`, `icc`, `iptc`, `exif`) null-checked before dereferencing
- Assignment operators `delete[]` old buffers before allocating new ones (no leaks on reassignment)
- Arithmetic operators return by value (not reference to heap-allocated object)
- Temporary buffers use RAII (`std::unique_ptr<byte[]>`) for exception safety
- Move constructors and move assignment operators marked `noexcept`

### C library boundary safety (FFI)
- **Type width:** C library APIs often expect specific integer widths (`uint16_t`, `uint32_t`). When passing C++ enum values or vectors to C functions, verify the underlying type matches what the C API expects. Example: libtiff's `TIFFSetField(TIFFTAG_EXTRASAMPLES, ...)` expects `uint16_t*`, not `uint8_t*` — passing a `vector<uint8_t>` causes a heap-buffer-overflow via `memcpy`
- **Ownership handoff:** When calling C++ functions that return raw `new[]` pointers (e.g., `Icc::iccBytes(len)`), the caller **must** `delete[]` after use. If a C library takes ownership, document it. Prefer wrapping in `std::unique_ptr<T[]>` immediately after receiving the pointer
- **RAII wrappers for C handles:** All C resource handles (`DIR*`, `FILE*`, `TIFF*`, `cmsHPROFILE`) must be wrapped in RAII — either `std::unique_ptr` with a custom deleter or a dedicated scope guard. Never rely on manual cleanup after a loop or before early returns
- **Variadic C functions:** `TIFFSetField`, `TIFFGetField`, and similar variadic APIs perform no type checking. Mismatched argument types compile silently and cause UB at runtime. Always cast arguments to the exact type the API documents

### Multi-buffer dimension safety
- When operating across two image buffers (e.g., watermark + target image), verify that loop bounds match the buffer being indexed — especially channel count (`nc`), width (`nx`), and height (`ny`). A loop iterating `k < image.nc` must not index into a watermark buffer with fewer channels
- The `POSITION(x, y, c, n)` macro encodes `n * (y * width + x) + c` — if `c >= n`, the access reads past the pixel boundary into adjacent data. Assert `c < n` at call sites or use `std::span` to make bounds explicit

### Resource exhaustion
- Large allocations (`new byte[bufsiz]`) wrapped in `try`/`catch` for `std::bad_alloc`
- Per-request pixel limit checked before expensive image processing
- Memory budget acquired before decode, released via RAII guard (`MemoryBudgetGuard`)
- Connection liveness verified before long operations (client may have disconnected)
- Rate limiter budget deducted before processing, not after

### Configuration consistency
- New config options added to all four surfaces: `SipiConf.h`/`.cpp` (Lua), `sipi.cpp` (CLI11), `SipiHttpServer.hpp` (accessor), and `config/sipi.config.lua` (documentation)
- Defaults identical across all entry points
- `docs/src/development/reviewer-guidelines.md` summarises this as "Lua / CLI / env" for review-checklist brevity; the four-surface list above is the authoritative one
- Invalid values produce clear startup errors with guidance on valid values

### Testing
- Security fixes include fuzz corpus entries and e2e tests
- Memory fixes verified under ASan (no leaks, no UB)
- New HTTP behavior tested in Rust e2e
- Tests verify behavior (dimensions, content, structure), not just HTTP status codes
- **Sanitizer gate:** PRs touching `src/`, `shttps/`, `include/`, or `test/` automatically run the sanitizer CI workflow (`sanitizer.yml`). Zero findings required to merge. The sanitizer build runs the full e2e suite under ASan/UBSan; first-party translation units are instrumented at compile time
- **Bazel test parity:** PRs that add a new unit test must add the matching `cc_test` target in `test/unit/<mod>/BUILD.bazel`. CI runs `just bazel-coverage`, which exercises every `cc_test` under `//test/unit/...` plus `//test/approval/...` and `//test/e2e-rust/...` in a single pass — a missing `cc_test` target = no CI coverage

### Performance
- Hot-path changes (codec decode/encode, `SipiImage::read`/`write`, IIIF parsing) include a before/after microbenchmark comparison in the PR. **No benchmark, no hot-path change** — add a `*_benchmark.cpp` first if none covers the path. See [docs/src/development/benchmarking.md](docs/src/development/benchmarking.md)
- Trust a delta only if it is green (U-test p < 0.05) AND the median shift exceeds the baseline CV; same machine, same `-c opt` binary for before and after. Sub-3% deltas are noise

### Metrics
- New metrics use correct Prometheus types (counter for monotonic, gauge for current state, histogram for distributions)
- Metric names follow `sipi_` prefix convention with `_total` suffix for counters
- Instrumentation in the correct layer (not duplicated across call chain)
- shttps-side instrumentation (request lifecycle, queue events) emits through the `shttps::ConnectionMetrics` Strategy interface (`shttps/transport/ConnectionMetrics.h`) — never call `Sipi::observability::Metrics::instance()` directly from `shttps/`. The SIPI → shttps direction is enforced by `package_group()` visibility in `BUILD.bazel`; `scripts/shttps-context-check.sh` is the advisory pre-commit grep variant of the same rule

### Thread safety
- Shared mutable state protected by `std::mutex` + `std::scoped_lock` or `std::atomic`
- `SipiMemoryBudget` uses `std::atomic<size_t>` — lock-free acquire/release from worker threads
- No `volatile` used for synchronization
- Thread-safety guarantees documented on classes accessed from multiple threads

## Style

- Prefer early returns over deeply nested conditionals
- Use `enum class` for new enumerations, `std::to_underlying()` for conversions
- Use structured bindings (`auto [key, value]`) instead of `.first`/`.second`
- Use `std::expected<T, E>` for fallible operations in new code (existing code uses `SipiError` exceptions)
- Apply `[[nodiscard]]` to functions where ignoring the return value is a bug
- Apply `const` everywhere it is valid
- Use `std::string_view` and `std::span` at function boundaries instead of `const std::string&` / `const std::vector<T>&`
- Follow the [C++23 style guide](docs/src/development/cpp-style-guide.md) for naming, formatting, and modernization patterns

## Skip

- Third-party source vendored via `http_archive` (the native `cc_library` deps; review their `bazel/<lib>.BUILD.bazel`, not the upstream source — updates flow from `MODULE.bazel`'s pins)
- Formatting-only changes (enforced by clang-format)
- Lua script changes in `scripts/` that only modify response text (not logic)
- Changes to `CHANGELOG.md`, `version.txt`, or `manifest.json` (managed by release-please)
