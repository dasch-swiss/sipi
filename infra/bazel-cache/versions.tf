terraform {
  required_version = ">= 1.6"

  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 6.0"
    }
  }

  backend "gcs" {
    bucket = "dasch-tf-state"
    prefix = "bazel-cache-proxy"
  }
}

provider "google" {
  project = local.project
  region  = local.region
}
