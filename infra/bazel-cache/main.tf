locals {
  project         = "dsp-repository-automation"
  region          = "us-central1"
  service_name    = "bazel-cache-proxy"
  cache_bucket    = "dasch-bazel-cache"
  htpasswd_secret = "bazel-cache-htpasswd"
  alert_email     = "i@dasch.swiss"

  # Pinned to the digest of the image currently running on the service so the
  # binary is immutable across applies. Bump deliberately when upgrading
  # bazel-remote (resolve a new digest with `crane digest
  # buchgr/bazel-remote-cache:<tag>`).
  bazel_remote_image = "buchgr/bazel-remote-cache@sha256:dedff81150b3f217ae36cb95b57d579029ab21d9ae9e336b5bb3a81cd533b11b"
}

# Service account the cache runs as.
resource "google_service_account" "cache" {
  project      = local.project
  account_id   = local.service_name
  display_name = "bazel-remote cache proxy (Cloud Run)"
}

# Persistent proxy backend (CAS + AC). bazel-remote writes through to this
# bucket asynchronously; it is the durable tier behind the per-instance local
# disk cache. 30-day lifecycle keeps it bounded.
resource "google_storage_bucket" "cache" {
  name     = local.cache_bucket
  project  = local.project
  location = local.region

  storage_class               = "STANDARD"
  uniform_bucket_level_access = true
  public_access_prevention    = "enforced"

  lifecycle_rule {
    condition {
      age = 30
    }
    action {
      type = "Delete"
    }
  }
}

resource "google_storage_bucket_iam_member" "cache_object_admin" {
  bucket = google_storage_bucket.cache.name
  role   = "roles/storage.objectAdmin"
  member = "serviceAccount:${google_service_account.cache.email}"
}

# htpasswd basic-auth gate. The secret *resource* is managed here; its
# *versions* (the credential material) are added out-of-band by the
# password-rotation runbook and deliberately kept out of Terraform state.
resource "google_secret_manager_secret" "htpasswd" {
  project   = local.project
  secret_id = local.htpasswd_secret

  replication {
    auto {}
  }
}

resource "google_secret_manager_secret_iam_member" "htpasswd_accessor" {
  project   = local.project
  secret_id = google_secret_manager_secret.htpasswd.secret_id
  role      = "roles/secretmanager.secretAccessor"
  member    = "serviceAccount:${google_service_account.cache.email}"
}

resource "google_cloud_run_v2_service" "bazel_cache_proxy" {
  name     = local.service_name
  project  = local.project
  location = local.region
  ingress  = "INGRESS_TRAFFIC_ALL"

  # Service-level scaling floor (distinct from the per-revision ceiling in
  # template.scaling below). The provider materializes this block with
  # min_instance_count=0 (scale-to-zero); declaring it keeps `tofu plan` clean.
  scaling {
    min_instance_count = 0
  }

  template {
    service_account = google_service_account.cache.email
    # Admission cap for the single instance; sized with cpu=4 for the CI burst.
    max_instance_request_concurrency = 320
    timeout                          = "300s"

    scaling {
      min_instance_count = 0
      # One instance keeps a single shared tmpfs hot tier (best local hit rate);
      # cpu=4 + concurrency=320 size it for the concurrent CI burst. Raise vCPU
      # or max_instance_count if it saturates.
      max_instance_count = 1
    }

    containers {
      image = local.bazel_remote_image

      # --max_size (GiB) caps the local disk cache, which on Cloud Run lives in
      # RAM-backed tmpfs — keep it well below `memory`. See ci.md.
      args = [
        "--dir=/data",
        "--max_size=4",
        "--http_address=unix:///tmp/http.sock",
        "--grpc_address=0.0.0.0:8080",
        "--gcs_proxy.bucket=${local.cache_bucket}",
        "--gcs_proxy.use_default_credentials",
        "--htpasswd_file=/etc/bazel-remote/htpasswd",
        "--experimental_remote_asset_api",
      ]

      ports {
        name           = "h2c"
        container_port = 8080
      }

      resources {
        limits = {
          cpu    = "4"
          memory = "6Gi"
        }
        startup_cpu_boost = true
      }

      volume_mounts {
        name       = "htpasswd"
        mount_path = "/etc/bazel-remote"
      }

      startup_probe {
        tcp_socket {
          port = 8080
        }
        period_seconds    = 240
        timeout_seconds   = 240
        failure_threshold = 1
      }
    }

    volumes {
      name = "htpasswd"
      secret {
        secret = google_secret_manager_secret.htpasswd.secret_id
        items {
          version = "latest"
          path    = "htpasswd"
        }
      }
    }
  }

  traffic {
    type    = "TRAFFIC_TARGET_ALLOCATION_TYPE_LATEST"
    percent = 100
  }
}

# Access is gated by bazel-remote's htpasswd, not Cloud Run IAM (Bazel's gRPC
# client doesn't speak Cloud Run OIDC), so the service allows unauthenticated
# invocation. See docs/src/development/ci.md "Auth".
resource "google_cloud_run_v2_service_iam_member" "public" {
  project  = local.project
  location = local.region
  name     = google_cloud_run_v2_service.bazel_cache_proxy.name
  role     = "roles/run.invoker"
  member   = "allUsers"
}
