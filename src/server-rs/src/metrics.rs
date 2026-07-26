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
//! bridged: `rejected_connections_total` and `waiting_connections` are never
//! written on the FFI serve path, so under this shell they stay zero, and the
//! pool publishes its own `sipi.pool.*` analogues instead.
//!
//! `every_snapshot_field_is_accounted_for` in the tests below is the tripwire: a
//! field added to the snapshot must land in [`COUNTERS`], [`GAUGES`], or that
//! two-name exclusion list, or the test fails. Nothing else detects a metric that
//! stops at the seam — it just never appears in Grafana.
//!
//! opentelemetry 0.31 has no batch-observer API (`register_callback` was
//! removed), so each instrument carries its own callback and each snapshots the
//! singleton. The read is a cheap singleton copy and collection runs at the
//! reader interval (60s), so the ~22 reads per cycle are immaterial.
//!
//! Two instruments are **synchronous** rather than observable, because they
//! record a distribution over individual requests that no end-of-interval poll
//! can reconstruct: [`record_http_duration`] (request latency) and
//! [`record_decode_estimate`] (per-serve decode-memory estimate). Their handles
//! are kept in `OnceLock`s, and a request that arrives before [`register`] ran
//! records nothing.

use std::sync::{Arc, OnceLock};
use std::time::Instant;

use axum::extract::{MatchedPath, Request};
use axum::middleware::Next;
use axum::response::Response;
use opentelemetry::metrics::{Histogram, Meter};
use opentelemetry::{global, KeyValue};
use tokio::sync::Semaphore;

use crate::ffi::{self, SipiMetricsSnapshot};
use crate::preflight_cache;
use crate::routes;

/// Explicit bucket boundaries (seconds) for `http.server.request.duration`, as
/// recommended by the HTTP semantic conventions.
const HTTP_DURATION_BOUNDARIES: &[f64] = &[
    0.005, 0.01, 0.025, 0.05, 0.075, 0.1, 0.25, 0.5, 0.75, 1.0, 2.5, 5.0, 7.5, 10.0,
];

/// Bucket boundaries (bytes) for the decode-memory estimate: 1 KiB → 2 GiB,
/// matching the buckets the engine's own histogram has always used, so the
/// operator PromQL in `docs/src/operation/memory-budget.md` keeps its shape.
const DECODE_ESTIMATE_BOUNDARIES: &[f64] = &[
    1024.0,
    10_240.0,
    102_400.0,
    1_048_576.0,
    10_485_760.0,
    104_857_600.0,
    524_288_000.0,
    1_073_741_824.0,
    2_147_483_648.0,
];

/// The HTTP methods the semantic conventions treat as known; anything else is
/// reported as `_OTHER` so a client cannot mint unbounded label values by
/// sending arbitrary method tokens (a method router answers 405 *after* this
/// layer has already seen the request).
const KNOWN_METHODS: &[&str] = &[
    "CONNECT", "DELETE", "GET", "HEAD", "OPTIONS", "PATCH", "POST", "PUT", "TRACE",
];

static HTTP_DURATION: OnceLock<Histogram<f64>> = OnceLock::new();
static DECODE_ESTIMATE: OnceLock<Histogram<u64>> = OnceLock::new();

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

    // ── Synchronous histograms ──────────────────────────────────────────────
    // Recorded per request rather than polled: a latency or decode-size
    // distribution cannot be reconstructed from an end-of-interval sample.
    let _ = HTTP_DURATION.set(
        meter
            .f64_histogram("http.server.request.duration")
            .with_description("Duration of HTTP server requests")
            .with_unit("s")
            .with_boundaries(HTTP_DURATION_BOUNDARIES.to_vec())
            .build(),
    );
    let _ = DECODE_ESTIMATE.set(
        meter
            .u64_histogram("sipi.decode_memory.estimate_bytes")
            .with_description("Estimated peak decode memory for one served image")
            .with_unit("By")
            .with_boundaries(DECODE_ESTIMATE_BOUNDARIES.to_vec())
            .build(),
    );

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

    // ── Preflight access-cache metrics ──────────────────────────────────────
    // The shell's opt-in cache in front of the `pre_flight` hook (see
    // `crate::preflight_cache`). Zero unless the cache is enabled
    // (`--preflight-cache-ttl > 0`); disabled by default, so no request touches it.
    meter
        .u64_observable_counter("sipi.preflight_cache.hits")
        .with_description("Preflight access-cache hits (hook skipped)")
        .with_callback(|observer| observer.observe(preflight_cache::hits(), &[]))
        .build();
    meter
        .u64_observable_counter("sipi.preflight_cache.misses")
        .with_description("Preflight access-cache misses (hook ran)")
        .with_callback(|observer| observer.observe(preflight_cache::misses(), &[]))
        .build();
    meter
        .i64_observable_gauge("sipi.preflight_cache.entries")
        .with_description(
            "Filled slots in the preflight access-cache (incl. expired-but-not-yet-reclaimed; \
             trends toward the slot ceiling on a busy server)",
        )
        .with_callback(|observer| observer.observe(preflight_cache::entries(), &[]))
        .build();
}

/// axum middleware recording `http.server.request.duration` for one request.
///
/// Registered as the outermost router layer, so the observed duration covers the
/// whole in-router path — including the tracing layers — and is the closest thing
/// to what the client waited for. `/health` and `/favicon.ico` are registered
/// after the layers and so stay out of the histogram, matching their exclusion
/// from the trace pipeline.
///
/// Attributes are the route *template* (never the raw path), the normalised
/// method, and the status code, so the label set stays bounded by the routing
/// table rather than by the request stream.
pub(crate) async fn record_http_duration(req: Request, next: Next) -> Response {
    let method = normalise_method(req.method().as_str());
    let route = req
        .extensions()
        .get::<MatchedPath>()
        .map(|matched| matched.as_str().to_owned());
    let start = Instant::now();
    let response = next.run(req).await;
    let elapsed = start.elapsed().as_secs_f64();

    if let Some(histogram) = HTTP_DURATION.get() {
        let mut attributes = vec![
            KeyValue::new("http.request.method", method),
            KeyValue::new(
                "http.response.status_code",
                i64::from(response.status().as_u16()),
            ),
        ];
        // Conditionally required by the semantic conventions: present only when a
        // route actually matched.
        if let Some(route) = route {
            attributes.push(KeyValue::new("http.route", route));
        }
        histogram.record(elapsed, &attributes);
    }
    response
}

/// Map a request method onto the semantic conventions' known-method set,
/// collapsing anything else to `_OTHER` (see [`KNOWN_METHODS`]).
fn normalise_method(method: &str) -> &'static str {
    KNOWN_METHODS
        .iter()
        .find(|known| **known == method)
        .copied()
        .unwrap_or("_OTHER")
}

/// Record one serve's estimated peak decode memory, as carried back from the
/// engine by [`crate::ffi::serve_timings_take`]. A zero estimate means no decode
/// ran (cache hit, HEAD, or passthrough) and is not a sample.
pub(crate) fn record_decode_estimate(estimate_bytes: u64) {
    if estimate_bytes == 0 {
        return;
    }
    if let Some(histogram) = DECODE_ESTIMATE.get() {
        histogram.record(estimate_bytes, &[]);
    }
}

/// The 16 live monotonic counters: OTel name, description, and the field to read
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
    (
        "sipi.tiff_pyramid.reduced_decodes",
        "TIFF decodes served from a reduced pyramid level",
        |s| s.tiff_pyramid_reduced_decodes_total,
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

#[cfg(test)]
mod tests {
    use super::{COUNTERS, GAUGES};
    use crate::ffi::SipiMetricsSnapshot;
    use std::collections::HashSet;
    use std::mem::size_of;

    /// Snapshot fields deliberately not exported, by field name. Neither is
    /// written on the FFI serve path, so both stay permanently zero under this
    /// shell; the pool publishes `sipi.pool.waiting` and `sipi.pool.load_shed`
    /// instead.
    ///
    /// Lives here rather than beside the tables because it exists to be counted,
    /// not read — the reasoning is in the module docs.
    const NOT_BRIDGED: &[&str] = &["rejected_connections_total", "waiting_connections"];

    /// Every field the engine hands across the seam must either be exported or be
    /// listed as deliberately unexported.
    ///
    /// This is the check that was missing when the OTLP cutover shipped: the
    /// `SipiMetricsSnapshot` layout test pins the struct's *shape*, so a field
    /// added C++-side and never bridged passed silently and simply never reached
    /// production.
    ///
    /// The field count comes from the struct size rather than a hand-maintained
    /// name list, which would just be one more place to forget: every snapshot
    /// field is an 8-byte `u64`/`i64`, so `size_of / 8` is exact, and it moves on
    /// its own when a field is added.
    #[test]
    fn every_snapshot_field_is_accounted_for() {
        let field_count = size_of::<SipiMetricsSnapshot>() / 8;
        let accounted = COUNTERS.len() + GAUGES.len() + NOT_BRIDGED.len();
        assert_eq!(
            accounted, field_count,
            "{field_count} snapshot fields but {accounted} accounted for — a new \
             field must be added to COUNTERS, GAUGES, or NOT_BRIDGED, or it will \
             never reach OTLP"
        );
    }

    #[test]
    fn instrument_names_are_unique() {
        // A duplicate name would have two callbacks reporting one series, which
        // the SDK reconciles silently rather than rejecting.
        let names = names();
        let unique: HashSet<&&str> = names.iter().collect();
        assert_eq!(unique.len(), names.len(), "duplicate instrument name");
    }

    #[test]
    fn instrument_names_are_sipi_namespaced() {
        // Engine names reach dashboards through the collector's OTLP→Prometheus
        // normalization, which maps `.` to `_`. Underscores *within* a segment are
        // fine and intended (`sipi.cache.size_bytes` → `sipi_cache_size_bytes`);
        // an empty segment is not, because it renders a double underscore.
        for name in names() {
            assert!(
                name.starts_with("sipi."),
                "{name} should be namespaced under `sipi.`"
            );
            assert!(
                !name.ends_with('.') && !name.contains(".."),
                "{name} has an empty name segment"
            );
        }
    }

    /// Every engine instrument name, counters then gauges.
    fn names() -> Vec<&'static str> {
        COUNTERS
            .iter()
            .map(|(name, ..)| *name)
            .chain(GAUGES.iter().map(|(name, ..)| *name))
            .collect()
    }
}
