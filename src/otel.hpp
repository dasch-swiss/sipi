//
// Created by Ivan Subotic on 10.04.2024.
//

#ifndef OTEL_HPP
#define OTEL_HPP

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/exporters/ostream/log_record_exporter.h"
#include "opentelemetry/exporters/ostream/metric_exporter_factory.h"
#include "opentelemetry/exporters/ostream/span_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/logs/logger.h"
#include "opentelemetry/sdk/logs/logger_context_factory.h"
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h"
#include "opentelemetry/sdk/metrics/meter_context_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/view/view_registry_factory.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_context.h"
#include "opentelemetry/sdk/trace/tracer_context_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"

#include <cstring>
#include <fstream>
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

namespace {
// Class definition for context propagation
otlp::OtlpHttpMetricExporterOptions options;
std::string version{ "1.0.1" };
std::string name{ "app_cpp" };
std::string schema{ "https://opentelemetry.io/schemas/1.2.0" };

template<typename T> class HttpTextMapCarrier : public opentelemetry::context::propagation::TextMapCarrier
{
public:
  HttpTextMapCarrier<T>(T &headers) : headers_(headers) {}
  HttpTextMapCarrier() = default;
  virtual nostd::string_view Get(nostd::string_view key) const noexcept override
  {
    std::string key_to_compare = key.data();
    // Header's first letter seems to be  automatically capitaliazed by our test http-server, so
    // compare accordingly.
    if (key == opentelemetry::trace::propagation::kTraceParent) {
      key_to_compare = "Traceparent";
    } else if (key == opentelemetry::trace::propagation::kTraceState) {
      key_to_compare = "Tracestate";
    }
    auto it = headers_.find(key_to_compare);
    if (it != headers_.end()) { return it->second; }
    return "";
  }

  virtual void Set(nostd::string_view key, nostd::string_view value) noexcept override
  {
    headers_.insert(std::pair<std::string, std::string>(std::string(key), std::string(value)));
  }

  T headers_;
};
// ===== GENERAL SETUP =====
void initTracer()
{
  otlp::OtlpHttpExporterOptions traceOptions;
  traceOptions.url = std::string(std::getenv("GRAFANA_OTLP_ENDPOINT")) + "/v1/traces";
  traceOptions.content_type = otlp::HttpRequestContentType::kBinary;
  traceOptions.http_headers.insert(
    std::make_pair<const std::string, std::string>("Authorization", std::getenv("GRAFANA_OTLP_TOKEN")));

  resource::ResourceAttributes resource_attributes = { { "service.name", name }, { "service.version", version } };
  resource::ResourceAttributes dt_resource_attributes;
  try {
    for (string name : { "dt_metadata_e617c525669e072eebe3d0f08212e8f2.properties",
           "/var/lib/dynatrace/enrichment/dt_metadata.properties",
           "/var/lib/dynatrace/enrichment/dt_host_metadata.properties" }) {
      string file_path;
      ifstream dt_file;
      dt_file.open(name);
      if (dt_file.is_open()) {
        string dt_metadata;
        ifstream dt_properties;
        while (getline(dt_file, file_path)) {
          dt_properties.open(file_path);
          if (dt_properties.is_open()) {
            while (getline(dt_properties, dt_metadata)) {
              dt_resource_attributes.SetAttribute(
                dt_metadata.substr(0, dt_metadata.find("=")), dt_metadata.substr(dt_metadata.find("=") + 1));
            }
            dt_properties.close();
          }
        }
        dt_file.close();
      }
    }
  } catch (...) {}
  auto dt_resource = resource::Resource::Create(dt_resource_attributes);
  auto resource = resource::Resource::Create(resource_attributes);
  auto merged_resource = dt_resource.Merge(resource);
  auto exporter = otlp::OtlpHttpExporterFactory::Create(traceOptions);
  auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
  std::vector<std::unique_ptr<trace_sdk::SpanProcessor>> processors;
  processors.push_back(std::move(processor));
  auto context = trace_sdk::TracerContextFactory::Create(std::move(processors), merged_resource);
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
  resource::ResourceAttributes resource_attributes = { { "service.name", name }, { "service.version", version } };
  otlp::OtlpHttpMetricExporterOptions otlpOptions;
  auto resource = resource::Resource::Create(resource_attributes);
  otlpOptions.url = std::string(std::getenv("GRAFANA_OTLP_ENDPOINT")) + "/v1/metrics";
  otlpOptions.aggregation_temporality = otlp::PreferredAggregationTemporality::kDelta;
  otlpOptions.content_type = otlp::HttpRequestContentType::kBinary;
  otlpOptions.http_headers.insert(
    std::make_pair<const std::string, std::string>("Authorization", std::getenv("GRAFANA_OTLP_TOKEN")));
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
  std::shared_ptr<opentelemetry::metrics::MeterProvider> provider(std::move(u_provider));
  metrics_api::Provider::SetMeterProvider(provider);
}

// ===== LOG SETUP =====
void initLogger()
{
  resource::ResourceAttributes resource_attributes = { { "service.name", name }, { "service.version", version } };
  auto resource = resource::Resource::Create(resource_attributes);
  otlp::OtlpHttpLogRecordExporterOptions loggerOptions;
  loggerOptions.url = std::string(std::getenv("GRAFANA_OTLP_ENDPOINT")) + "/v1/logs";
  loggerOptions.http_headers.insert(
    std::make_pair<const std::string, std::string>("Authorization", std::getenv("GRAFANA_OTLP_TOKEN")));
  loggerOptions.content_type = opentelemetry::exporter::otlp::HttpRequestContentType::kBinary;
  auto exporter = otlp::OtlpHttpLogRecordExporterFactory::Create(loggerOptions);
  auto processor = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));
  std::vector<std::unique_ptr<logs_sdk::LogRecordProcessor>> processors;
  processors.push_back(std::move(processor));
  auto context = logs_sdk::LoggerContextFactory::Create(std::move(processors), resource);
  std::shared_ptr<logs_api::LoggerProvider> provider = logs_sdk::LoggerProviderFactory::Create(std::move(context));
  opentelemetry::logs::Provider::SetLoggerProvider(provider);
}
nostd::shared_ptr<opentelemetry::logs::Logger> get_logger(std::string scope)
{
  // TODO: add your log provider here
  return logger;
}
opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer(std::string tracer_name)
{
  // TODO: add your trace provider here
  return tracer;
}

nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> initIntCounter()
{
  // TODO: add your custom metrics here
  return request_counter;
}
}// namespace

#endif// OTEL_HPP
