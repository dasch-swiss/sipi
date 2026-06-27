locals {
  project     = "dsp-repository-automation"
  region      = "us-central1"
  zone        = "us-central1-a"
  name        = "nativelink-rbe"
  alert_email = "i@dasch.swiss"
  network     = "default"

  # NativeLink ships no standalone release binary and its OCI image is distroless
  # (no shell, so build `genrule`/`bash` actions can't run inside it). The VM
  # builds this tag from source on first boot (see startup.sh) and runs the
  # binary on bare Ubuntu, giving every action a full POSIX userland. Bump
  # deliberately; a bump rebuilds on the next VM (re)creation.
  nativelink_version = "v1.5.2"

  # bazel-remote — the http_archive download cache (Remote Asset API) co-located on
  # the VM, replacing the Cloud Run proxy. Prebuilt release binary, fetched + sha256-
  # verified by startup.sh; bump the version + its sha256 together. amd64 binary.
  bazel_remote_version = "v2.6.1"
  bazel_remote_sha256  = "025d53aeb03a7fdd4a0e76262a5ae9eeee9f64d53ca510deff1c84cf3f276784"

  # 16 vCPU / 64 GB cross-compiles all three target arches (exec stays x86_64).
  machine_type = "n2-standard-16"

  # Fast CAS tier + action work_directory live on this SSD and MUST share one
  # filesystem (NativeLink hardlinks CAS blobs into each action sandbox;
  # cross-device hardlinks fail with EXDEV). Hardlinked blobs are non-evictable
  # while an action holds them, so cap the store max_bytes < capacity (.tftpl).
  # Capped at 200, not 500: us-central1's default SSD_TOTAL_GB quota is 500, and
  # attaching the disk re-counts it at instance-insert, so 2 * size must be <= 500.
  # Raise the regional SSD quota to grow this back toward 500.
  ssd_size_gb = 200

  # Durable slow tier for the CAS + AC fast_slow stores: a second, larger block
  # device the SSD hot tier falls through to. A local filesystem has no per-object
  # write-rate limit (unlike GCS, which 429'd on the hot empty-stderr digest); the
  # durable tier is correctness/durability, not the hot path (the SSD hot tier above
  # serves reads). pd-standard (HDD), NOT pd-ssd/pd-balanced: pd-standard draws on
  # the separate, large DISKS_TOTAL_GB quota, sidestepping the constrained 500 GB
  # SSD_TOTAL_GB pool the pd-ssd + the pd-balanced boot disk already share. If the
  # mandatory synchronous slow write ever bottlenecks cold builds, upgrade to
  # pd-balanced + raise SSD_TOTAL_GB (~3000 GB to cover the insert-time double-count).
  slow_size_gb = 1000

  server_crt_secret = "nativelink-server-crt"
  server_key_secret = "nativelink-server-key"
  clients_ca_secret = "nativelink-clients-ca-crt"
}

# Service account the NativeLink node runs as. Kept minimal: secretAccessor on the
# three TLS secrets (no bucket access; both CAS tiers are local block devices) plus
# write-only metric/log roles for the Ops Agent (telemetry only — a worker
# compromise can emit spurious metrics/logs, not read or mutate project state). The
# blast radius is bounded to those, not the whole project.
resource "google_service_account" "nativelink" {
  project      = local.project
  account_id   = local.name
  display_name = "NativeLink RBE node (GCE VM)"
}

# mTLS material. The secret *resources* are managed here; their *versions* (the
# CA-signed server cert/key and the client CA) are added out-of-band by the
# cert-bootstrap runbook (README) and deliberately kept out of Terraform state —
# same pattern as bazel-cache's htpasswd. The VM's systemd ExecStartPre fetches
# them and retries until they exist, so the node self-heals once they're added.
resource "google_secret_manager_secret" "server_crt" {
  project   = local.project
  secret_id = local.server_crt_secret
  replication {
    auto {}
  }
}

resource "google_secret_manager_secret" "server_key" {
  project   = local.project
  secret_id = local.server_key_secret
  replication {
    auto {}
  }
}

resource "google_secret_manager_secret" "clients_ca" {
  project   = local.project
  secret_id = local.clients_ca_secret
  replication {
    auto {}
  }
}

resource "google_secret_manager_secret_iam_member" "tls_accessor" {
  for_each = toset([
    google_secret_manager_secret.server_crt.secret_id,
    google_secret_manager_secret.server_key.secret_id,
    google_secret_manager_secret.clients_ca.secret_id,
  ])
  project   = local.project
  secret_id = each.value
  role      = "roles/secretmanager.secretAccessor"
  member    = "serviceAccount:${google_service_account.nativelink.email}"
}

# Ops Agent telemetry: guest metrics (memory, per-filesystem disk %) + the
# nativelink/bazel-remote systemd journal shipped to Cloud Monitoring/Logging, so
# the worker is observable without SSH. Write-only roles; the cloud-platform OAuth
# scope already permits the APIs — these IAM bindings are the actual authorization.
resource "google_project_iam_member" "ops_agent_metrics" {
  project = local.project
  role    = "roles/monitoring.metricWriter"
  member  = "serviceAccount:${google_service_account.nativelink.email}"
}

resource "google_project_iam_member" "ops_agent_logs" {
  project = local.project
  role    = "roles/logging.logWriter"
  member  = "serviceAccount:${google_service_account.nativelink.email}"
}

# Reserved static external IP. It is the server cert's IP SAN, and it persists
# across VM recreation — so the same mTLS cert keeps working when the VM is
# rebuilt (e.g. to pick up a NativeLink version bump).
resource "google_compute_address" "nativelink" {
  name    = local.name
  project = local.project
  region  = local.region
}

# Dedicated SSD for the fast CAS tier + action work_directory (see locals).
resource "google_compute_disk" "ssd" {
  name    = "${local.name}-ssd"
  project = local.project
  zone    = local.zone
  type    = "pd-ssd"
  size    = local.ssd_size_gb
}

# Durable slow tier for the CAS + AC fast_slow stores (see locals + the .tftpl).
# pd-standard (HDD): a durability/correctness fallback, not the hot path; draws on
# the DISKS_TOTAL_GB quota, not the constrained SSD pool. A separate resource from
# the instance, so it survives VM reset/stop-start AND full instance recreation (the
# instance just re-attaches it). It does NOT survive `tofu destroy` or a disk
# -replace; the data is a rebuildable build cache, so for real disaster recovery
# snapshot it rather than relying on prevent_destroy.
resource "google_compute_disk" "slow" {
  name    = "${local.name}-slow"
  project = local.project
  zone    = local.zone
  type    = "pd-standard"
  size    = local.slow_size_gb
}

resource "google_compute_instance" "nativelink" {
  name         = local.name
  project      = local.project
  zone         = local.zone
  machine_type = local.machine_type
  tags         = [local.name]

  boot_disk {
    initialize_params {
      image = "ubuntu-os-cloud/ubuntu-2404-lts-amd64"
      size  = 50
      type  = "pd-balanced"
    }
  }

  attached_disk {
    source      = google_compute_disk.ssd.id
    device_name = "nativelink-ssd"
  }

  attached_disk {
    source      = google_compute_disk.slow.id
    device_name = "nativelink-slow"
  }

  network_interface {
    network = local.network
    access_config {
      nat_ip = google_compute_address.nativelink.address
    }
  }

  service_account {
    email = google_service_account.nativelink.email
    # cloud-platform is required because Secret Manager has no narrower OAuth
    # scope. The actual authorization boundary is the SA's IAM — secretAccessor
    # on the three TLS secrets (no bucket access; both CAS tiers are local disks)
    # — not the token scope, so a leaked token is bounded to those, not the whole
    # project.
    scopes = ["cloud-platform"]
  }

  shielded_instance_config {
    enable_secure_boot          = true
    enable_vtpm                 = true
    enable_integrity_monitoring = true
  }

  metadata = {
    startup-script = file("${path.module}/startup.sh")
    # The NativeLink config (no template vars) is delivered via metadata;
    # startup.sh writes it to disk. Kept as a standalone reviewable file at
    # nativelink.json5.tftpl.
    nativelink-config    = templatefile("${path.module}/nativelink.json5.tftpl", {})
    nativelink-version   = local.nativelink_version
    bazel-remote-version = local.bazel_remote_version
    bazel-remote-sha256  = local.bazel_remote_sha256
    server-crt-secret    = google_secret_manager_secret.server_crt.secret_id
    server-key-secret    = google_secret_manager_secret.server_key.secret_id
    clients-ca-secret    = google_secret_manager_secret.clients_ca.secret_id
  }
}

# mTLS-gated gRPC ports: 50051 = NativeLink RE-API, 50052 = bazel-remote (the
# http_archive download cache). The client-cert requirement (both services
# validate against clients-ca) is the security boundary; this rule only opens the
# ports. The worker_api port (50061) and bazel-remote's HTTP (localhost:8080) are
# intentionally NOT opened — they bind localhost. source_ranges=0.0.0.0/0 is a
# deliberate single-layer choice: GitHub's hosted runners egress from a broad,
# changing CIDR set, so mTLS (not IP filtering) is the gate. Production graduation
# should restrict this to the published GitHub Actions ranges + admin CIDRs.
resource "google_compute_firewall" "nativelink_grpc" {
  name    = "${local.name}-allow-grpc"
  project = local.project
  network = local.network

  allow {
    protocol = "tcp"
    ports    = ["50051", "50052"]
  }
  source_ranges = ["0.0.0.0/0"]
  target_tags   = [local.name]
}

# SSH for admin/debug, restricted to Google IAP's TCP-forwarding range so the
# box is reachable via `gcloud compute ssh --tunnel-through-iap` but not the
# open internet.
resource "google_compute_firewall" "nativelink_ssh_iap" {
  name    = "${local.name}-allow-ssh-iap"
  project = local.project
  network = local.network

  allow {
    protocol = "tcp"
    ports    = ["22"]
  }
  source_ranges = ["35.235.240.0/20"]
  target_tags   = [local.name]
}
