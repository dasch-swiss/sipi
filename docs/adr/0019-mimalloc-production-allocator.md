---
status: accepted
---

# mimalloc is the production allocator, statically linked into the Rust shell binary

On 2026-07-29 — the first day the Rust-shell SIPI 6.x served production traffic
on vre-prod-01 — the server's container RSS ratcheted ~0.5 GB/h to its 4 GiB
cgroup limit and the kernel OOM-killed it after six hours (anon-rss 4.1 GB,
file-rss 31 MB). The engine's own decode-memory accounting
(`sipi_decode_memory_used_bytes`) returned to ~0 between requests throughout:
every decode buffer was freed, yet RSS never came back down.

A controlled replay (identical ~1,500-request JP2-only corpus against the
released v6.2.2 image, 15 minutes per configuration) isolated the allocator as
the only variable:

| allocator | RSS floor slope (15 min) |
|---|---|
| glibc default | +2.5 GiB/h |
| glibc, `MALLOC_ARENA_MAX=2` | flat within noise |
| jemalloc (`LD_PRELOAD`) | flat within noise |

**There is no leak.** The ratchet is glibc allocator retention: every JP2
decode creates and destroys a full Kakadu worker-thread pool
(`kdu_get_num_processors()` threads per request, `src/formats/SipiIOJ2k.cpp`),
and that cross-thread malloc/free churn fragments glibc's per-thread arenas (up
to 8 × cores of them), each of which retains its high-water mark instead of
returning freed memory to the OS. Production serves exclusively JP2, so every
request drives this pattern. The churn has a precise birthday: multithreaded
JP2 decode (`7bdb6349`, first released in 6.2.1) — a pre-6.2.1 build replayed
under the identical load stays flat on default glibc, because single-threaded
decode never spreads allocations across arenas.

## Decision

**Link an override allocator with sharded free lists — mimalloc v3, vendored
as a native `cc_library` (`bazel/mimalloc.BUILD.bazel`) — statically into
`//src/cli-rs:sipi`, Linux targets only.** macOS dev builds stay on the system
allocator.

**Why vendored v3 and not the BCR 2.x module:** the BCR drop-in (2.2.4) was
tried first and **failed the replay gate catastrophically** — RSS pinned at
~5.9 GiB within two minutes and the container OOM-ed at its 6 GiB limit in
six, worse than the glibc ratchet it was meant to fix. mimalloc 2.x's segment
architecture retains terminated threads' freed memory (abandoned segments;
`abandoned_page_purge` defaults off, reclaim is lazy), and per-request thread
churn is precisely that worst case; option tuning
(`MIMALLOC_ABANDONED_PAGE_PURGE=1`, `MIMALLOC_PURGE_DELAY=1`,
`MIMALLOC_MAX_SEGMENT_RECLAIM=100`) did not save it. mimalloc v3 replaced
segments with per-page management and reclaims dead threads' pages on free —
built for this workload — but BCR carries only 2.x, so the
BCR-drop-in-or-vendor rule (ADR-0015) resolves to vendoring: pin the v3
release tarball with a hand-written `cc_library` (mimalloc needs no configure
step, so the BUILD file is small).

**Why mimalloc and not jemalloc**, given jemalloc produced the flat validation
line: jemalloc upstream was archived in 2025 and its BCR packaging is
alpha-stage from an individual's fork; Debian's prebuilt `libjemalloc2` links
`libstdc++`, which `distroless_base` does not ship; and the
`tikv-jemalloc-sys` crate route is barred by this repo's build-script policy
(the `aws-lc-sys` precedent in `MODULE.bazel`). mimalloc's free-list sharding
targets exactly the cross-thread-churn failure mode, the BCR module is
packaged from `microsoft/mimalloc` by a core Bazel maintainer, it is pure C,
and it is BUILT for override use. The switch is gated on mimalloc reproducing
jemalloc's flat line in the same replay harness.

**Mechanism.** `rust_binary` has no `malloc=` attribute, so the module's
public alias `@mimalloc//:mimalloc` is linked as a plain Linux-only dep
(`_ALLOCATOR` in `src/cli-rs/BUILD.bazel`) — on Linux that produces the exact
link line `malloc=` would. The BCR `:mimalloc-lib` is `alwayslink`
(whole-archive), so the override objects — including the glibc-internal
`__libc_malloc` aliases and the Itanium-mangled C++ operators — are always in
the link, ahead of the lazily-scanned static libc++abi and the libc DSO.
Symbols defined in the executable that shared objects also define are exported
to the dynamic table by default, so libc-internal allocations preempt to
mimalloc as well.

**The interposition probe is part of the contract.** The one real hazard is
*partial* interposition (override symbols silently not exported): the binary's
`free` would be mimalloc while libc-internal `malloc` stays glibc — latent
heap corruption (the engine frees `scandir`-allocated entries in
`src/SipiCache.cpp`). At startup, `allocator::init()` in `src/cli-rs/src/main.rs`
probes a libc-internal allocation (`getcwd(NULL, 0)`) with
`mi_is_in_heap_region` and aborts on mismatch rather than serving.

**Allocator observability moves with the allocator.** `sipi::malloc_stats`
stays allocator-agnostic: the final-link binary registers a stats reader
(`mi_stats_get` prefix read) via `set_source`; without one, the library falls
back to glibc `mallinfo2`. The `sipi.malloc.*` OTLP gauges therefore report
the allocator that is actually serving the heap.

**The C++ engine binary `//src/cli:sipi` stays on glibc**, deliberately: it is
oracle-only (never deployed), and keeping it un-switched preserves an
allocator-isolated baseline for memory comparisons. The differential parity
gate compares HTTP/pixel output and is allocator-neutral.

## Consequences

- `MALLOC_ARENA_MAX=2` is removed from the OCI image env: with glibc's
  allocator bypassed it configures nothing that serves requests.
- Downstream crates that replace this binary with their own `main` (per the
  `cli-rs` extension contract) choose their own allocator; the `sipi` library
  neither requires nor assumes mimalloc. A downstream `main` that links no
  override gets correct glibc gauges via the fallback.
- Kakadu's per-request thread-pool create/destroy remains a perf smell worth
  revisiting on its own merits (thread-env reuse); this decision removes its
  memory consequence, not its cost.
- If mimalloc ever regresses, the validated fallback is a jemalloc
  `LD_PRELOAD` image layer (flat line on the same harness, 2026-07-29).
