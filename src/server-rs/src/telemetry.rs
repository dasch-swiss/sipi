//! Structured JSON logs + OTLP trace export for the Rust shell.
//!
//! **One log schema, shared with the C++ engine**, so Grafana Alloy parses the
//! single binary's stdout uniformly: `{"level", "message", "target"?,
//! "trace_id"?, "span_id"?}`. No timestamp — Alloy stamps on scrape (matching the
//! C++ logger, which has always omitted it). `trace_id`/`span_id` are emitted
//! only inside a span, as lowercase hex (32 / 16 chars, no dashes), so logs
//! correlate to traces in Tempo. The C++ engine emits the same core keys and is
//! stamped with the active trace context across the seam (see
//! [`crate::ffi::LogTraceScope`]).
//!
//! Trace export is gRPC-tonic to the Alloy collector (matching the dsp-repository
//! precedent), **fail-open**: with no `OTEL_EXPORTER_OTLP_ENDPOINT` no exporter is
//! built and serving is unaffected; export errors are dropped on the batch
//! processor's background thread, never propagating into request handling.

use std::fmt::{self, Write as _};

use opentelemetry::trace::{TraceContextExt, TracerProvider as _};
use opentelemetry_sdk::trace::SdkTracerProvider;
use tracing::field::{Field, Visit};
use tracing::{Event, Subscriber};
use tracing_opentelemetry::OpenTelemetrySpanExt;
use tracing_subscriber::fmt::format::Writer;
use tracing_subscriber::fmt::{FmtContext, FormatEvent, FormatFields};
use tracing_subscriber::layer::SubscriberExt;
use tracing_subscriber::registry::LookupSpan;
use tracing_subscriber::util::SubscriberInitExt;
use tracing_subscriber::EnvFilter;

/// Owns the trace (and, in local dev, log) providers so their batch exporters
/// flush on shutdown. Each is `None` when not configured (fail-open).
pub struct Telemetry {
    provider: Option<SdkTracerProvider>,
    logger_provider: Option<opentelemetry_sdk::logs::SdkLoggerProvider>,
}

impl Telemetry {
    /// Flush + shut down the exporters. The flush is blocking I/O, so callers run
    /// this off the async runtime (`spawn_blocking`).
    pub fn shutdown(self) {
        if let Some(provider) = self.provider {
            let _ = provider.shutdown();
        }
        if let Some(logger_provider) = self.logger_provider {
            let _ = logger_provider.shutdown();
        }
    }
}

/// Install the global subscriber: env-filtered JSON logs (the shared schema)
/// bridged to OTLP traces. Call once, **inside the tokio runtime** (the OTLP
/// batch exporter needs it). Idempotent — a second call is a no-op.
#[must_use]
pub fn init() -> Telemetry {
    use opentelemetry::global;
    use opentelemetry_sdk::propagation::TraceContextPropagator;

    // Continue the W3C traceparent in and out, so SIPI joins the caller's trace.
    global::set_text_map_propagator(TraceContextPropagator::new());

    // RUST_LOG / OTEL_LOG_LEVEL, defaulting to info, plus the otel::tracing=trace
    // directive the OpenTelemetry layer needs to emit spans.
    let filter = init_tracing_opentelemetry::tracing_subscriber_ext::build_level_filter_layer("")
        .unwrap_or_else(|_| EnvFilter::new("info"));

    // Fail-open: only build the exporter + bridge layer when an endpoint is set.
    let provider = build_tracer_provider();
    let otel_layer = provider
        .as_ref()
        .map(|p| tracing_opentelemetry::layer().with_tracer(p.tracer("sipi")));

    // Local-dev only: also export logs over OTLP so they land in a local LGTM
    // stack. The bridge attaches the active trace context, and a target filter
    // keeps the OTel SDK's own logs out (no export feedback loop).
    let logger_provider = build_logger_provider();
    let logs_layer = logger_provider.as_ref().map(|lp| {
        use tracing_subscriber::Layer;
        opentelemetry_appender_tracing::layer::OpenTelemetryTracingBridge::new(lp)
            .with_filter(tracing_subscriber::filter::filter_fn(|meta| !meta.target().starts_with("opentelemetry")))
    });

    let _ = tracing_subscriber::registry()
        .with(filter)
        .with(otel_layer)
        .with(logs_layer)
        .with(tracing_subscriber::fmt::layer().event_format(SharedJson))
        .try_init();

    Telemetry { provider, logger_provider }
}

/// Build the OTLP (gRPC-tonic) tracer provider, or `None` when no endpoint is
/// configured. The exporter reads the standard `OTEL_EXPORTER_OTLP_*` env.
fn build_tracer_provider() -> Option<SdkTracerProvider> {
    std::env::var("OTEL_EXPORTER_OTLP_ENDPOINT").ok()?;
    let exporter = match opentelemetry_otlp::SpanExporter::builder().with_tonic().build() {
        Ok(exporter) => exporter,
        Err(e) => {
            tracing::warn!(error = %e, "OTLP span exporter init failed; trace export disabled");
            return None;
        }
    };
    let resource = opentelemetry_sdk::Resource::builder().with_service_name("sipi").build();
    Some(
        SdkTracerProvider::builder()
            .with_batch_exporter(exporter)
            .with_resource(resource)
            .build(),
    )
}

/// Build the OTLP (gRPC-tonic) log-record provider for **local dev only**:
/// requires `SIPI_OTLP_LOGS` set (truthy) *and* an endpoint. In production logs
/// are scraped from stdout, so OTLP log export would duplicate them — hence the
/// explicit opt-in. `None` otherwise.
fn build_logger_provider() -> Option<opentelemetry_sdk::logs::SdkLoggerProvider> {
    let enabled = std::env::var("SIPI_OTLP_LOGS").map(|v| !v.is_empty() && v != "0").unwrap_or(false);
    if !enabled {
        return None;
    }
    std::env::var("OTEL_EXPORTER_OTLP_ENDPOINT").ok()?;
    let exporter = match opentelemetry_otlp::LogExporter::builder().with_tonic().build() {
        Ok(exporter) => exporter,
        Err(e) => {
            tracing::warn!(error = %e, "OTLP log exporter init failed; log export disabled");
            return None;
        }
    };
    let resource = opentelemetry_sdk::Resource::builder().with_service_name("sipi").build();
    Some(
        opentelemetry_sdk::logs::SdkLoggerProvider::builder()
            .with_batch_exporter(exporter)
            .with_resource(resource)
            .build(),
    )
}

/// The current span's trace context as lowercase-hex `(trace_id, span_id)`, or
/// `None` when there is no valid active span (so the keys are omitted rather than
/// emitted empty). Used both by the log formatter and the cross-seam stamping.
#[must_use]
pub fn current_trace_context() -> Option<(String, String)> {
    let cx = tracing::Span::current().context();
    let span = cx.span();
    let sc = span.span_context();
    if sc.is_valid() {
        Some((sc.trace_id().to_string(), sc.span_id().to_string()))
    } else {
        None
    }
}

/// Custom event formatter producing the shared `{level, message, target,
/// trace_id?, span_id?}` JSON line — the schema the C++ logger also emits.
struct SharedJson;

impl<S, N> FormatEvent<S, N> for SharedJson
where
    S: Subscriber + for<'a> LookupSpan<'a>,
    N: for<'a> FormatFields<'a> + 'static,
{
    fn format_event(&self, _ctx: &FmtContext<'_, S, N>, mut writer: Writer<'_>, event: &Event<'_>) -> fmt::Result {
        let meta = event.metadata();

        let mut visitor = JsonVisitor::default();
        event.record(&mut visitor);

        write!(writer, "{{\"level\":\"{}\"", meta.level())?;
        write!(writer, ",\"message\":")?;
        write_json_str(&mut writer, &visitor.message)?;
        write!(writer, ",\"target\":")?;
        write_json_str(&mut writer, meta.target())?;
        // Rust-only structured fields ride along as extra keys (the research
        // confirms Alloy ignores keys it isn't configured for); the C++ side has
        // no equivalent, which is fine — only the core keys must match.
        writer.write_str(&visitor.extra)?;
        if let Some((trace_id, span_id)) = current_trace_context() {
            write!(writer, ",\"trace_id\":\"{trace_id}\",\"span_id\":\"{span_id}\"")?;
        }
        writeln!(writer, "}}")
    }
}

/// Collects the `message` field and JSON-encodes every other event field into
/// `extra` (`,"key":value` fragments ready to splice into the line).
#[derive(Default)]
struct JsonVisitor {
    message: String,
    extra: String,
}

impl JsonVisitor {
    fn push_field(&mut self, name: &str, encoded_value: &str) {
        // Field names are valid identifiers, so they need no escaping beyond quotes.
        let _ = write!(self.extra, ",\"{name}\":{encoded_value}");
    }
}

impl Visit for JsonVisitor {
    fn record_debug(&mut self, field: &Field, value: &dyn fmt::Debug) {
        let rendered = format!("{value:?}");
        if field.name() == "message" {
            self.message = rendered;
        } else {
            // Debug renderings aren't valid JSON values, so quote + escape them.
            let mut quoted = String::new();
            let _ = write_json_str(&mut FmtString(&mut quoted), &rendered);
            self.push_field(field.name(), &quoted);
        }
    }

    fn record_str(&mut self, field: &Field, value: &str) {
        if field.name() == "message" {
            self.message = value.to_owned();
        } else {
            let mut quoted = String::new();
            let _ = write_json_str(&mut FmtString(&mut quoted), value);
            self.push_field(field.name(), &quoted);
        }
    }

    fn record_i64(&mut self, field: &Field, value: i64) {
        self.push_field(field.name(), &value.to_string());
    }

    fn record_u64(&mut self, field: &Field, value: u64) {
        self.push_field(field.name(), &value.to_string());
    }

    fn record_f64(&mut self, field: &Field, value: f64) {
        if value.is_finite() {
            self.push_field(field.name(), &value.to_string());
        } else {
            // JSON has no NaN/Infinity literal — quote a non-finite float so it
            // can never emit a bare token and corrupt the line.
            let mut quoted = String::new();
            let _ = write_json_str(&mut FmtString(&mut quoted), &value.to_string());
            self.push_field(field.name(), &quoted);
        }
    }

    fn record_bool(&mut self, field: &Field, value: bool) {
        self.push_field(field.name(), &value.to_string());
    }
}

/// Write a JSON-escaped, double-quoted string. Escapes the JSON-mandatory set
/// (`"`, `\`, control chars) so a message with quotes/newlines stays valid.
fn write_json_str<W: fmt::Write>(w: &mut W, s: &str) -> fmt::Result {
    w.write_char('"')?;
    for c in s.chars() {
        match c {
            '"' => w.write_str("\\\"")?,
            '\\' => w.write_str("\\\\")?,
            '\n' => w.write_str("\\n")?,
            '\r' => w.write_str("\\r")?,
            '\t' => w.write_str("\\t")?,
            c if (c as u32) < 0x20 => write!(w, "\\u{:04x}", c as u32)?,
            c => w.write_char(c)?,
        }
    }
    w.write_char('"')
}

/// Adapts a `&mut String` to `fmt::Write` for [`write_json_str`] (the formatter's
/// `Writer` and a plain `String` don't share a trait object).
struct FmtString<'a>(&'a mut String);

impl fmt::Write for FmtString<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.0.push_str(s);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn esc(s: &str) -> String {
        let mut out = String::new();
        write_json_str(&mut FmtString(&mut out), s).expect("string writer never fails");
        out
    }

    #[test]
    fn json_str_escapes_quotes_newlines_and_controls() {
        // A log message must never break out of its JSON string (log injection).
        assert_eq!(esc("ok"), "\"ok\"");
        assert_eq!(esc("a\"b"), "\"a\\\"b\"");
        assert_eq!(esc("a\nb\tc\\d"), "\"a\\nb\\tc\\\\d\"");
        assert_eq!(esc("x\u{0001}y"), "\"x\\u0001y\"");
    }
}
