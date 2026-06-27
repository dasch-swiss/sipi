locals {
  project = "dsp-repository-automation"
  region  = "us-central1"
}

# Shared Terraform remote-state bucket for DaSCH infrastructure configs
# (infra/nativelink, and any future per-service config). `prevent_destroy`
# guards against `tofu destroy` wiping the state store itself.
resource "google_storage_bucket" "tf_state" {
  name     = "dasch-tf-state"
  project  = local.project
  location = local.region

  storage_class               = "STANDARD"
  uniform_bucket_level_access = true
  public_access_prevention    = "enforced"

  versioning {
    enabled = true
  }

  # Cap state-version growth: keep the 10 most recent noncurrent versions and
  # expire any noncurrent version older than 90 days.
  lifecycle_rule {
    condition {
      num_newer_versions = 10
    }
    action {
      type = "Delete"
    }
  }
  lifecycle_rule {
    condition {
      days_since_noncurrent_time = 90
    }
    action {
      type = "Delete"
    }
  }

  lifecycle {
    prevent_destroy = true
  }
}
