---
dune_map: true
schema_version: 1
last_verified_commit: none   # not SHA-tracked: rebase-merge rewrites branch SHAs, so a pre-merge SHA is unknowable. Use `date` for freshness.
date: 2026-08-21
---

# ARCH-MAP.md — SIPI

Pull-on-demand architecture map. Not auto-loaded; open it for blast-radius and
boundary questions. Vocabulary lives in [`UBIQUITOUS_LANGUAGE.md`](UBIQUITOUS_LANGUAGE.md);
the bounded-context framing in [`CONTEXT.md`](CONTEXT.md); the module-scope table
this map lifts in [`CONVENTIONS.md`](CONVENTIONS.md).

## Overview

SIPI is a IIIF Image API 3.0 media server. Production is a **Rust axum shell**
(`server-rs` + `cli-rs`) that drives a **C++ image engine** (`libsipi`) across a
hand-mirrored `extern "C"` FFI seam (`src/ffi`); the former C++ HTTP server was
removed (ADR-0020), so the C++ `sipi` binary now provides only offline verbs
(convert/verify/query/compare/health). The engine is a set of Bazel `cc_library`
packages carved by concern — the image hub (`SipiImage` + cache + memory budget),
the codec handlers (`formats`), the IIIF parsers (`iiifparser`), metadata, and
the Lua/util/jwt support leaves extracted from the deleted shttps transport. The
top-level dependency arrow is one-way: **shell → seam → engine → leaves**; the
engine never links the shell or the seam. New work is added by dropping a file in
a package or a route in the axum router, not by editing a central switch — except
image formats, whose registry fan-out is documented, not mechanized.

## Components

### image

- **Paths:** `:(glob)src/SipiImage.{h,cpp}`, `:(glob)src/SipiCommon.{h,cpp}`, `:(glob)src/SipiFilenameHash.{h,cpp}`, `:(glob)src/SipiIO.h`, `:(glob)src/SipiImageError.h`, `:(glob)src/SipiError.{h,cpp}`, `:(glob)src/populate_from_image.{h,cpp}`, `:(glob)src/resample.{cc,h}`, `:(glob)src/SipiConf.cpp`, `:(glob)src/SipiReport.cpp`, `:(glob)src/process_benchmark.cpp`, `:(glob)src/BUILD.bazel`, `:(glob)src/nsswitch.conf`
- **Purpose:** The image engine hub — `SipiImage` orchestrates decode → process (scale/rotate/crop/ICC) → encode, and owns the metadata wrappers and format dispatch. Also holds the shared error base (`SipiError` = `//src:sipi_top`), the `//src` package's Bazel wiring, and the CLI's config object (`SipiConf`) and JSON reporter (`SipiReport`).
- **Key entities:** `Sipi::SipiImage`, `SipiImage::io` (static handler registry, *defined* in `formats`), `SipiImage::read`/`read_shape`/`write`/`add_watermark`/`convertToIcc`/`scale`/`rotate`/`crop`, `Sipi::SipiIO` (abstract), `SipiImgInfo`, `Sipi::read_watermark` (defined in `formats`), `SipiFilenameHash`, `Sipi::resample_separable_u8/u16`, `Sipi::estimate_peak_memory`, `Sipi::SipiError`/`SipiImageError`, `Sipi::SipiConf`, `Sipi::emit_json_report`
- **Public interface:** `SipiImage` (via `//src:engine`), `SipiIO`, `SipiError`; consumed by `formats`, `ffi`, `cli`, and the Lua bindings.
- **Local-context kit:** `src/SipiImage.h`, `src/SipiImage.cpp`, `src/SipiIO.h`, `src/BUILD.bazel` (the `:engine`/`:sipi_lib` targets), `src/formats/format_registry.cpp` (where `io` is defined), `docs/adr/0007-sipiimage-decomposition.md`, `CONVENTIONS.md`
- **Depends on:** metadata, iiifparser, formats (`:output_sink` only), util, logging, observability, cache, throttling
- **Used by:** formats (one-way back-edge, see rule), ffi, cli, cli-rs (transitively)
- **Boundary rules:**
  - The engine references but does **not** define `SipiImage::io` / `Sipi::read_watermark`; both are defined in `formats`, so `//src:engine` does not depend on `//src/formats:formats`. This inverts the `SipiImage`↔handler cycle. *Enforcement: `structure`* (a Bazel package cannot depend on itself; the cycle is unrepresentable).
  - The four codec handlers are `friend`s of `SipiImage` (`SipiImage.h`), a documented bidirectional coupling ADR-0007 plans to remove. *Enforcement: `docs-only`* (friendship is a language reach-in nothing flags).
- **Durable state:** `SipiImage::io` (static registry, single writer = `format_registry.cpp`); `SipiFilenameHash::__levels` (static, set via `setLevels`/`migrateToLevels`).

### cache

- **Paths:** `:(glob)src/SipiCache.{h,cpp}`
- **Purpose:** File-based LRU of generated representations, keyed by *Cache key*, with dual-limit eviction (total size **and** file count) and crash recovery. Compiled into `//src:engine`.
- **Key entities:** `Sipi::SipiCache`, `SipiCache::check`/`add`/`purge`/`deblock`, `FileCacheRecord` (on-disk fixed-width), `CacheRecord`
- **Public interface:** `SipiCache` (via `//src:engine`); the FFI runtime owns one instance.
- **Local-context kit:** `src/SipiCache.h`, `src/SipiCache.cpp`, `src/ffi/init.cpp` (constructs it), `src/ffi/serve_image.cpp` (check/add call sites), `UBIQUITOUS_LANGUAGE.md` (Cache / Cache key / Cache pin)
- **Depends on:** logging, observability
- **Used by:** ffi (`init.cpp` owns it, `serve_image.cpp` uses it)
- **Boundary rules:** Cache state is exposed **exclusively** through *Metrics*, never Lua bindings; cache-hit responses bypass both Throttling policies (ADR-0008). *Enforcement: `docs-only`* (glossary rule; no mechanical check).
- **Durable state:** in-memory `cachetable` + `blocked_files` + `cache_used_bytes`/`nfiles` (mutex-guarded); on-disk `.sipicache` index (rewritten on destruction; corruption → dir cleared). Single owner = the FFI runtime (`std::unique_ptr`, constructed in `init.cpp`).

### throttling

Colocated polyglot (ADR-0021/0022): a shell-side admission pool and an
engine-side memory budget under one component. Supersedes the former
`memory-budget` entry.

- **Paths:** `:(glob)src/throttling/rust/**` (admission, shell-side), `:(glob)src/throttling/cpp/**` (memory_budget, engine-side).
- **Purpose:** SIPI's load-driven request-rejection (Throttling). Two sub-policies: **Admission** (the two-partition thread pool — tile floor + full hard cap, pre-dispatch) and the **Decode memory budget** (the full partition's decode-RAM cap, post-cache, 503/413 under advanced mode).
- **Key entities:** Rust `admission::{Admission, AdmissionKind, AdmissionMode, AdmissionConfig, AdmissionSnapshot, Permit, default_pool_size}` (`//src/throttling/rust:admission`); C++ `Sipi::SipiMemoryBudget` + `MemoryBudgetGuard` + `enum class AdmissionMode { BASIC, ADVANCED }` + `Sipi::estimate_peak_memory` (`//src/throttling/cpp:memory_budget`).
- **Public interface:** `Admission` (owned by `server-rs` `AppState`; `classify`/`acquire`/`snapshot`); `SipiMemoryBudget` + `MemoryBudgetGuard` (via `//src:engine`).
- **Local-context kit:** `src/throttling/rust/lib.rs`, `src/throttling/cpp/SipiMemoryBudget.{h,cpp}`, `src/throttling/cpp/SipiPeakMemory.h`, `src/ffi/serve_image.cpp` (budget acquire site), `src/ffi/init.cpp` (resolves the config), `src/server-rs/src/routes.rs` (`AppState`, classify/acquire call sites), `UBIQUITOUS_LANGUAGE.md` (Throttling), `docs/adr/0022-two-lane-admission-control.md`.
- **Depends on:** admission (Rust) → `tokio` + `//src/iiifparser/rust:iiif_parser` only (FFI-free; no `//src/ffi`, no C++ engine — DUNE-002). memory_budget (C++) → nothing internal (decoupled from observability by design; the acquire site re-publishes gauges).
- **Used by:** `server-rs` (`AppState` owns the pool; `routes.rs` classify/acquire; `metrics.rs` reads the snapshot); ffi (`init.cpp` constructs the budget, `serve_image.cpp` acquires).
- **Boundary rules:**
  - The admission crate is engine-free: `bazel query 'deps(//src/throttling/rust:admission)'` shows no `//src/ffi`, no engine, no Kakadu. *Enforcement: `structure`* (Bazel dep graph).
  - The memory-budget module writes no metrics itself; the `serve_image.cpp` acquire site re-publishes gauges (DUNE-005). *Enforcement: `docs-only`*.
  - The shell Semaphore pool (pre-dispatch) and the engine memory budget (post-cache) are two distinct admission layers. *Enforcement: `docs-only`*.
  - `admission_mode`/`tiles_memory_ratio`/`large_decode_threshold_bytes`/`memory_limit` are resolved engine-side and read back over the seam (single authority, DUNE-003). *Enforcement: `docs-only`*.
- **Durable state:** admission — `Arc<Semaphore>` global + full sub-pool, per-partition `AtomicUsize`/`AtomicU64` wait/shed counters (single writer). memory_budget — `std::atomic<size_t> _used` (CAS-updated). No persistence.

### formats

- **Paths:** `:(glob)src/formats/**`
- **Purpose:** The four `SipiIO` codec handlers (TIFF, JPEG2000/Kakadu, JPEG, PNG), the codec-agnostic `output_sink`, and the `SipiImage::io` registry definition.
- **Key entities:** `SipiIOTiff`/`SipiIOJ2k`/`SipiIOJpeg`/`SipiIOPng`, `OutputSink`/`SinkStream`, `read_watermark`, `SipiImage::io` (defined in `format_registry.cpp`)
- **Public interface:** the `SipiIO` overrides (reached only through `SipiImage`'s dispatch); `output_sink` is a separate leaf target `//src/formats:output_sink`.
- **Local-context kit:** `src/formats/BUILD.bazel`, `src/formats/format_registry.cpp`, `src/formats/SipiIOTiff.{h,cpp}`, `src/formats/output_sink.h`, `src/SipiImage.h` (friend decls + `io` decl), `tools/formats-fanout.sh` (the new-format edit-site list)
- **Depends on:** image (`//src:engine`, one-way), metadata, observability, logging, util, output_sink; codecs `@kakadu` `@tiff` `@libpng` `@libjpeg_turbo`
- **Used by:** image (link-time, for `io`/`read_watermark`), ffi, cli, tests
- **Boundary rules:**
  - `//src/formats:formats` depends one-way on `//src:engine`; `output_sink` is a dependency-free leaf so the engine can reach it without depending on the handler package. *Enforcement: `structure`* (Bazel target-granularity dep direction).
  - Byte-exact cross-arch output invariant — the encode paths are pinned byte-for-byte by `//test/approval:approvaltests` across all platforms; math feeding the encoder must be architecture-independent (fixed-point over float). *Enforcement: `static-analysis`* (CI approval gate; file-head banners in SipiIOTiff/J2k/Png.cpp).
  - Adding a format touches a shared registry + dispatch + friend fan-out (~5 sites across 3 files, listed by `tools/formats-fanout.sh`). *Enforcement: `docs-only`* (DUNE-005 deferred a descriptor table until a real 5th format; ADR-0006 is the prior art to check first).
- **Durable state:** `SipiImage::io` (single writer here); no runtime state.

### iiifparser

- **Paths:** `:(glob)src/iiifparser/**`
- **Purpose:** The IIIF URL parser, colocated polyglot (component-first, then language; ADR-0021). `cpp/value_objects/` is the live engine value objects (region/size/rotation/quality/format/identifier + `compute_decode_dims`); `cpp/classifier/` is the `testonly` `parse_iiif_uri` reference oracle; `rust/` is the production Rust parser (`//src/iiifparser/rust:iiif_parser`) the shell drives, emitting domain types. `corpus/` is the language-neutral regression corpus both languages sweep.
- **Key entities:** C++: `SipiRegion`/`SipiSize`/`SipiRotation`/`SipiQualityFormat`/`SipiIdentifier` (each with a string-parse ctor and a flattened-FFI-seam ctor + `canonical()`), `SipiDecodeDims`/`compute_decode_dims`, `handlers::iiif_handler::parse_iiif_uri` (testonly reference). Rust: `parse_request`, `ParsedRequest`/`RequestKind`, `IiifParams`, `RegionKind`/`SizeKind`/`QualityKind`/`FormatKind` (domain enums, total supersets of the FFI enums)
- **Public interface:** the value-object classes (via `//src/iiifparser/cpp/value_objects:iiifparser`); `parse_iiif_uri` via the testonly `//src/iiifparser/cpp/classifier:iiif_handler`; the Rust parser via `//src/iiifparser/rust:iiif_parser` (`parse_request` → domain `IiifParams`, `server-rs` owns the `From` flattening).
- **Local-context kit:** `src/iiifparser/cpp/value_objects/BUILD.bazel`, `src/iiifparser/cpp/value_objects/SipiSize.h`, `src/iiifparser/cpp/classifier/iiif_handler.h`, `src/iiifparser/rust/BUILD.bazel`, `src/iiifparser/rust/parse.rs`, `src/ffi/serve_image.cpp` (the C++ FFI-seam reconstruction), `src/server-rs/src/ffi.rs` (the domain → seam `From<IiifParams>` mapping a domain-enum change must be kept exhaustive against). ADR-0021 is one hop away via every subpackage's BUILD docstring.
- **Depends on:** image (`//src:sipi_top` for `SipiError`), util. The Rust crate deps only `@crates//:percent-encoding` — no FFI, no C++ engine.
- **Used by:** image (`SipiIO.h`), ffi (`serve_image.cpp` rebuilds the value objects), formats (`SipiIOJ2k`), cli; server-rs (drives the Rust parser)
- **Boundary rules:**
  - `cpp/value_objects` is a leaf — deps only `//src:sipi_top` + `//src/util`, never `SipiImage`/codecs. *Enforcement: `structure`* (Bazel visibility + dep set). Each subpackage pins the virtual `iiifparser/` include prefix to its own physical depth (`strip_include_prefix` + `include_prefix = "iiifparser"`) so consumers keep `#include "iiifparser/*.h"`. *Enforcement: `structure`* (a wrong prefix fails the engine compile).
  - `cpp/classifier` `iiif_handler` is the `testonly` reference oracle for the Rust `parse_request`, deps `//src/util` only, and is `rm -rf`-deletable as a whole folder once the Rust port is trusted (only the `//test/approval` edge + corpus consumer need unwiring). *Enforcement: `structure`* (`testonly` keeps it out of `//src:sipi_lib`; DUNE-015).
  - `rust/iiif_parser` is FFI-free: `bazel query 'deps(...)'` shows no `//src/ffi:sipi_ffi` and no C++ engine, so it needs no `_CPP_STDLIB_LINK` and is sanitizer-eligible (untagged). *Enforcement: `structure`* (Bazel dep set). The domain→FFI `From` impls in `server-rs/ffi.rs` are exhaustive matches, never `as` casts. *Enforcement: `static-analysis`* (a new variant fails to compile; per-variant mapping test).
- **Durable state:** `SipiSize::limitdim` (static compile-time constant, no writer).

### metadata

- **Paths:** `:(glob)src/metadata/**`
- **Purpose:** EXIF / IPTC / XMP / ICC wrappers over exiv2 + lcms2, and the Essentials preservation packet. Sits **below** the image engine (no `SipiImage` dep). The canonical model package (docstring README, Test-seam visibility, colocated tests).
- **Key entities:** `Exif`/`Iptc`/`Xmp`/`Icc`/`Essentials`, `Icc::iccBytes()` (the single ICC-materialization chokepoint, ADR-0002), `EssentialsFields`, `PhotometricInterpretation`
- **Public interface:** the wrapper classes (via `//src/metadata:metadata`); the byte-mutation helper is in `//src/metadata/internal` (restricted).
- **Local-context kit:** `src/metadata/BUILD.bazel`, `src/metadata/icc.h`, `src/metadata/icc.cpp`, `src/metadata/internal/BUILD.bazel`, `src/metadata/internal/icc_normalization.{h,cpp}`, `src/metadata/essentials.h`, `docs/adr/0002-icc-profile-determinism-test-only.md`
- **Depends on:** image (`//src:sipi_top` for `SipiError`), util; exiv2, lcms2, openssl, protobuf (internal codec)
- **Used by:** image (owns the wrappers as members), formats (read/write ICC + Essentials), ffi, cli
- **Boundary rules:**
  - `//src/metadata/internal` (`icc_normalization`, `protobuf_codec`) is visibility-restricted to `//src/metadata:__pkg__` — the canonical **Test seam** pattern (DEV-6406). *Enforcement: `structure`* (Bazel visibility fails analysis on an external include).
  - `Icc::iccBytes()` is the single chokepoint every codec-bound ICC profile funnels through; new format handlers must route through it (bypassing it breaks the approval gate). *Enforcement: `static-analysis`* (approval determinism gate) + `docs-only` (the banner).
  - No `SipiImage` dependency — the one former coupling was inverted via `photometric_interpretation.h`. *Enforcement: `structure`* (dep set).
- **Durable state:** none in-process; the durable artifact is the Essentials packet embedded in image headers (written by format encoders via `Essentials::serialize`).

### scripting

- **Paths:** `:(glob)src/scripting/**`
- **Purpose:** The Lua runtime, colocated polyglot (component-first, then language; ADR-0021). `rust/` is the Rust mlua runtime (ADR-0023): hardened per-request VM profile (stdlib whitelist, `os` shim, restricted `require`, memory cap, deadline hook + binding chokepoint) and the bytecode cache. The C++ side (`LuaServer` + the `RequestContext`/`ResponseSink` seam + the `server.db` sqlite bindings) still serves the live preflight/route/config entry points and is deleted at the ADR-0023 cutover.
- **Key entities:** Rust: `ScriptRuntime`/`RequestVm` (VM factory + chokepoint), `BytecodeCache`, `LimitConfig`/`Deadline`/`KillStats`, `config_vm`. C++: `shttps::LuaServer`, `RequestContext`, `ResponseSink` (abstract), `HttpMethod`, `UploadedFile`, `shttps::sqliteGlobals`, `LuaSetGlobalsFunc`
- **Public interface:** Rust runtime via `//src/scripting/rust:scripting` (crate `scripting`); C++ `LuaServer` + `request_context.h` via `//src/scripting:scripting`; sqlite bindings via `//src/scripting:lua_sqlite`.
- **Local-context kit:** `src/scripting/rust/BUILD.bazel`, `src/scripting/rust/runtime.rs`, `src/scripting/rust/limits.rs`, `src/scripting/BUILD.bazel`, `src/scripting/LuaServer.h`, `src/scripting/request_context.h`, `src/ffi/lua_config.cpp` (builds a per-request `LuaServer`), `docs/adr/0023-rust-hosted-mlua-lua-runtime.md` — over the ≤7-file budget while both implementations coexist; the C++ half of the kit is deleted with the cutover.
- **Depends on:** Rust: mlua (`external` link mode), lua (direct `@lua` dep), libc, tracing. C++: util, jwt, logging; lua, jansson, curl, sole (base62 UUID for DSP IRIs), sqlite3 (lua_sqlite)
- **Used by:** ffi (preflight, run_lua_route, init, SipiLua); server-rs (the Rust runtime)
- **Boundary rules:**
  - C++ side driven only through the `RequestContext`/`ResponseSink` DI seam — no HTTP transport dep. `lua_sqlite` visibility narrowed to `//src/cli` + `//src/ffi`. *Enforcement: `structure`* (Bazel visibility).
  - Rust side: every script-visible binding registers through the `RequestVm::register_binding` chokepoint (deadline check first); `verify_bindings_checked` enumerates binding tables against the registration record. *Enforcement: `static-analysis`* (the enumeration test).
- **Durable state:** none intrinsic (a runtime/VM; fresh VM per request). The bytecode cache is in-memory, keyed by path, invalidated by mtime+size. `lua_sqlite`'s `server.db` opens caller-controlled sqlite files at script direction.

### util

- **Paths:** `:(glob)src/util/**`
- **Purpose:** Generic SIPI-domain helpers (MIME/string parsing, file hashing, error/global types, URL decode) extracted from shttps; a leaf. Namespace stays `shttps::`.
- **Key entities:** `shttps::Error`, `shttps::Hash` + `enum HashType` (on-disk contract, ADR-0005), `shttps::Parsing::{getFileMimetype,getBestFileMimetype,parseMimetype}`, `shttps::urldecode`, `Global::as_integer`
- **Public interface:** the free functions + value types (via `//src/util`, `strip_include_prefix="/src"` → `#include "util/…"`).
- **Local-context kit:** `src/util/BUILD.bazel`, `src/util/Parsing.h`, `src/util/Hash.h`, `src/util/Error.h`, `src/util/UrlDecode.h`
- **Depends on:** openssl (Hash), libmagic (Parsing MIME sniff) — no SIPI-internal deps.
- **Used by:** image, formats, iiifparser, metadata, scripting, cli, ffi (broadly used leaf)
- **Boundary rules:** a leaf — no internal deps; colocated `util_test` links only `:util`, so a forbidden cross-module include is a build error, not a `sipi_lib`-wide slip. *Enforcement: `structure`*.
- **Durable state:** `Hash::HashType` values are an on-disk contract (mirrored in `essentials.proto`); `Parsing` ships a compiled-in `magic.mgc` blob (read-only).

### jwt

- **Paths:** `:(glob)src/jwt/**`
- **Purpose:** A minimal JWT (JWS) sign/verify C leaf over OpenSSL + jansson, consumed only by the Lua `server.jwt` bindings.
- **Key entities:** `jwt_new`/`jwt_decode`/`jwt_encode_str`/`jwt_set_alg`/`jwt_add_grants_json`, `enum jwt_alg_t` (only HS256 exercised)
- **Public interface:** `jwt.h` (`extern "C"`), via `//src/jwt`.
- **Local-context kit:** `src/jwt/BUILD.bazel`, `src/jwt/jwt.h`, `src/scripting/LuaServer.cpp` (the sole caller, `server.jwt` bindings)
- **Depends on:** openssl, jansson (no internal deps)
- **Used by:** scripting (only)
- **Boundary rules:** visibility restricted to `//src/scripting:__pkg__`. *Enforcement: `structure`* (Bazel visibility). No colocated unit test — coverage is e2e only (`test/e2e/src/jwt.rs`). *Enforcement: `docs-only`* (a noted gap).
- **Durable state:** none; the `jwt_secret` flows through `RequestContext`, caller-supplied per request.

### observability

- **Paths:** `:(glob)src/observability/**`
- **Purpose:** In-process metrics (plain atomic counters/gauges — prometheus-cpp removed in Phase 7) and a Tracy profiling shim. Metrics reach production OTLP **only** via the scalar `SipiMetricsSnapshot` FFI struct.
- **Key entities:** `Sipi::observability::Metrics` (Meyers singleton), `Counter`/`Gauge`, `read_shape_fast_path_counter`, `essentials_hash_mismatch_counter`, `SIPI_ZONE()` macros
- **Public interface:** `Metrics::instance()` (via `//src/observability:observability`).
- **Local-context kit:** `src/observability/metrics.h`, `src/observability/metrics.cpp`, `src/observability/metrics_registry_test.cpp` (the seam tripwire), `src/ffi/metrics_snapshot.h` (the Inclusion rule), `src/server-rs/src/metrics.rs` (the OTLP bridge)
- **Depends on:** tracy (inert unless `--config=tracy`)
- **Used by:** image, cache (writers), ffi (reader/bridge), formats
- **Boundary rules:** the singleton is engine-internal; a scalar counter reaches production only if it is also read into `SipiMetricsSnapshot` and mapped in `metrics.rs`. `metrics_registry_test` pins the full field inventory (22 bridged / 13 engine-internal). *Enforcement: `static-analysis`* (the seam-tripwire test + the `SipiMetricsSnapshot` 176-byte layout lock) + `docs-only` (the banner).
- **Durable state:** the `Metrics` singleton atomics (process-lifetime); writers across image/cache/formats/ffi, reader = `sipi_metrics_snapshot`.

### logging

- **Paths:** `:(glob)src/logging/**`
- **Purpose:** A generic, non-`Sipi::` logging primitive (free functions + set-once flags + per-request thread-local trace context) any module may depend on. Pure stdlib.
- **Key entities:** `log_debug`/`log_info`/`log_warn`/`log_err`, `enum LogLevel`, `set_log_trace_context`/`set_outbound_traceparent`, `set_json_mode`
- **Public interface:** `logger.h` (via `//src/logging:logging`, `include_prefix="logging"`).
- **Local-context kit:** `src/logging/BUILD.bazel`, `src/logging/logger.h`, `src/logging/logger.cpp`
- **Depends on:** (stdlib only)
- **Used by:** nearly every C++ component
- **Boundary rules:** the one module whose per-target `layering_check` passes today (no vendored includes). *Enforcement: `static-analysis`* (layering_check enabled on this target only; deferred elsewhere — DEV-6353).
- **Durable state:** module-level statics (`g_log_level`, `g_json_mode`) + thread-locals (`g_trace_id`/`g_span_id`/`g_outbound_traceparent`); writers are the `set_*` functions.

### ffi

- **Paths:** `:(glob)src/ffi/**`
- **Purpose:** The hand-mirrored `extern "C"` seam the Rust shell drives the C++ engine through — serve/preflight/lua-route entries, the engine-context install, the metrics snapshot, edge probes, and the shared `LibraryInitialiser`. Also hosts `SipiLua` (the `sipi.*` Lua bindings) and `sipi_init`.
- **Key entities:** `sipi_serve_image`/`sipi_serve_file`/`sipi_init`/`sipi_preflight`/`sipi_run_lua_route`/`sipi_metrics_snapshot`/`sipi_cli_main` (defined in `cli`), `SipiResponse` (streamed sink), `SipiIiifParams`/`SipiServeRequest`/`SipiMetricsSnapshot` (`#[repr(C)]` mirrors), `EngineContext` + `set_engine_context`, `LibraryInitialiser`, `FfiResponseSink`
- **Public interface:** `sipi_ffi.h` (`strip_include_prefix="/src"` → `#include "ffi/sipi_ffi.h"`), mirrored by hand in `src/server-rs/src/ffi.rs`.
- **Local-context kit:** `src/ffi/sipi_ffi.h`, `src/ffi/serve_image.cpp`, `src/ffi/engine_context.{h,cpp}`, `src/ffi/init.cpp`, `src/ffi/metrics_snapshot.h`, `src/ffi/BUILD.bazel`, `src/server-rs/src/ffi.rs` (the Rust mirror)
- **Depends on:** image, formats, iiifparser (transitive), metadata, scripting (+lua_sqlite), util, observability, logging; lua, curl, exiv2
- **Used by:** server-rs (drives it), cli-rs (links it), cli (shares `startup`/`LibraryInitialiser`)
- **Boundary rules:**
  - `//src:engine` does **not** depend on this package (the seam drives the engine, never the reverse) — no cycle. *Enforcement: `structure`* (Bazel dep direction).
  - Every `#[repr(C)]` struct/enum crossing the seam is guarded on both sides: C++ `static_assert(sizeof/offsetof)` + Rust `offset_of!`/`size_of` layout tests (DUNE-002). *Enforcement: `structure`* on the C++ side (a drifted struct fails to compile) + `static-analysis` on the Rust side (test-time layout asserts run on the `//src/...` wildcard).
  - No C++ exception may cross the boundary — every `sipi_*` wraps in a catch-all (`sipi_guard`). *Enforcement: `review`*.
- **Durable state:** `EngineContext` (file-static `g_engine`, single sink `set_engine_context`, sole installer `sipi_init`); the Lua config VM (`set_lua_config`, installed once by `sipi_init`).

### cli

- **Paths:** `:(glob)src/cli/**`
- **Purpose:** The C++ offline verbs (convert/verify/query/compare/health) behind the `sipi_cli_main` FFI entry, and the thin `main` in `sipi.cpp`. Server mode was deleted with the oracle (Phase 7).
- **Key entities:** `sipi_cli_main` (extern "C"), `Sipi::cli::cmd_convert_access_file`/`cmd_convert_service_file`/`cmd_verify`/`cmd_health`, `LibraryInitialiser::instance()`
- **Public interface:** `sipi_cli_main` (the sole export `cli-rs` links); `//src/cli:sipi` binary.
- **Local-context kit:** `src/cli/cli_app.cpp`, `src/cli/sipi.cpp`, `src/cli/BUILD.bazel`, `src/cli/commands/BUILD.bazel`, `src/cli/commands/convert_service_file.h`, `docs/adr/0009-file-taxonomy.md`
- **Depends on:** image (`sipi_lib`), ffi (`sipi_cli_main` contract, shared `startup`), cli11
- **Used by:** cli-rs (dispatches offline verbs via `sipi_cli_main`)
- **Boundary rules:** one `.cpp`/`.h` per `sipi <verb> <noun>` in `commands/`; CLI11 stays in `cli_app.cpp` and never leaks into `commands/` (which take plain `*Args` structs). `//src/cli/commands` visibility scoped to `//src/cli:__pkg__`. *Enforcement: `structure`* (visibility) + `docs-only` (the one-file-per-verb convention).
- **Durable state:** `LibraryInitialiser` singleton (process-global init, idempotent).

### server-rs

- **Paths:** `:(glob)src/server-rs/**`
- **Purpose:** The production Rust axum HTTP shell — routing, IIIF/info assembly, the streaming response sink, edge path validation, config (Lua or TOML), the Throttling pool, the preflight cache, and OTel telemetry. Shipped as the `sipi` library so a downstream crate can embed it.
- **Key entities:** `run`/`serve`/`app`, `routes::iiif`/`cors_preflight`/`serve_docroot`, `AppState`, `IMAGE_MIMES`, `iiif_parser::parse_request` (the carved parser crate), the `ffi.rs` `From<iiif_parser::IiifParams> for SipiIiifParams` seam mapping, `info::{image_info_json,bitstream_info_json}`, `preflight_cache::PreflightCache`, `ServerOverrides`, the `ffi.rs` `#[repr(C)]` mirrors + layout-lock tests
- **Public interface:** crate `sipi` (`//src/server-rs:lib`) — `pub fn run` and `pub fn app`; `ServerOverrides`.
- **Local-context kit:** `src/server-rs/src/lib.rs`, `src/server-rs/src/routes.rs`, `src/server-rs/src/ffi.rs`, `src/server-rs/src/config.rs`, `src/server-rs/BUILD.bazel`, `src/ffi/sipi_ffi.h` (the C++ side of the seam)
- **Depends on:** ffi (`//src/ffi:sipi_ffi`, the first Rust→C++ link; carries the whole engine); iiifparser (`//src/iiifparser/rust:iiif_parser`, the domain-typed URL parser); axum/tokio/opentelemetry/sentry (via the single `@crates` hub)
- **Used by:** cli-rs (calls `sipi::run`)
- **Boundary rules:**
  - Production Rust comments describe current behaviour on their own terms — the oracle vocabulary (`oracle|shttps|cutover|parity|strangler|C++ server`) is avoided in `src/server-rs/src` + `src/cli-rs/src` `.rs` files. *Enforcement: `docs-only`* (the `CONVENTIONS.md` § Production surface rule; DUNE-012).
  - The listen-port precedence chain has a single authority in `lib.rs::serve()` (`SIPI_RS_PORT` > `--serverport`/`SIPI_SERVERPORT` > Lua `sipi.port` > `DEFAULT_PORT=1024`). *Enforcement: `docs-only`* (one code site; doc copies are pointers).
  - `IMAGE_MIMES` must list the same image mimes as the C++ `detect_in_format`. *Enforcement: `docs-only`* (cross-reference comment; no mechanical check).
- **Durable state:** `AppState` (built once per `serve()`), which owns the two-lane `Admission` pool (`Arc<admission::Admission>`, holding the semaphores + per-partition counters — see the `throttling` component); the opt-in Preflight cache.

### cli-rs

- **Paths:** `:(glob)src/cli-rs/**`
- **Purpose:** The Rust binary entry point (`//src/cli-rs:sipi`) — owns `main`, the clap `server` verb, Sentry init + the out-of-process minidump reporter, and the mimalloc allocator. Dispatches offline verbs to the C++ `sipi_cli_main`.
- **Key entities:** `main`, `commands::server::run`, `ServerArgs` (clap flatten groups), `impl From<&ServerArgs> for ServerOverrides` (exhaustive destructure, DUNE-006), `mod allocator` (mimalloc `extern "C"` block), `init_sentry`
- **Public interface:** the `sipi` binary; clap `ServerArgs`.
- **Local-context kit:** `src/cli-rs/src/main.rs`, `src/cli-rs/src/commands/server/mod.rs`, `src/cli-rs/src/commands/server/args/mod.rs`, `src/cli-rs/BUILD.bazel`, `docs/adr/0019-mimalloc-production-allocator.md`, `docs/adr/0018-minidump-crash-memory-accepted-risk.md`
- **Depends on:** server-rs (`sipi::run`), cli (`sipi_cli_main`), mimalloc (vendored static, Linux-non-ASan), sentry(+minidump)
- **Used by:** (top of the binary graph)
- **Boundary rules:**
  - The config seam fails on omission: `From<&ServerArgs>` destructures every clap group exhaustively (no `..`), so a new `server` flag fails to compile until forwarded or explicitly `field: _`. *Enforcement: `structure`* (exhaustive-match compile error; DUNE-006).
  - The mimalloc stats `extern "C"` block is deliberately colocated in `main.rs` (drops out with the feature; a version drift is a SIGSEGV, not a build error — read via the `mi_stats_shim.c` C shim). *Enforcement: `docs-only`* (SAFETY comment; decision 4).
  - Same oracle-vocabulary avoidance as server-rs. *Enforcement: `docs-only`* (the `CONVENTIONS.md` § Production surface rule).
- **Durable state:** none persistent; owns the process lifecycle, the Sentry client guard, and the minidump reporter guard.

## Cross-cutting concerns

Files and rules that span components rather than living in one:

- **The FFI seam** (`src/ffi/sipi_ffi.h` ↔ `src/server-rs/src/ffi.rs`) is the single contract between the shell and the engine; its layout is locked on both sides (see the `ffi` and `server-rs` entries). A change to any `#[repr(C)]` struct is a two-file edit by construction.
- **The metrics bridge** flows `observability::Metrics` (engine) → `SipiMetricsSnapshot` (`src/ffi/metrics_snapshot.h`) → `server-rs/src/metrics.rs` → OTLP. The `metrics_registry_test` seam tripwire is the mechanical guard that a new counter is a conscious bridge-or-not decision.
- **The Throttling gate** (`src/ffi/serve_image.cpp`) is the one post-cache point where the engine-side memory budget fires (ADR-0008); the shell's two-lane `Admission` pool is a separate, earlier (pre-dispatch) admission layer (`server-rs/routes.rs`, `//src/throttling/rust:admission`). See the `throttling` component and ADR-0022.
- **Ubiquitous language** — identifiers/comments follow `UBIQUITOUS_LANGUAGE.md`; the reviewer checklist (`docs/src/development/reviewer-guidelines.md` § Ubiquitous Language) and the `CONVENTIONS.md` § Production surface rule are the guards (convention-only — no mechanical gate).

## Support areas (completeness coverage)

These carry no component boundary rules but exist so every tracked file maps somewhere:

- **Tests** — `:(glob)test/**` (unit under `test/unit/**`, snapshot regression under `test/approval/**`, Rust reqwest e2e under `test/e2e/**`, fixtures under `test/_test_data/**`). Unit tests link the narrow per-module target where one exists (metadata, util, iiifparser, formats, output_sink, jwt-via-scripting, decode_dims, handlers), else `//src:sipi_lib` (sipiimage, cache, memory_budget, logger, configuration, tiff_codecs). Colocated C++ tests live beside their module (ADR-0003).
- **Build & tooling** — `:(glob)MODULE.bazel`, `:(glob)MODULE.bazel.lock`, `:(glob).bazelrc` (absent = tracked via workflow), `:(glob)justfile`, `:(glob)bazel/**`, `:(glob)tools/**`, `:(glob)platforms/**`, `:(glob).github/**`, `:(glob)flake.nix`, `:(glob)flake.lock`, `:(glob)rustfmt.toml`, `:(glob)codecov.yml`, `:(glob)version.txt`. CI gates: `just bazel-rustfmt-check`, `just bazel-clippy-check`, `just commit-lint`, the approval + e2e + unit suites.
- **Docs & agent-context** — `:(glob)docs/**`, `:(glob)*.md` (CLAUDE.md, CONTEXT.md, UBIQUITOUS_LANGUAGE.md, CONVENTIONS.md, REVIEW.md, RELEASING.md, README.md, DEPRECATIONS.md, ARCH-MAP.md), ADRs under `docs/adr/**`.
- **Runtime assets** — `:(glob)include/**` (generated headers, ICC profiles, favicon), `:(glob)server/**`, `:(glob)config/**`, `:(glob)scripts/**`, `:(glob)certificate/**`, `:(glob)db/**`, `:(glob)openseadragon.min.js.map`, `:(glob)test_tifs.sh`, `:(glob).claude/**`.

## Conventions

- **Module granularity** — one Bazel `cc_library` per concern under `src/<mod>/`, source + header + `*_test.cpp` colocated (ADR-0003). Local-context-kit budget **≤7 files**.
- **Top-level dependency direction (one-way):** `cli-rs → server-rs → //src/ffi:sipi_ffi → //src:engine → {metadata, iiifparser, formats:output_sink, util, logging, observability}`; `formats`/`scripting`/`jwt` sit beside/below the engine. **The engine never links the shell or the seam.** *Enforcement: `structure`* (Bazel dep graph; a back-edge fails analysis).
- **New work is added by dropping a file / adding a route, not editing a central switch** — a new offline verb is one file in `src/cli/commands/`; a new axum route is a registration in `server-rs/lib.rs::app()`; a new engine module is a new `cc_library` package. *Enforcement: `docs-only`.*
  - **Exception (banned-construct):** new image format → editing the `SipiImage::io` registry + `read`/`read_shape` switch + `friend` fan-out (~5 shared sites, `tools/formats-fanout.sh`) → *why it couples:* the dispatch is centralized, so a 5th format is a multi-file shared edit → *alternative:* a descriptor-registration table (deferred until a real 5th format; ADR-0006) → *enforcement:* `docs-only`.
- **Test seam** — a helper that must be unit-tested but not publicly callable goes in an `internal/` subpackage with visibility restricted to its parent (`//src/metadata/internal` is the model). *Enforcement: `structure`.*
- **Colocated docs** — every `cc_library`/`rust_library` package carries a `BUILD.bazel` docstring; invariant banners sit at the file head next to the code they govern (`src/metadata/icc.h`, the format encoders, `src/observability/metrics.h`). *Enforcement: `docs-only`.*
- **`layering_check`** is deferred repo-wide (vendored native deps emit no module maps — DEV-6353) except `//src/logging`, where it passes today. *Enforcement: `static-analysis`* (where enabled).
- **Banned constructs:**
  - oracle-era framing (`oracle`/`shttps`/`cutover`/`parity`/`strangler`/`C++ server`) in production Rust comments → couples the code to a removed transport → describe current behaviour on its own terms → *enforcement:* `docs-only` (the `CONVENTIONS.md` § Production surface rule).
  - a `server` clap flag that is silently dropped → a config option that never reaches the engine → destructure exhaustively in `From<&ServerArgs>` (bind `field: _` to drop deliberately) → *enforcement:* `structure`.
  - "file" as a domain noun for the served byte stream, "backpressure" for the load-shed policies, "canonical URL" for the cache key → drift from the ubiquitous language → use Bitstream / Throttling / Cache key (`UBIQUITOUS_LANGUAGE.md`) → *enforcement:* `docs-only` (reviewer checklist) + `static-analysis` for the oracle subset.
