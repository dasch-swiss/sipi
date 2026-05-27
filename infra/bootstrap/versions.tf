terraform {
  required_version = ">= 1.6"

  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 6.0"
    }
  }

  # Bootstrap order (chicken-and-egg): the bucket that stores this state is
  # created BY this config, so the first apply runs on the implicit local
  # backend. Once `dasch-tf-state` exists, uncomment the block below and run
  # `tofu init -migrate-state` to move this config's own state into the bucket.
  #
  # backend "gcs" {
  #   bucket = "dasch-tf-state"
  #   prefix = "bootstrap"
  # }
}

provider "google" {
  project = local.project
  region  = local.region
}
