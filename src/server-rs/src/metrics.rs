//! The OTLP metrics bridge: engine counters + the shell's own concurrency
//! signals, exported as OTel observable instruments.
//!
//! The C++ engine keeps its own metrics singleton (cache / rate-limiter /
//! decode-memory / admission counters + gauges), read across the seam as a flat
//! [`SipiMetricsSnapshot`]. This module registers an OTel **observable** (async)
//! instrument per field: on each collection the SDK invokes the callback, which
//! snapshots the singleton and reports the current value — a pull, not a push,
//! matching the engine side's pure `sipi_guard`-only read. The meter provider
//! itself is built in [`crate::telemetry`] (fail-open on
//! `OTEL_EXPORTER_OTLP_ENDPOINT`); with no provider installed the global meter is
//! a no-op and every instrument here silently does nothing.
//!
//! Instrument names are OTel-idiomatic (`sipi.cache.hits`), which the standard
//! OTLP→Prometheus normalization at the collector renders as the existing
//! dashboard names (`sipi_cache_hits_total`). Two snapshot fields are **not**
//! bridged — `rejected_connections_total` and `waiting_connections` are never
//! written on the FFI serve path, so they stay zero (see
//! [`crate::ffi::SipiMetricsSnapshot`]); the shell exposes its own analogs
//! instead, `sipi.pool.load_shed` and `sipi.pool.waiting` (the bounded wait queue
//! in front of the pool), both below.
//!
//! opentelemetry 0.31 has no batch-observer API (`register_callback` was
//! removed), so each instrument carries its own callback and each snapshots the
//! singleton. The read is a cheap singleton copy and collection runs at the
//! reader interval (60s), so the ~22 reads per cycle are immaterial.

use std::sync::Arc;

use opentelemetry::global;
use opentelemetry::metrics::Meter;
use tokio::sync::Semaphore;

use crate::ffi::{self, SipiMetricsSnapshot};
use crate::routes;

/// Register the engine + pool observable instruments against the global meter.
/// Safe to call unconditionally: with no meter provider installed (no OTLP
/// endpoint) the global meter is a no-op and this registers nothing observable.
/// Call once, after [`crate::telemetry::init`] has set the global provider and
/// after the [`crate::routes::AppState`] pool exists (its permit count feeds the
/// concurrency gauges).
///
/// The instrument handles are intentionally dropped: `build()` registers the
/// callback with the SDK meter pipeline, which owns it for the meter provider's
/// lifetime; the returned handle carries none of that state.
pub(crate) fn register(pool: Arc<Semaphore>, permits_total: usize) {
    let meter = global::meter("sipi");

    // ── Engine counters (monotonic) ─────────────────────────────────────────
    for (name, description, extract) in COUNTERS {
        counter(&meter, name, description, *extract);
    }

    // ── Engine gauges ───────────────────────────────────────────────────────
    for (name, description, unit, extract) in GAUGES {
        gauge(&meter, name, description, unit, *extract);
    }

    // ── Engine-pool concurrency metrics ─────────────────────────────────────
    // Engine-pool permits in flight: total − currently-available — the real
    // saturation signal.
    meter
        .i64_observable_gauge("sipi.pool.permits_in_use")
        .with_description("Engine-pool permits currently held (blocking engine work in flight)")
        .with_callback(move |observer| {
            let in_use = permits_total.saturating_sub(pool.available_permits());
            observer.observe(in_use as i64, &[]);
        })
        .build();
    // Total permits (the configured worker count); fixed after startup.
    meter
        .i64_observable_gauge("sipi.pool.permits_total")
        .with_description("Engine-pool total permit count (the configured worker count)")
        .with_callback(move |observer| observer.observe(permits_total as i64, &[]))
        .build();
    // 503 load-shed count: every backpressure shed (immediate + queue-timeout).
    meter
        .u64_observable_counter("sipi.pool.load_shed")
        .with_description("Requests shed with 503 because the engine pool was saturated")
        .with_callback(|observer| observer.observe(routes::load_shed_total(), &[]))
        .build();
    // Requests currently parked in the wait queue for a permit. With
    // `permits_in_use` (== permits_total under load) this is the full saturation
    // picture: a rising `waiting` is sustained overload approaching the shed edge.
    meter
        .i64_observable_gauge("sipi.pool.waiting")
        .with_description("Requests currently waiting for an engine-pool permit")
        .with_callback(|observer| observer.observe(routes::waiting(), &[]))
        .build();
    // Queue-timeout sheds — the subset of `load_shed` that waited past
    // `queue_timeout` rather than shedding immediately on a full queue.
    meter
        .u64_observable_counter("sipi.pool.queue_timeout")
        .with_description("Requests shed with 503 after waiting past the queue timeout")
        .with_callback(|observer| observer.observe(routes::queue_timeout_total(), &[]))
        .build();
}

/// The 15 live monotonic counters: OTel name, description, and the field to read
/// from a snapshot. (`rejected_connections_total` is omitted — transport-dead.)
type CounterRow = (&'static str, &'static str, fn(&SipiMetricsSnapshot) -> u64);
const COUNTERS: &[CounterRow] = &[
    ("sipi.cache.hits", "Cache hits", |s| s.cache_hits_total),
    ("sipi.cache.misses", "Cache misses", |s| {
        s.cache_misses_total
    }),
    ("sipi.cache.evictions", "Cache entries evicted", |s| {
        s.cache_evictions_total
    }),
    (
        "sipi.cache.skips",
        "Files too large to cache (skipped)",
        |s| s.cache_skips_total,
    ),
    (
        "sipi.image_too_large",
        "Requests rejected by the output pixel limit",
        |s| s.image_too_large_total,
    ),
    (
        "sipi.client_disconnected",
        "Requests aborted by client disconnect",
        |s| s.client_disconnected_total,
    ),
    (
        "sipi.memory_alloc_failures",
        "Allocation failures during image processing",
        |s| s.memory_alloc_failures_total,
    ),
    (
        "sipi.rate_limit.allowed",
        "Rate-limiter: requests allowed",
        |s| s.rate_limit_allowed_total,
    ),
    (
        "sipi.rate_limit.rejected",
        "Rate-limiter: requests rejected",
        |s| s.rate_limit_rejected_total,
    ),
    (
        "sipi.rate_limit.shadow_rejected",
        "Rate-limiter: shadow-mode rejections",
        |s| s.rate_limit_shadow_rejected_total,
    ),
    (
        "sipi.rate_limit.near_limit",
        "Rate-limiter: times a client exceeded 80% of its budget",
        |s| s.rate_limit_near_limit_total,
    ),
    (
        "sipi.decode_memory.acquired",
        "Decode-memory budget: acquisitions",
        |s| s.decode_memory_acquired_total,
    ),
    (
        "sipi.decode_memory.rejected",
        "Decode-memory budget: rejections",
        |s| s.decode_memory_rejected_total,
    ),
    (
        "sipi.decode_memory.shadow_rejected",
        "Decode-memory budget: shadow-mode rejections",
        |s| s.decode_memory_shadow_rejected_total,
    ),
    (
        "sipi.decode_memory.near_limit",
        "Decode-memory budget: times usage exceeded 80% of budget",
        |s| s.decode_memory_near_limit_total,
    ),
];

/// The 7 live gauges: OTel name, description, unit (`""` = none), and the field.
/// (`waiting_connections` is omitted — transport-dead.)
type GaugeRow = (
    &'static str,
    &'static str,
    &'static str,
    fn(&SipiMetricsSnapshot) -> i64,
);
const GAUGES: &[GaugeRow] = &[
    ("sipi.cache.size_bytes", "Current cache size", "By", |s| {
        s.cache_size_bytes
    }),
    ("sipi.cache.files", "Current cached file count", "", |s| {
        s.cache_files
    }),
    (
        "sipi.cache.size_limit_bytes",
        "Configured cache size limit (-1 = unlimited)",
        "By",
        |s| s.cache_size_limit_bytes,
    ),
    (
        "sipi.cache.files_limit",
        "Configured cache file-count limit (0 = none)",
        "",
        |s| s.cache_files_limit,
    ),
    (
        "sipi.rate_limit.clients_tracked",
        "Active client entries in the rate limiter",
        "",
        |s| s.rate_limit_clients_tracked,
    ),
    (
        "sipi.decode_memory.budget_bytes",
        "Configured decode-memory budget",
        "By",
        |s| s.decode_memory_budget_bytes,
    ),
    (
        "sipi.decode_memory.used_bytes",
        "Decode memory currently in use",
        "By",
        |s| s.decode_memory_used_bytes,
    ),
];

/// Build one observable `u64` counter whose callback snapshots the engine
/// metrics and reports `extract`'s field. A failed snapshot observes nothing
/// (fail-safe on the collection thread). The handle is dropped (see
/// [`register`]).
fn counter(
    meter: &Meter,
    name: &'static str,
    description: &'static str,
    extract: fn(&SipiMetricsSnapshot) -> u64,
) {
    meter
        .u64_observable_counter(name)
        .with_description(description)
        .with_callback(move |observer| {
            if let Some(snap) = ffi::metrics_snapshot() {
                observer.observe(extract(&snap), &[]);
            }
        })
        .build();
}

/// Build one observable `i64` gauge (see [`counter`]); `unit` is applied only
/// when non-empty (byte gauges pass `"By"`).
fn gauge(
    meter: &Meter,
    name: &'static str,
    description: &'static str,
    unit: &'static str,
    extract: fn(&SipiMetricsSnapshot) -> i64,
) {
    let mut builder = meter
        .i64_observable_gauge(name)
        .with_description(description);
    if !unit.is_empty() {
        builder = builder.with_unit(unit);
    }
    builder
        .with_callback(move |observer| {
            if let Some(snap) = ffi::metrics_snapshot() {
                observer.observe(extract(&snap), &[]);
            }
        })
        .build();
}
