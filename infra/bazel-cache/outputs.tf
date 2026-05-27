output "dashboard_url" {
  description = "Cloud Monitoring console URL for the bazel-cache-proxy dashboard."
  value       = "https://console.cloud.google.com/monitoring/dashboards/builder/${reverse(split("/", google_monitoring_dashboard.bazel_cache.id))[0]}?project=${local.project}"
}

output "service_url" {
  description = "bazel-cache-proxy Cloud Run gRPC endpoint."
  value       = google_cloud_run_v2_service.bazel_cache_proxy.uri
}
