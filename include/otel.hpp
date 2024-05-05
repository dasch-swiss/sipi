//
// Created by Ivan Subotic on 10.04.2024.
//

#ifndef OTEL_HPP
#define OTEL_HPP

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/exporters/ostream/log_record_exporter.h"
#include "opentelemetry/exporters/ostream/log_record_exporter_factory.h"
#include "opentelemetry/exporters/ostream/metric_exporter_factory.h"
#include "opentelemetry/exporters/ostream/span_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/logs/logger.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/logs/logger.h"
#include "opentelemetry/sdk/logs/logger_context_factory.h"
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h"
#include "opentelemetry/sdk/metrics/meter_context_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/view/view_registry_factory.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_context.h"
#include "opentelemetry/sdk/trace/tracer_context_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/tracer.h"

#include "absl/strings/escaping.h"// for absl::Base64Escape

#include <cstring>
#include <fstream>
#include <generated/SipiVersion.h>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace std;
namespace nostd = opentelemetry::nostd;
namespace otlp = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;

namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;

namespace logs_api = opentelemetry::logs;
namespace logs_sdk = opentelemetry::sdk::logs;
namespace logs_exporter = opentelemetry::exporter::logs;

namespace {
// Class definition for context propagation
otlp::OtlpHttpMetricExporterOptions options;
std::string version{ BUILD_SCM_TAG };
std::string name{ "sipi" };
std::string schema{ "https://opentelemetry.io/schemas/1.2.0" };


// ===== GENERAL SETUP =====
void initTracer()
{
  const char *username_env = std::getenv("GRAFANA_OTLP_USER");
  const char *password_env = std::getenv("GRAFANA_OTLP_TOKEN");

  if (!username_env) { throw std::runtime_error("Environment variable 'GRAFANA_OTLP_USER' not found."); }
  std::string username{ username_env };

  if (!password_env) { throw std::runtime_error("Environment variable 'GRAFANA_OTLP_TOKEN' not found."); }
  std::string password{ password_env };

  const std::string credentials = username + ":" + password;
  const std::string encodedCredentials = absl::Base64Escape(credentials);
  const std::string authHeader = "Basic " + encodedCredentials;

  resource::ResourceAttributes resource_attributes = { { "service.name", name }, { "service.version", version } };
  auto resource = resource::Resource::Create(resource_attributes);

  otlp::OtlpHttpExporterOptions traceOptions;
  traceOptions.url = std::string(std::getenv("GRAFANA_OTLP_ENDPOINT")) + "/v1/traces";
  traceOptions.content_type = otlp::HttpRequestContentType::kBinary;
  traceOptions.http_headers.insert(
    std::make_pair<const std::string, const std::string>("Authorization", std::move(authHeader)));

  auto exporter = otlp::OtlpHttpExporterFactory::Create(traceOptions);
  auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
  std::vector<std::unique_ptr<trace_sdk::SpanProcessor>> processors;
  processors.push_back(std::move(processor));

  auto context = trace_sdk::TracerContextFactory::Create(std::move(processors), resource);
  std::shared_ptr<opentelemetry::trace::TracerProvider> provider =
    opentelemetry::sdk::trace::TracerProviderFactory::Create(std::move(context));
  // Set the global trace provider
  opentelemetry::trace::Provider::SetTracerProvider(provider);
  // set global propagator
  opentelemetry::context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
    opentelemetry::nostd::shared_ptr<opentelemetry::context::propagation::TextMapPropagator>(
      new opentelemetry::trace::propagation::HttpTraceContext()));
}
// ===== METRIC SETUP =====
void initMeter()
{
  const char *username_env = std::getenv("GRAFANA_OTLP_USER");
  const char *password_env = std::getenv("GRAFANA_OTLP_TOKEN");

  if (!username_env) { throw std::runtime_error("Environment variable 'GRAFANA_OTLP_USER' not found."); }
  std::string username{ username_env };

  if (!password_env) { throw std::runtime_error("Environment variable 'GRAFANA_OTLP_TOKEN' not found."); }
  std::string password{ password_env };

  const std::string credentials = username + ":" + password;
  const std::string encodedCredentials = absl::Base64Escape(credentials);
  const std::string authHeader = "Basic " + encodedCredentials;

  resource::ResourceAttributes resource_attributes = { { "service.name", name }, { "service.version", version } };
  auto resource = resource::Resource::Create(resource_attributes);

  otlp::OtlpHttpMetricExporterOptions otlpOptions;
  otlpOptions.url = std::string(std::getenv("GRAFANA_OTLP_ENDPOINT")) + "/v1/metrics";
  otlpOptions.aggregation_temporality = otlp::PreferredAggregationTemporality::kDelta;
  otlpOptions.content_type = otlp::HttpRequestContentType::kBinary;
  otlpOptions.http_headers.insert(
    std::make_pair<const std::string, const std::string>("Authorization", std::move(authHeader)));

  // This creates the exporter with the options we have defined above.
  auto exporter = otlp::OtlpHttpMetricExporterFactory::Create(otlpOptions);
  metrics_sdk::PeriodicExportingMetricReaderOptions options;
  options.export_interval_millis = std::chrono::milliseconds(1000);
  options.export_timeout_millis = std::chrono::milliseconds(500);
  std::unique_ptr<metrics_sdk::MetricReader> reader{ new metrics_sdk::PeriodicExportingMetricReader(
    std::move(exporter), options) };

  auto context =
    metrics_sdk::MeterContextFactory::Create(opentelemetry::sdk::metrics::ViewRegistryFactory::Create(), resource);
  context->AddMetricReader(std::move(reader));
  auto u_provider = metrics_sdk::MeterProviderFactory::Create(std::move(context));

  std::shared_ptr provider(std::move(u_provider));
  metrics_api::Provider::SetMeterProvider(provider);
}

// ===== LOG SETUP =====
void initLogger()
{
  const char *username_env = std::getenv("GRAFANA_OTLP_USER");
  const char *password_env = std::getenv("GRAFANA_OTLP_TOKEN");

  if (!username_env) { throw std::runtime_error("Environment variable 'GRAFANA_OTLP_USER' not found."); }
  const std::string username{ username_env };

  if (!password_env) { throw std::runtime_error("Environment variable 'GRAFANA_OTLP_TOKEN' not found."); }
  const std::string password{ password_env };

  const std::string credentials = username + ":" + password;
  const std::string encodedCredentials = absl::Base64Escape(credentials);
  const std::string authHeader = "Basic " + encodedCredentials;

  resource::ResourceAttributes resource_attributes = { { "service.name", name }, { "service.version", version } };
  auto resource = resource::Resource::Create(resource_attributes);

  otlp::OtlpHttpLogRecordExporterOptions loggerOptions;
  loggerOptions.url = std::string(std::getenv("GRAFANA_OTLP_ENDPOINT")) + "/v1/logs";
  loggerOptions.http_headers.insert(
    std::make_pair<const std::string, const std::string>("Authorization", std::move(authHeader)));
  loggerOptions.content_type = opentelemetry::exporter::otlp::HttpRequestContentType::kBinary;

  // auto exporter = otlp::OtlpHttpLogRecordExporterFactory::Create(loggerOptions);
  auto exporter = logs_exporter::OStreamLogRecordExporterFactory::Create();
  auto processor = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));
  std::vector<std::unique_ptr<logs_sdk::LogRecordProcessor>> processors;
  processors.push_back(std::move(processor));

  auto context = logs_sdk::LoggerContextFactory::Create(std::move(processors), resource);
  std::shared_ptr provider = logs_sdk::LoggerProviderFactory::Create(std::move(context));
  opentelemetry::logs::Provider::SetLoggerProvider(provider);
}

nostd::shared_ptr<opentelemetry::logs::Logger> get_logger()
{
  auto logger = logs_api::Provider::GetLoggerProvider()->GetLogger("sipi");
  return logger;
}
nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer()
{
  auto tracer = trace_api::Provider::GetTracerProvider()->GetTracer("sipi");
  return tracer;
}

nostd::shared_ptr<opentelemetry::metrics::Meter> get_meter()
{
  auto meter = metrics_api::Provider::GetMeterProvider()->GetMeter("sipi");
  return meter;
}
}// namespace

#endif// OTEL_HPP
