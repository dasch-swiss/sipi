output "endpoint" {
  description = "NativeLink RE-API gRPC endpoint (mTLS). Use for both --remote_cache and --remote_executor."
  value       = "grpcs://${google_compute_address.nativelink.address}:50051"
}

output "static_ip" {
  description = "Reserved static external IP — use as the server cert IP SAN when generating the mTLS certs (see README)."
  value       = google_compute_address.nativelink.address
}

output "dashboard_url" {
  description = "Cloud Monitoring console URL for the NativeLink VM dashboard."
  value       = "https://console.cloud.google.com/monitoring/dashboards/builder/${reverse(split("/", google_monitoring_dashboard.nativelink.id))[0]}?project=${local.project}"
}
