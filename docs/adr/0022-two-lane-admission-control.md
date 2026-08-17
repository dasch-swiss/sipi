---
status: accepted
---

# Cost-based two-lane admission control

SIPI's decode cost is bimodal. On `vre-prod-01` a 13-hour histogram of ~19,600
decodes split 68 % under 10 MB (viewer tiles) against 27 % at 100–500 MB each
(full-image downloads, overwhelmingly distributed crawler bots). With a
cost-blind worker pool (`nthreads=16`), up to sixteen 100–500 MB decodes could
run at once, pushing RSS past the envelope until the kernel OOM-killed the
process. The per-IP rate limiter was the wrong axis — a distributed swarm spreads
under every per-client budget — and was removed ([sipi#777](https://github.com/dasch-swiss/sipi/pull/777)).

The need is to protect legitimate tile traffic and the RAM envelope from
expensive full-image decodes, without a per-client budget.

## Decision

**Admission is cost-based and two-partition.** Every request is classified into a
`Tile` or `Full` partition and admitted to a pool derived from two hard caps (RAM
and CPU threads) plus two ratios and a mode:

- **Threads.** `tile_min = round(nthreads × tiles_thread_ratio)` is guaranteed to
  tiles; `full_max = nthreads − tile_min` hard-caps concurrent full decodes.
  Tiles take a global permit only and burst into idle capacity; a full takes its
  full sub-pool permit **first, then** the global permit. Full-lane-first is
  correctness-critical: global-first would let queued fulls hold global permits
  while blocked on the full sub-pool, starving tiles. Full-first bounds the fulls
  contending for global permits to `≤ full_max`, so `≥ tile_min` global permits
  are always reachable by tiles. Tile priority is therefore *bounded, not
  absolute* — a full already parked in the FIFO global queue does not yield to a
  later tile; the floor is the mechanism, stated as an explicit design choice.
- **Memory.** `full_mem = memory_limit × (1 − tiles_memory_ratio)` hard-caps the
  full partition's accounted decode bytes; tiles bypass the budget. The reserve
  `memory_limit × tiles_memory_ratio` houses tile usage + the non-decode floor.
- **Mode.** `admission_mode = basic | advanced` (no "off"; default `basic`). This
  is a two-tier model: the **basic tier** (the global CPU/thread concurrency cap)
  is always enforced, while the **advanced tier** (the full thread cap plus the
  memory budget and two-lane load-shedding) is enforced only under `advanced`.
  Under `basic` the advanced tier shadow-counts what it *would* shed, so shipping
  the default binary changes no behavior — letting the operator size the full
  partition before switching to `advanced`. An unrecognized value (e.g. a stale
  `off` or a legacy `monitor`/`enforce`) is not a startup error; it falls back to
  the `basic` default.

Everything derives from four env knobs — `SIPI_MEMORY_LIMIT`, `SIPI_NTHREADS`,
`SIPI_TILES_THREAD_RATIO` (0.5), `SIPI_TILES_MEMORY_RATIO` (0.25) — plus
`SIPI_ADMISSION_MODE`. Defaults live in the binary, so the fingerprint and shadow
counters are observable on Grafana with no ops-deploy change.

### Two rejections, two status codes

Full-partition memory exhaustion is an **immediate 503 + `Retry-After`** (the
one-shot lock-free `try_acquire` never blocks; a blocking wait here would pin
scarce permits and add a second starvation vector). A request whose estimate
*alone* exceeds `full_mem` can never succeed, so it returns **413 Payload Too
Large** (no `Retry-After`) with a distinct counter — retrying is pointless.

### Module shape: a colocated `src/throttling/` polyglot

Throttling becomes a structural component, a component-first / language-second
package per the [ADR-0021](0021-iiifparser-polyglot-colocation.md) pattern:

```
src/throttling/
├── cpp/    cc_library memory_budget — SipiMemoryBudget.{h,cpp} + SipiPeakMemory.h
│           (engine-side, post-cache; carved out of //src:engine)
└── rust/   rust_library admission — the two-partition pool (shell-side, pre-dispatch)
```

The Rust side (`//src/throttling/rust:admission`) is FFI-free: deps are `tokio`
and `//src/iiifparser/rust:iiif_parser` only — no `//src/ffi`, no C++ engine — so
its concurrency tests run without linking Kakadu. It exposes one concrete type
(`Admission`, no trait/port) built from plain config values, owning both
semaphores and the per-partition wait/shed counters (a single writer). The C++
side is a move-plus-carve of the already-self-contained memory budget, so its
unit test links the narrow target instead of `//src:sipi_lib`
([ADR-0003](0003-module-co-located-source-and-tests.md)).

**Naming:** the package carries the umbrella (`throttling`); the crate and type
carry the mechanism (`admission`) — the `//src/iiifparser/rust:iiif_parser`
pattern. Unlike ADR-0021 this is not a strangler pairing: both sides are
permanent, at different pipeline stages. The vocabulary is **admission**
throughout (pool `Admission`, partition `AdmissionKind`, mode `AdmissionMode`,
metrics `sipi_admission_*`); "lane" is deliberately not used, to keep one word
across code, metrics, and docs (maintainer decision, 2026-08-16). This lifts the
former glossary ban on "admission control" — it is now the term, kept distinct
from *Permission* (the Lua access decision).

**Deliberately not colocated: the output size guard.** It is a ~10-line
stateless check at the gate site (`serve_image.cpp`, reading
`eng.max_pixel_limit`), and the gate must stay in `src/ffi` (it orchestrates
cache-check → policies; it is the seam). Extracting ten lines into a module to
complete the package name would be shape without substance. ARCH-MAP records the
guard's gate-site location so the umbrella still has one findable index.

### Two classifiers, one intent

Lane routing needs a decision in the shell *before* dispatch (native dims unknown
there), while the memory budget classifies precisely in the engine from
`estimate_peak_memory`. The shell classifier consumes the domain `IiifParams`
from `iiif_parser::parse_request` but lives in the `admission` crate — the parser
crate stays pure FFI-free URL grammar. `large_decode_threshold_bytes` is defined
**once, in the shell config** (code default 32 MiB) and passed to the engine over
the seam at init (DUNE-003), so the two sides cannot drift. The shell derives a
pixel-count cutoff from it via a bytes-per-pixel proxy; residual heuristic drift
is exported as a disagreement counter.

### Single source for the memory-coupled config

`admission_mode`, `tiles_memory_ratio`, `large_decode_threshold_bytes`, and the
resolved `memory_limit` envelope are parsed/resolved **once, engine-side** (the
engine reads the Lua config and layers the CLI/env overrides), and the shell
reads them back over the seam (`sipi_admission_mode` / `sipi_tiles_memory_ratio` /
`sipi_large_decode_threshold_bytes` / `sipi_memory_limit_bytes`). One authority,
no drift — the shell's pool runs the same mode and classifies against the same
threshold the engine's budget uses. The thread-only knobs (`nthreads`,
`tiles_thread_ratio`, `max_waiting`, `queue_timeout`) are Rust-owned serve args,
like the pre-existing pool knobs.

## Consequences

- Tiles get a guaranteed thread floor and burst; full downloads are hard-capped
  in threads and memory and shed with 503/413 when their share is exhausted.
- The default `basic` mode enforces only the basic tier (the global-pool
  concurrency cap); the advanced tier is observe-only, so shipping the binary is
  safe and needs no ops-deploy change; the shadow counters + fingerprint metrics
  size the full partition before the switch to `advanced`.
- `basic` preserves the pre-existing global-pool concurrency bound and the
  per-partition wait accounting fix (tiles are never shed by full-queue depth);
  only the full thread cap and the full-specific rejections (the advanced tier)
  are mode-gated.
- The shell Semaphore pool (pre-dispatch) and the engine memory budget
  (post-cache) remain two distinct admission layers, as ARCH-MAP records.
- The regression net is subject-only (e2e + approval + proptest + unit); the C++
  oracle and its differential gate were removed ([ADR-0020](0020-oracle-removal.md)),
  so advanced-mode 503/413 is covered by dedicated e2e, not a diff.
