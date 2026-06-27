# infra/nativelink

Terraform/OpenTofu module for the **NativeLink remote-build-execution (RBE)
demonstrator** — a single GCE VM running NativeLink (CAS + Action Cache +
Scheduler + one local Worker) in one process, behind mTLS.

A BuildBuddy RBE trial worked but its hosted CAS served partial Merkle trees
mid-build, so this self-hosts the CAS so we own eviction. It is a
**demonstrator meant to graduate to prod**, not a throwaway — the mTLS model and
this module are built to split into a distributed topology later (see
[`RUNBOOK.md`](RUNBOOK.md) §13). For the engineering rationale — the
cross-compile pattern, the RBE-vs-cache-only performance comparison, and the
hermetic-llvm header patch — see
[`docs/src/development/rbe.md`](../../docs/src/development/rbe.md).

## What it provisions

- `google_compute_instance` **`nativelink-rbe`** (Ubuntu 24.04, `n2-standard-16`)
  with a 200 GB `pd-ssd` as the HOT cache (CAS hot tier + worker fast tier + action
  `work_directory`, one filesystem — NativeLink hardlinks blobs into action sandboxes;
  capped at 200 GB by the region's 500 GB SSD quota, since instance-insert re-counts
  the attached disk — raise the quota to grow it, see `main.tf`). Durability is the
  slow disk below, so the SSD only holds hot data;
- a **second 1 TB `pd-standard` disk** mounted at `/mnt/slow` — the durable slow tier
  behind the CAS + AC `fast_slow` stores. A local filesystem has no per-object
  write-rate limit (GCS 429'd on the hot empty-stderr digest); `pd-standard` (HDD) draws
  on the `DISKS_TOTAL_GB` quota, sidestepping the constrained 500 GB SSD pool, and the
  slow tier is a durability fallback (the SSD hot tier serves the hot path). Sized by the
  store `eviction_policy`, not a bucket lifecycle;
- the service account the VM runs as (secretAccessor on the three TLS secrets, plus
  `monitoring.metricWriter` + `logging.logWriter` for the Cloud Ops Agent — no bucket
  access, both CAS tiers are local disks);
- three Secret Manager *secrets* (`nativelink-server-crt`, `nativelink-server-key`,
  `nativelink-clients-ca-crt`) for the mTLS material — **versions stay out of TF**
  (added by the bootstrap in `RUNBOOK.md`), like bazel-cache's htpasswd;
- a reserved static IP (the server cert's IP SAN; stable across VM rebuilds),
  firewall (tcp:50051 + 50052 mTLS-gated; SSH only from Google IAP's range);
- monitoring: a "VM unreachable" alert + a dashboard (CPU / disk / network).

NativeLink has no release binary and its image is distroless, so the VM **builds
the pinned tag (`local.nativelink_version`) from source on first boot** and runs
it on bare Ubuntu (`startup.sh`). bazel-remote (the `http_archive` download cache
on `:50052`) installs from a prebuilt release. The config is `nativelink.json5.tftpl`,
delivered via instance metadata.

State lives in `gs://dasch-tf-state` (prefix `nativelink`), provisioned by
[`../bootstrap`](../bootstrap). Recipes: `just tf-plan-nl` / `just tf-apply-nl`.

## Operations

This whole `infra/` tree is deleted once NativeLink moves to DaSCH hardware, so the
operational detail is kept in the infra-team runbook:

**[`RUNBOOK.md`](RUNBOOK.md)** — VM topology, secrets + CI configuration, storage
design + the worker store-config, the bazel-remote download cache and the jbigkit
flake, first-time bootstrap (cert generation + upload), the config-apply procedure,
cache-wipe commands, version bumps, the Cloud Ops Agent, connectivity checks, the
cost estimate, worker sizing for DaSCH hardware, and the distributed production
topology.
