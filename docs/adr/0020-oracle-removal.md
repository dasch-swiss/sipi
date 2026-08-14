---
status: accepted
amends: 0013-shttps-as-internal-module.md
---

# The shttps oracle is removed; the Rust shell stands alone

> **Amends [ADR-0013](0013-shttps-as-internal-module.md) (DEV-6968 / DEV-6969,
> 2026-08-14).** ADR-0013 kept `src/shttps/` in-tree as the frozen
> differential-parity oracle, "removed after deploy." That deploy has held in
> production for weeks. This ADR records the removal: the oracle transport,
> `SipiHttpServer`, the C++ `server` mode, and the differential parity gate are
> deleted. ADR-0013's decomposition (the cutover survivors moved to
> `src/scripting/`, `src/util/`, `src/jwt/`) is what made the removal a bounded
> change rather than a rewrite.

The C++ HTTP server (`src/shttps/transport/` + `src/SipiHttpServer.{h,cpp}` + the
`server` subcommand of the C++ `//src/cli:sipi` binary) is deleted. The Rust axum
shell (`src/server-rs` + `src/cli-rs`) is the sole production server; it drives the
C++ image engine (`libsipi`) over the FFI seam (`src/server-rs/src/ffi.rs`). The
C++ `//src/cli:sipi` binary remains, but only for the offline verbs
(`convert` / `verify` / `query` / `compare` / `health`).

We accept this because the Rust shell has been the deployed production surface for
weeks (the C++ server was never deployed after the cutover — it survived only as
the differential oracle). Retaining a non-deployed reference server carries a
standing cost: every engine and seam change had to keep two callers compiling and
in behavioral lockstep, the differential gate was a dedicated CI leg, and the
oracle's transport dragged prometheus-cpp, a Lua/SQLite link surface, and a
`GET /metrics` endpoint into the graph that production does not use. Removing the
oracle collapses that surface to the one server that ships.

## What is deleted

- `src/shttps/` in its entirety — the HTTP/socket transport (`Server`,
  `Connection`, `SockStream`, `ChunkReader`, `SocketControl`, `ThreadControl`,
  `ConnectionMetrics`), plus the oracle's `certificate/`, `docroot/`, `scripts/`,
  and `shttps.config.lua`. The domain modules ADR-0013 extracted
  (`src/scripting/`, `src/util/`, `src/jwt/`) survive; the interim reverse edges
  from `transport/` back into them die with the transport.
- `src/SipiHttpServer.{h,cpp}` and the `server`-mode branch of `src/cli/cli_app.cpp`.
- `src/observability/connection_metrics_adapter.{h,cpp}` — the transport→`Metrics`
  bridge, whose only caller was the deleted server mode.
- **The differential parity gate, without replacement** — `test/e2e/tests/differential.rs`,
  the `$SIPI_BIN_REF` plumbing, `just bazel-test-differential` /
  `just differential-coverage-check`, and the dedicated CI leg. The regression net
  is now the subject-only tests: **e2e + approval + proptest + unit**. There is no
  second binary to diff against, so parity is no longer a meaningful assertion.
- **prometheus-cpp and `GET /metrics`** — the metrics singleton
  (`Sipi::observability::Metrics`) is rewritten from prometheus-cpp types to plain
  lock-free atomics (`Counter` / `Gauge`). The 20 scalar fields production reads
  cross the FFI seam as `SipiMetricsSnapshot` and are exported over OTLP by the Rust
  shell — that bridge is unchanged and is the sole production metrics path. The
  prometheus-cpp `bazel_dep` + its BCR patch are dropped.
- **The `//fuzz/handlers` libFuzzer target** — it fuzzed the C++ classifier
  `handlers::iiif_handler::parse_iiif_uri`, which was on the oracle path only.
  Production parses IIIF URIs entirely in Rust (`src/server-rs/src/iiif.rs`). A
  cargo-fuzz harness against `iiif.rs::parse_request` (the production parser) is a
  tracked follow-up under DEV-6969; until it lands there is no IIIF-parser fuzzing.

## Considered Options

- **Keep the oracle indefinitely as a differential reference** — rejected. The
  parity gate's value was bounded to the cutover window; the Rust shell has been the
  production surface for weeks. Every engine change paying a two-caller lockstep tax
  for a binary that never ships is a standing cost with no live benefit.
- **Replace the differential gate with a recorded golden-response corpus** —
  rejected. The e2e, approval, and proptest suites already assert the shell's
  behavior directly against fixtures and the IIIF spec. A frozen golden corpus would
  duplicate that net and re-introduce a maintenance burden without a second
  implementation to catch drift between.
- **Retarget the fuzz harness at the C++ `src/iiifparser/` string parsers** —
  rejected. Those parsers are also off the production path (the oracle used them;
  the Rust shell reimplements them). Fuzzing them would guard dead code. The
  valuable target is the Rust `iiif::parse_request`, which needs a Rust fuzz
  harness — separate work, tracked, not smuggled into a deletion.

## Consequences

- `src/BUILD.bazel`, `src/observability/BUILD.bazel`, and the metadata packages no
  longer depend on `//src/shttps:shttps`; the stale re-export edges that reached
  `src/util` *through* shttps are retargeted to `//src/util` directly.
- `Sipi::observability::Metrics` is a plain atomic-counter singleton. The
  `SipiMetricsSnapshot` 160-byte layout lock-step (`static_assert` +
  `offset_of!`, mirrored in `server-rs/src/ffi.rs`) is unchanged and still gates
  drift. The label-fanned `read_shape_*` / `essentials_hash_mismatch_*` counters
  remain engine-internal (not snapshotted) — the recorded observability gap is
  unchanged by this ADR (see `metrics_registry_test.cpp`).
- [ADR-0001](0001-shttps-as-strangler-fig-target.md) and
  [ADR-0013](0013-shttps-as-internal-module.md) are retained as historical record
  with closing notes; the strangler-fig migration they describe is now complete.
- A repo-wide grep for the deleted oracle (`SipiHttpServer`, the C++ `server` mode,
  the differential gate, prometheus, `GET /metrics`) finds only historical ADR
  content and the surviving `shttps::` C++ namespace (the domain symbols kept their
  namespace when they moved to `src/util/` / `src/scripting/`).
