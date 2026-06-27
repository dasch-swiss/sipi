# Monitoring for the NativeLink VM. The demonstrator's whole point is reliability
# vs BuildBuddy's CAS, so the alert that matters is "node unreachable"
# (uptime metric absent). The dashboard surfaces the signals the build-perf
# analysis reads to classify CPU-bound vs critical-path-bound: CPU, disk I/O,
# and network.
#
# Guest memory and NativeLink's own runtime stats need the Ops Agent / a metrics
# scrape and are out of scope here — the primary perf signal is the Bazel
# --profile (client-side) plus these VM metrics and the BuildBuddy BES timeline.

resource "google_monitoring_notification_channel" "email" {
  project      = local.project
  display_name = "DaSCH infra alerts (email)"
  type         = "email"

  labels = {
    email_address = local.alert_email
  }
}

resource "google_monitoring_alert_policy" "instance_down" {
  project      = local.project
  display_name = "nativelink-rbe VM unreachable"
  combiner     = "OR"

  conditions {
    display_name = "uptime metric absent for 5m (VM stopped/crashed/unreachable)"

    condition_absent {
      # instance/uptime is the agentless hypervisor metric (emitted without the
      # Ops Agent); its absence means the VM is stopped/unreachable.
      filter = join(" AND ", [
        "resource.type = \"gce_instance\"",
        "resource.labels.instance_id = \"${google_compute_instance.nativelink.instance_id}\"",
        "metric.type = \"compute.googleapis.com/instance/uptime\"",
      ])
      duration = "300s"

      aggregations {
        alignment_period   = "60s"
        per_series_aligner = "ALIGN_RATE"
      }

      trigger {
        count = 1
      }
    }
  }

  notification_channels = [google_monitoring_notification_channel.email.id]

  documentation {
    content   = "The NativeLink RBE VM (nativelink-rbe) is reporting no uptime metric — it is stopped, crashed, or unreachable. With --noremote_local_fallback, CI jobs pointed at it will fail fast. Check `gcloud compute instances get-serial-port-output nativelink-rbe --zone us-central1-a` and `systemctl status nativelink`. See infra/nativelink/README.md."
    mime_type = "text/markdown"
  }
}

resource "google_monitoring_dashboard" "nativelink" {
  project = local.project

  dashboard_json = jsonencode({
    displayName = "nativelink-rbe"
    mosaicLayout = {
      columns = 12
      tiles = [
        {
          xPos = 0, yPos = 0, width = 6, height = 4
          widget = {
            title = "CPU utilization"
            xyChart = {
              dataSets = [{
                timeSeriesQuery = {
                  timeSeriesFilter = {
                    filter = "resource.type=\"gce_instance\" resource.labels.instance_id=\"${google_compute_instance.nativelink.instance_id}\" metric.type=\"compute.googleapis.com/instance/cpu/utilization\""
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
          xPos = 6, yPos = 0, width = 6, height = 4
          widget = {
            title = "Disk throughput (read + write bytes/s)"
            xyChart = {
              dataSets = [
                {
                  timeSeriesQuery = {
                    timeSeriesFilter = {
                      filter = "resource.type=\"gce_instance\" resource.labels.instance_id=\"${google_compute_instance.nativelink.instance_id}\" metric.type=\"compute.googleapis.com/instance/disk/read_bytes_count\""
                      aggregation = {
                        alignmentPeriod  = "60s"
                        perSeriesAligner = "ALIGN_RATE"
                      }
                    }
                  }
                },
                {
                  timeSeriesQuery = {
                    timeSeriesFilter = {
                      filter = "resource.type=\"gce_instance\" resource.labels.instance_id=\"${google_compute_instance.nativelink.instance_id}\" metric.type=\"compute.googleapis.com/instance/disk/write_bytes_count\""
                      aggregation = {
                        alignmentPeriod  = "60s"
                        perSeriesAligner = "ALIGN_RATE"
                      }
                    }
                  }
                },
              ]
            }
          }
        },
        {
          xPos = 0, yPos = 4, width = 6, height = 4
          widget = {
            title = "Network throughput (received + sent bytes/s)"
            xyChart = {
              dataSets = [
                {
                  timeSeriesQuery = {
                    timeSeriesFilter = {
                      filter = "resource.type=\"gce_instance\" resource.labels.instance_id=\"${google_compute_instance.nativelink.instance_id}\" metric.type=\"compute.googleapis.com/instance/network/received_bytes_count\""
                      aggregation = {
                        alignmentPeriod  = "60s"
                        perSeriesAligner = "ALIGN_RATE"
                      }
                    }
                  }
                },
                {
                  timeSeriesQuery = {
                    timeSeriesFilter = {
                      filter = "resource.type=\"gce_instance\" resource.labels.instance_id=\"${google_compute_instance.nativelink.instance_id}\" metric.type=\"compute.googleapis.com/instance/network/sent_bytes_count\""
                      aggregation = {
                        alignmentPeriod  = "60s"
                        perSeriesAligner = "ALIGN_RATE"
                      }
                    }
                  }
                },
              ]
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
