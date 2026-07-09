# NativeLink RBE — Operations Runbook

**Scope:** deploy, operate, and maintain the NativeLink RBE VM. Infra-team reference only.
For the engineering rationale (cross-compile pattern, RBE vs cache-only trade-offs, Bazel
flag choices) see `docs/src/development/rbe.md`.

This entire `infra/` tree is deleted once the service moves to DaSCH hardware. Write and
read it accordingly.

---

## 1. VM topology

| Attribute | Value |
|-----------|-------|
| Instance name | `nativelink-rbe` |
| Project | `dsp-repository-automation` |
| Zone | `us-central1-a` |
| Machine type | `n2-standard-16` (16 vCPU / 64 GB RAM) |
| Static external IP | `35.202.252.136` |
| NativeLink endpoint | `grpcs://35.202.252.136:50051` |
| bazel-remote endpoint | `grpcs://35.202.252.136:50052` |
| NativeLink version | `v1.5.2` (built from source at boot) |
| bazel-remote version | `v2.6.1` (prebuilt release binary, sha256-verified) |

Two services run on the VM under the same mTLS CA, so one `--tls_certificate` validates both:

- **NativeLink** (`:50051`) — CAS + Action Cache + Scheduler + one local Worker in one process.
- **bazel-remote** (`:50052`) — `--experimental_remote_asset_api` download cache for `http_archive` fetches.

Both are managed by systemd (`nativelink.service`, `bazel-remote.service`). The worker-api
port (`:50061`) and bazel-remote's HTTP port (`:8080`) bind localhost and are never opened
externally.

### Security model

mTLS is the security boundary. Only holders of a CA-signed client cert can submit cache or
execution requests. The firewall opens `:50051` and `:50052` to `0.0.0.0/0` — mTLS, not IP
filtering, is the gate. SSH is restricted to Google IAP's TCP-forwarding range
(`35.235.240.0/20`); admin access via `gcloud compute ssh --tunnel-through-iap`.

Known demonstrator limitations that must be closed before production:

- Build actions run as the `nativelink` user and can reach IMDS (the VM SA token). The SA
  is minimal (secretAccessor on three TLS secrets; no bucket access), so the blast radius is
  bounded. Production must isolate per-action network (netns or gVisor).
- NativeLink is built at boot from source via `curl | sh` rustup and `git clone`. Production
  should pre-build, sign, and verify the binary.
- Firewall is `0.0.0.0/0` on the gRPC ports. Production should restrict to the published
  GitHub Actions IP ranges plus admin CIDRs.

---

## 2. Secrets and CI configuration

### GitHub (org-level, scoped to the `sipi` repo)

| Name | Kind | Content |
|------|------|---------|
| `REMOTEBUILD_RUNNER_ENDPOINT` | variable | `grpcs://35.202.252.136:50051` |
| `REMOTEBUILD_CACHE_ENDPOINT` | variable | `grpcs://35.202.252.136:50052` |
| `REMOTEBUILD_CA_CERT` | secret | Server CA certificate (Bazel verifies the server against it) |
| `REMOTEBUILD_CLIENT_CERT` | secret | Client certificate (CI identity) |
| `REMOTEBUILD_CLIENT_KEY` | secret | Client private key |

Scoped with `--visibility selected --repos sipi`. The client cert grants remote execution
access — do NOT expand to `all`.

### Secret Manager (project `dsp-repository-automation`)

| Secret ID | Content |
|-----------|---------|
| `nativelink-server-crt` | Server TLS certificate |
| `nativelink-server-key` | Server TLS private key |
| `nativelink-clients-ca-crt` | Client CA certificate (validates client certs) |

Secret resource objects are managed by OpenTofu. Secret *versions* (the actual cert/key
material) are added out-of-band (see bootstrap section below) and deliberately kept out of
Terraform state.

The `nativelink` service account holds `roles/secretmanager.secretAccessor` on these three
secrets, plus `roles/monitoring.metricWriter` and `roles/logging.logWriter` for the Ops
Agent.

---

## 3. Storage design

### Disks

| Disk | Type | Size | Mount | Purpose |
|------|------|------|-------|---------|
| `nativelink-rbe-ssd` (pd-ssd) | SSD | 200 GB | `/mnt/ssd` | CAS hot tier, AC hot tier, worker fast tier, action work_directory |
| `nativelink-rbe-slow` (pd-standard) | HDD | 1 TB | `/mnt/slow` | Durable CAS slow tier, AC slow tier, bazel-remote cache |
| Boot disk (pd-balanced) | — | 50 GB | `/` | OS only |

The SSD cap is 200 GB, not 500 GB, because GCE counts the attached disk again at
instance-insert against the regional `SSD_TOTAL_GB` quota (500 GB default). Raise the quota
to grow this.

pd-standard for the slow disk is intentional: it draws on the `DISKS_TOTAL_GB` quota, which
is separate and large, sidestepping the constrained 500 GB `SSD_TOTAL_GB` pool. A 1 TB
pd-balanced slow disk failed `tofu apply` on SSD quota. If the synchronous slow-tier write
bottlenecks cold builds, upgrade to pd-balanced and raise `SSD_TOTAL_GB` to ~3000 GB.

### NativeLink store topology

```
CAS_MAIN_STORE
  fast_slow:
    fast: filesystem /mnt/ssd/cas   (50 GB, LRU eviction)
    slow: filesystem /mnt/slow/cas  (800 GB, LRU eviction)
  served to: clients (cas / execution / bytestream) + worker slow tier via ref_store

AC_MAIN_STORE
  fast_slow:
    fast: filesystem /mnt/ssd/ac    (5 GB, LRU eviction)
    slow: filesystem /mnt/slow/ac   (50 GB, LRU eviction)

WORKER_FAST_SLOW_STORE
  fast_slow:
    fast: filesystem /mnt/ssd/worker  (50 GB, worker-local LRU — separate from CAS)
    fast_direction: "get"             (action outputs go to slow/CAS; fast is populated on read)
    slow: ref_store -> CAS_MAIN_STORE (fall-through: SSD hot -> /mnt/slow -> re-fetchable)
```

`/mnt/slow` is the source of truth for the CAS. An SSD-tier eviction is always re-fetchable
from `/mnt/slow`. The slow disk survives VM reset, stop/start, and full instance recreation
(it is a separate `google_compute_disk` resource that the instance re-attaches). It does NOT
survive `tofu destroy` or a disk `-replace` — snapshot for real DR.

### Why the worker has a separate fast tier

The original config bound one `fast_slow` store (with a `noop` slow tier) to both the CAS
service and the worker. This was our misconfiguration. Two coupled failures made it fatal
under concurrent builds:

1. Client upload pressure evicted blobs the worker was mid-hardlink on (shared store).
2. The `noop` slow tier made evicted blobs unrecoverable (hardlink `ENOENT` → worker panic).

Under two concurrent cold builds uploading the same toolchain digests, the worker's
`prepare_action` hit a hardlink `ENOENT` on `/mnt/ssd/cas/content.exec/…`; the burst
disconnected the worker and `try_join_all` panicked (`nativelink.rs:740`) → restart →
`UNAVAILABLE` on every leg. This was not a NativeLink bug and not OS resource exhaustion
(dmesg was clean, NOFILE was un-exhausted). NativeLink's worker contract
(`nativelink-config cas_server.rs:850`) requires the worker slow tier to eventually resolve
to the same store the scheduler and client use.

The fix (separate worker fast tier + `ref_store` slow tier → `CAS_MAIN_STORE`) mirrors the
reference `deployment-examples/docker-compose/worker-shared-cas.json5`. Verified under two
concurrent cold builds: zero panics, zero restarts, zero "file was likely evicted from cache"
errors.

### bazel-remote storage

bazel-remote's cache directory is `/mnt/slow/bazel-remote`, capped at 50 GB via `--max_size`
(in GiB). It shares the `/mnt/slow` filesystem with the CAS and AC slow tiers; total slow
disk allocation is CAS 800 GB + AC 50 GB + bazel-remote 50 GB + headroom = under the 1 TB
capacity.

---

## 4. Download cache (bazel-remote) and the jbigkit flake

bazel-remote on `:50052` serves the Remote Asset API (`--experimental_remote_asset_api`),
which Bazel uses for `http_archive` and `http_file` fetches when
`--experimental_remote_downloader` points at it. This replaces the old Cloud Run proxy.

### How cache warming works

bazel-remote warms its CAS only via its own proxy-fetch. On a `FetchBlob` miss, bazel-remote
fetches the URL itself, verifies the `sha256` qualifier (set on SIPI's `http_archive`
entries), stores the blob, and every subsequent `FetchBlob` for that hash is a permanent CAS
hit. The jbigkit tarball (~1.5 MB) in an 800 GB CAS has no real eviction risk once warmed.

A green CI build that won via `--experimental_remote_downloader_local_fallback` (the runner
downloaded the file directly) does NOT warm the bazel-remote cache. This is Bazel issue
[#14646](https://github.com/bazelbuild/bazel/issues/14646), unfixed in Bazel 9. If bazel-remote
cannot reach an upstream that the runner can, the flake persists silently across all future
builds.

### The jbigkit/cl.cam.ac.uk flake

`cl.cam.ac.uk` has been globally unreachable during CI runs (Connect timeout). The VM can
reach `cl.cam.ac.uk` (HTTP 200 verified from the VM). Once the upstream is reachable from the
VM, the first CI build after that warms the cache permanently.

The current decision (Ivan, 2026-06-27): no mirror — rely on self-warming once the upstream is
reachable again. The bulletproof fix if the flake recurs is to mirror the tarball to a host we
control (GitHub release like Kakadu, or GCS) listed first in the `urls` array.

The Apple macOS SDK has the same warming dynamic on the darwin-arm64 leg.

---

## 5. Bootstrap (first-time provisioning)

```bash
cd infra/nativelink
tofu init
tofu plan     # or: just tf-plan-nl
tofu apply    # or: just tf-apply-nl
```

`tofu apply` creates the VM, disks, static IP, firewall rules, service account, and Secret
Manager secret resources. The VM boots and starts building NativeLink from source (tens of
minutes — watch the serial console):

```bash
gcloud compute instances get-serial-port-output nativelink-rbe --zone us-central1-a
```

The `nativelink.service` ExecStartPre blocks indefinitely until the mTLS secrets exist in
Secret Manager. Generate and upload them:

```bash
IP=$(tofu output -raw static_ip)   # should be 35.202.252.136

# Server CA + server cert (IP SAN MUST equal the static IP Bazel dials)
openssl req -x509 -newkey rsa:4096 -nodes -keyout server-ca.key -out server-ca.crt \
  -days 825 -sha256 -subj "/CN=dasch-nativelink-server-ca"
openssl req -newkey rsa:4096 -nodes -keyout server.key -out server.csr \
  -subj "/CN=nativelink-rbe"
openssl x509 -req -in server.csr -CA server-ca.crt -CAkey server-ca.key -CAcreateserial \
  -out server.crt -days 825 -sha256 \
  -extfile <(printf "subjectAltName=IP:%s\nextendedKeyUsage=serverAuth\n" "$IP")

# Client CA + client cert (the CI identity)
openssl req -x509 -newkey rsa:4096 -nodes -keyout client-ca.key -out client-ca.crt \
  -days 825 -sha256 -subj "/CN=dasch-nativelink-client-ca"
openssl req -newkey rsa:4096 -nodes -keyout client.key -out client.csr \
  -subj "/CN=sipi-ci"
openssl x509 -req -in client.csr -CA client-ca.crt -CAkey client-ca.key -CAcreateserial \
  -out client.crt -days 825 -sha256 \
  -extfile <(printf "extendedKeyUsage=clientAuth\n")

# Upload VM-side secrets
P=dsp-repository-automation
gcloud secrets versions add nativelink-server-crt     --project "$P" --data-file=server.crt
gcloud secrets versions add nativelink-server-key     --project "$P" --data-file=server.key
gcloud secrets versions add nativelink-clients-ca-crt --project "$P" --data-file=client-ca.crt

# Upload CI-side secrets (org-level, scoped to sipi repo)
ORG=dasch-swiss
gh variable set REMOTEBUILD_RUNNER_ENDPOINT \
  --org "$ORG" --visibility selected --repos sipi --body "grpcs://$IP:50051"
gh variable set REMOTEBUILD_CACHE_ENDPOINT \
  --org "$ORG" --visibility selected --repos sipi --body "grpcs://$IP:50052"
gh secret set REMOTEBUILD_CA_CERT     --org "$ORG" --visibility selected --repos sipi < server-ca.crt
gh secret set REMOTEBUILD_CLIENT_CERT --org "$ORG" --visibility selected --repos sipi < client.crt
gh secret set REMOTEBUILD_CLIENT_KEY  --org "$ORG" --visibility selected --repos sipi < client.key
```

Within ~30 seconds of the Secret Manager upload, NativeLink comes up. Verify:

```bash
gcloud compute ssh nativelink-rbe --zone us-central1-a --tunnel-through-iap \
  --command 'systemctl status nativelink bazel-remote --no-pager'
```

Keep all `*-ca.key` and `*.key` files off the repo and shared drives. To rotate certs:
reissue from the same CA private key and re-upload to Secret Manager. To add a second CI
client, issue another cert from `client-ca.key`.

---

## 6. Config-apply runbook

`tofu apply` updates the `nativelink-config` instance metadata key in place. The on-VM
`/etc/nativelink/nativelink.json5` is only (re)written by `startup.sh` at boot. A VM reset
(`gcloud compute instances reset`) does NOT reliably re-run `startup.sh` — it reboots the
instance but GCE does not guarantee re-execution of startup scripts on reset.

To apply a config change without recreating the VM:

**Step 1:** run `tofu apply` to push the updated metadata.

**Step 2:** on the VM, pull the new config and restart NativeLink:

```bash
gcloud compute ssh nativelink-rbe --zone us-central1-a --tunnel-through-iap --command '
  curl -sf -H "Metadata-Flavor: Google" \
    "http://metadata.google.internal/computeMetadata/v1/instance/attributes/nativelink-config" \
    | sudo tee /etc/nativelink/nativelink.json5 >/dev/null
  sudo systemctl restart nativelink
  sleep 3
  systemctl is-active nativelink
'
```

To apply a config change AND force a full re-run of `startup.sh` (e.g. version bump or disk
resize):

```bash
tofu apply -replace=google_compute_instance.nativelink
```

This recreates the VM. The slow disk is a separate resource and survives the recreation; the
SSD hot tier is destroyed and rebuilt (cold cache after recreation is expected and correct for
version bumps).

---

## 7. Cache wipe commands

### SSD hot-tier wipe (slow-disk-warm)

Useful for forcing a warm-cache CI run without losing the durable slow tier. The worker
re-fetches any needed blobs from `/mnt/slow`.

```bash
gcloud compute ssh nativelink-rbe --zone us-central1-a --tunnel-through-iap --command '
  sudo systemctl stop nativelink
  sudo rm -rf \
    /mnt/ssd/cas/content/* /mnt/ssd/cas/temp/* \
    /mnt/ssd/worker/content/* /mnt/ssd/worker/temp/* \
    /mnt/ssd/ac/content/* /mnt/ssd/ac/temp/* \
    /mnt/ssd/work/*
  sudo systemctl start nativelink
'
```

### TRUE cold wipe (also wipe the slow disk)

Required to reproduce the concurrent same-digest upload scenario (the original crash trigger)
or to benchmark a genuine cold build. Also wipe the Bazel caches on the runners.

```bash
gcloud compute ssh nativelink-rbe --zone us-central1-a --tunnel-through-iap --command '
  sudo systemctl stop nativelink
  sudo rm -rf \
    /mnt/ssd/cas/content/* /mnt/ssd/cas/temp/* \
    /mnt/ssd/worker/content/* /mnt/ssd/worker/temp/* \
    /mnt/ssd/ac/content/* /mnt/ssd/ac/temp/* \
    /mnt/ssd/work/* \
    /mnt/slow/cas/content/* /mnt/slow/cas/temp/* \
    /mnt/slow/ac/content/* /mnt/slow/ac/temp/*
  sudo systemctl start nativelink
'
```

---

## 8. Version bump (NativeLink or bazel-remote)

1. Update `local.nativelink_version` or `local.bazel_remote_version` (+ `local.bazel_remote_sha256`) in `main.tf`.
2. For NativeLink: `tofu apply -replace=google_compute_instance.nativelink` — the startup
   script rebuilds only when the pinned tag differs from the sentinel at
   `/usr/local/lib/nativelink.version`. A plain `tofu apply` updates the metadata but the
   binary is not rebuilt until the next boot.
3. For bazel-remote: same — the sentinel is `/usr/local/lib/bazel-remote.version`.
4. Watch the serial console during the NativeLink source build (~tens of minutes on first run).

---

## 9. Cloud Ops Agent

The Ops Agent ships journald logs and host metrics to Cloud Logging and Cloud Monitoring
without requiring SSH. It was installed in the startup script after a root-cause investigation.

**Root cause it was previously absent:** `startup.sh` fetched `add-google-cloud-ops-agent.sh`
(a URL that permanently 404s). `curl -sSO` saved the HTML error page to disk and bash ran it
silently as a shell script. The correct installer is `add-google-cloud-ops-agent-repo.sh`.
The fix uses `curl -fsSL` so an HTTP error causes a loud failure instead of a silent
no-op.

The agent sends:
- **Logs:** the full systemd journal (captures `nativelink.service` and `bazel-remote.service`
  output, including NRestarts, eviction events, and panics) via a `systemd_journald` receiver.
- **Metrics:** host metrics (CPU, memory, per-filesystem disk utilisation).

The SA requires `roles/logging.logWriter` and `roles/monitoring.metricWriter`, both present
in `main.tf`. The `cloud-platform` OAuth scope on the VM permits the APIs; the IAM bindings
are the actual authorization boundary.

If the agent is not running after a VM reset, re-run the startup script manually or recreate
the VM. The install step is idempotent (guarded by `systemctl is-active google-cloud-ops-agent`).

---

## 10. Connectivity checks

```bash
# TCP reachability (no certs needed)
bash -c 'cat </dev/null >/dev/tcp/35.202.252.136/50051 && echo "50051 open"'
bash -c 'cat </dev/null >/dev/tcp/35.202.252.136/50052 && echo "50052 open"'

# Service health (on the VM)
gcloud compute ssh nativelink-rbe --zone us-central1-a --tunnel-through-iap \
  --command 'systemctl status nativelink bazel-remote google-cloud-ops-agent --no-pager'

# NativeLink logs (recent)
gcloud compute ssh nativelink-rbe --zone us-central1-a --tunnel-through-iap \
  --command 'journalctl -u nativelink -n 100 --no-pager'

# Crash / eviction grep
gcloud compute ssh nativelink-rbe --zone us-central1-a --tunnel-through-iap \
  --command 'journalctl -u nativelink | grep -iE "panic|evict|ENOENT|UNAVAILABLE|restart"'

# Disk utilisation
gcloud compute ssh nativelink-rbe --zone us-central1-a --tunnel-through-iap \
  --command 'df -h /mnt/ssd /mnt/slow'
```

---

## 11. Cost estimate (list price, no committed-use discount)

| Resource | Cost |
|----------|------|
| n2-standard-16, always-on | ~$555/mo |
| 200 GB pd-ssd | ~$34/mo |
| 1 TB pd-standard | ~$40/mo |
| **Total** | **~$629/mo** |

Run `tofu destroy` to tear everything down when the VM is no longer needed. This deletes the
static IP and both data disks. Keep the CA private keys if cert continuity is needed for a
rebuild.

---

## 12. Worker sizing for DaSCH hardware

Build-graph measurements (from `--profile` JSON on the 16-vCPU demonstrator VM):

- A single regular (fastbuild) build keeps ~6 cores busy; the build is I/O-bound, not
  CPU-bound. Worker idles at 2.3–2.5× concurrent actions on 16 vCPUs.
- A single ASan build is CPU-bound and utilises ~37 cores.
- Aggregate concurrent PR load across three CI legs is ~80–130 cores.
- The 16-vCPU demonstrator is why the ASan build timed out at 60 minutes under contention:
  insufficient cores for the CPU-bound leg plus I/O contention.

A 192-core machine is well-sized for this load. It must be paired with fast local NVMe CAS
storage. The build is I/O (CAS output-download) bound — output download is 6–9× the remote
compute time per leg on a cold cache. Feeding a 192-core worker from a slow CAS disk will
re-introduce the I/O bottleneck at a higher concurrency ceiling. NVMe or equivalent
low-latency block storage is the pairing requirement, not just CPU count.

---

## 13. Distributed production topology

The single-VM configuration is this topology collapsed into one process. When splitting for
production scale:

**CAS + Scheduler VM:**
- Runs NativeLink with `CAS_MAIN_STORE`, `AC_MAIN_STORE`, and `MAIN_SCHEDULER`.
- Has the durable block-storage slow tier (large pd-ssd or pd-balanced disk is appropriate at
  prod scale; the demonstrator uses pd-standard only to fit the GCP SSD quota).
- The `worker_api` private port (`:50061`) stays localhost on this VM.
- `server_api` (`:50051`) is the gRPC endpoint both clients and workers connect to.

**Worker VMs (N instances):**
- Each runs a NativeLink worker only.
- `WORKER_FAST_SLOW_STORE` slow tier changes from `ref_store -> CAS_MAIN_STORE` (in-process)
  to `grpc -> <CAS VM>:50051` (network).
- Same mTLS client cert model: worker VMs hold client certs signed by the client CA.

**OpenTofu module shape:**
- Same module as the current single-VM setup, refactored with `for_each` on a worker pool
  resource set. CAS VM and worker VMs are separate `google_compute_instance` resources in the
  same module.
- N workers + one scheduler serve parallel cross-branch and cross-client builds with no
  serialization at any layer (no `concurrency:` group in CI; the store config is what makes
  concurrent builds safe, not CI-side serialization).

No CI changes are required to move from single-VM to distributed: the endpoint
(`REMOTEBUILD_RUNNER_ENDPOINT`) stays the same; the scheduler routes actions to whichever
worker is available.
