# Path 1 monitoring: alert on the built-in Cloud Run memory-utilization metric
# (the OOM signal) and a dashboard that also surfaces GCS bucket size. Scraping
# bazel-remote's own /metrics (disk bytes, hit/miss, queue depth) would need the
# GMP sidecar + an --http_address TCP change and is deliberately out of scope.

resource "google_monitoring_notification_channel" "email" {
  project      = local.project
  display_name = "DaSCH infra alerts (email)"
  type         = "email"

  labels = {
    email_address = local.alert_email
  }
}

resource "google_monitoring_alert_policy" "cache_memory" {
  project      = local.project
  display_name = "bazel-cache-proxy memory utilization high"
  combiner     = "OR"

  conditions {
    display_name = "Container memory utilization > 90%"

    condition_threshold {
      filter = join(" AND ", [
        "resource.type = \"cloud_run_revision\"",
        "resource.labels.service_name = \"${local.service_name}\"",
        "metric.type = \"run.googleapis.com/container/memory/utilizations\"",
      ])
      comparison      = "COMPARISON_GT"
      threshold_value = 0.9
      duration        = "300s"

      aggregations {
        # memory/utilizations is a distribution; reduce to the p99 scalar.
        alignment_period   = "60s"
        per_series_aligner = "ALIGN_PERCENTILE_99"
      }

      trigger {
        count = 1
      }
    }
  }

  notification_channels = [google_monitoring_notification_channel.email.id]

  documentation {
    content   = "bazel-cache-proxy is approaching its memory limit. The local disk cache lives on Cloud Run tmpfs (RAM); if this stays high, lower --max_size or raise memory. See docs/src/development/ci.md."
    mime_type = "text/markdown"
  }
}

resource "google_monitoring_dashboard" "bazel_cache" {
  project = local.project

  dashboard_json = jsonencode({
    displayName = "bazel-cache-proxy"
    mosaicLayout = {
      columns = 12
      tiles = [
        {
          xPos = 0, yPos = 0, width = 6, height = 4
          widget = {
            title = "Memory utilization (p99)"
            xyChart = {
              dataSets = [{
                timeSeriesQuery = {
                  timeSeriesFilter = {
                    filter = "resource.type=\"cloud_run_revision\" resource.labels.service_name=\"${local.service_name}\" metric.type=\"run.googleapis.com/container/memory/utilizations\""
                    aggregation = {
                      alignmentPeriod  = "60s"
                      perSeriesAligner = "ALIGN_PERCENTILE_99"
                    }
                  }
                }
              }]
            }
          }
        },
        {
          xPos = 6, yPos = 0, width = 6, height = 4
          widget = {
            title = "CPU utilization (p99)"
            xyChart = {
              dataSets = [{
                timeSeriesQuery = {
                  timeSeriesFilter = {
                    filter = "resource.type=\"cloud_run_revision\" resource.labels.service_name=\"${local.service_name}\" metric.type=\"run.googleapis.com/container/cpu/utilizations\""
                    aggregation = {
                      alignmentPeriod  = "60s"
                      perSeriesAligner = "ALIGN_PERCENTILE_99"
                    }
                  }
                }
              }]
            }
          }
        },
        {
          xPos = 0, yPos = 4, width = 6, height = 4
          widget = {
            title = "Request count (rate)"
            xyChart = {
              dataSets = [{
                timeSeriesQuery = {
                  timeSeriesFilter = {
                    filter = "resource.type=\"cloud_run_revision\" resource.labels.service_name=\"${local.service_name}\" metric.type=\"run.googleapis.com/request_count\""
                    aggregation = {
                      alignmentPeriod  = "60s"
                      perSeriesAligner = "ALIGN_RATE"
                    }
                  }
                }
              }]
            }
          }
        },
        {
          xPos = 6, yPos = 4, width = 6, height = 4
          widget = {
            title = "Instance count"
            xyChart = {
              dataSets = [{
                timeSeriesQuery = {
                  timeSeriesFilter = {
                    filter = "resource.type=\"cloud_run_revision\" resource.labels.service_name=\"${local.service_name}\" metric.type=\"run.googleapis.com/container/instance_count\""
                    aggregation = {
                      alignmentPeriod  = "60s"
                      perSeriesAligner = "ALIGN_MEAN"
                    }
                  }
                }
              }]
            }
          }
        },
        {
          xPos = 0, yPos = 8, width = 12, height = 4
          widget = {
            # storage/total_bytes is sampled ~daily; a wide window + stacked bar
            # reads best. Tracks growth / lifecycle-effectiveness / cost.
            title = "GCS bucket size (dasch-bazel-cache) — ~daily samples"
            xyChart = {
              dataSets = [{
                plotType = "STACKED_BAR"
                timeSeriesQuery = {
                  timeSeriesFilter = {
                    filter = "resource.type=\"gcs_bucket\" resource.labels.bucket_name=\"${local.cache_bucket}\" metric.type=\"storage.googleapis.com/storage/total_bytes\""
                    aggregation = {
                      alignmentPeriod  = "86400s"
                      perSeriesAligner = "ALIGN_MEAN"
                    }
                  }
                }
              }]
            }
          }
        },
      ]
    }
  })

  lifecycle {
    # The Monitoring API normalizes dashboard_json (injects targetAxis, drops
    # zero-valued xPos, reorders fields), so a byte-exact round-trip is
    # unattainable and plans would perpetually drift. To change the dashboard,
    # edit above and temporarily drop this ignore (or `tofu apply -replace`).
    ignore_changes = [dashboard_json]
  }
}
