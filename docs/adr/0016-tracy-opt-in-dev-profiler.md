---
status: accepted
---

# Tracy as an opt-in, dev-only profiler — always linked, inert unless `--config=tracy`

SIPI vendors the [Tracy](https://github.com/wolfpld/tracy) profiler client as an
**unconditional** Bazel dependency of the production graph, instrumented along the
IIIF hot path (parse / cache / decode / process / encode) with scoped zones behind
a single first-party shim, `src/observability/profiling.h`. The profiler is inert
in every build except `--config=tracy`: the entire Tracy machinery — listening
socket, sampling thread, allocation arena — is gated behind `TRACY_ENABLE`, which
only the `build:tracy` block in `.bazelrc` defines. A normal or production build
links a near-empty `TracyClient.cpp`, opens no socket, and collects nothing.

Tracy is local-dev only and never CI-gated, alongside `just bench` and
`just valgrind`. It answers "*where* in the code does the time go for this
workload" — the live, per-thread, per-function complement to the aggregate
Prometheus metrics and the isolated Google Benchmark microbenchmarks. See
`docs/src/development/profiling.md`.

## Considered options

- **Always-linked, globally gated by `--config=tracy` — accepted.** One
  `@tracy//:tracy` target, always in `//src:sipi_lib`'s graph; `--config=tracy`
  defines `TRACY_ENABLE` globally so it reaches both `TracyClient.cpp` and every
  instrumented first-party TU. The shim header `#include <tracy/Tracy.hpp>` is
  therefore always resolvable, so instrumented code compiles in *every* config
  (the macros are upstream no-ops without `TRACY_ENABLE`) with no `#ifdef` noise
  at the call sites. Cost: the inert client TU compiles in normal/CI/Docker
  builds, and the archive is fetched on every build. Both are negligible —
  `TracyClient.cpp` reduces to the thread-name helpers in `common/TracySystem.cpp`
  when disabled, and the fetch is one ~5 MB tarball behind the repository cache.

- **`--define`-gated split target (`tracy_headers` always; `tracy_client` via
  `select()`) — rejected (YAGNI).** Keeps the client TU out of non-Tracy builds
  entirely, at the cost of a `bool_flag`/`config_setting`/`select()` apparatus and
  a header-only-vs-client target split. The thing it saves — compiling one inert
  TU — is not worth the machinery. Revisit only if Tracy must be provably absent
  from production/CI artifacts.

- **BCR `bazel_dep` — rejected (not available).** Tracy is not in the Bazel
  Central Registry, so per ADR-0015 it is a native `cc_library` over an
  `http_archive` (`bazel/tracy.BUILD.bazel`).

- **Raw Tracy macros at every call site — rejected.** Instrumented code uses the
  `SIPI_ZONE*` macros from `src/observability/profiling.h`, the single place
  `<tracy/Tracy.hpp>` is included, so the codebase can be re-pointed at a different
  profiler (or none) from one file. The lone exception is `src/shttps/Server.cpp`
  worker-thread naming: shttps sits *below* observability in the dependency graph
  and cannot include the shim without inverting that one-way direction, so it uses
  the upstream header directly under an `#ifdef TRACY_ENABLE` guard.

## Consequences

- `MODULE.bazel`: `@tracy` `http_archive` pinned to v0.13.1.
  `bazel/tracy.BUILD.bazel`: native `cc_library` — `srcs = [TracyClient.cpp]`,
  the implementation `.cpp` files it textually `#include`s in `textual_hdrs`
  (compiling them as `srcs` would double-compile and collide), `copts = ["-w"]`
  to silence vendored-code warnings.
- `.bazelrc`: a `build:tracy` block (`-DTRACY_ENABLE -DTRACY_ON_DEMAND
  -fno-omit-frame-pointer -g --strip=never`). `TRACY_ON_DEMAND` keeps a
  long-running server from buffering when no GUI is attached.
- `justfile`: `bazel-build-tracy` (`-c opt --config=tracy`). `flake.nix`: a
  `profiling` dev shell carrying the Tracy GUI on Linux (`brew install tracy` on
  macOS — nixpkgs' GUI build is unreliable on aarch64-darwin).
- Dependency edges for the shim: `//src/observability` gains `@tracy//:tracy` and
  carries `profiling.h`; it propagates to `//src:sipi_lib`. `//src/shttps:shttps`
  gains `@tracy//:tracy` for thread naming. The fuzz subset and the shared
  `handlers/iiif_handler.cpp` are deliberately left untouched — the parse-tier
  zone wraps the `parse_iiif_uri` *call site* in `SipiHttpServer.cpp` (sipi_lib
  only), not the function body in the shared file.
- Zero production impact: deployed binaries are built without `--config=tracy`,
  so they carry no profiler. Verified by `bazel cquery` (the dep is present) plus
  the absence of a listening socket at runtime.
