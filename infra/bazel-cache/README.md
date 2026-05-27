# infra/bazel-cache

Terraform/OpenTofu module for the **`bazel-cache-proxy`** Cloud Run service — the
[bazel-remote](https://github.com/buchgr/bazel-remote) gRPC cache that backs CI,
plus its supporting resources:

- the Cloud Run service (`google_cloud_run_v2_service`), digest-pinned image,
  `--max_size=4` GiB local tier under `memory=6Gi`;
- the GCS proxy bucket `dasch-bazel-cache` (CAS + AC, 30-day lifecycle) and the
  service account with `roles/storage.objectAdmin`;
- the `bazel-cache-htpasswd` Secret Manager *secret* (versions stay out of TF)
  and the SA's `secretAccessor` binding;
- monitoring: a memory-utilization alert and a dashboard (memory, CPU, requests,
  instances, GCS bucket size).

State lives in `gs://dasch-tf-state` (prefix `bazel-cache-proxy`), provisioned by
[`infra/bootstrap`](../bootstrap).

## Usage

```bash
cd infra/bazel-cache
tofu init
tofu plan      # or: just tf-plan
tofu apply     # or: just tf-apply
```

If a resource already exists in GCP, `tofu import` it before `apply` so it
isn't destroyed/recreated (see [`ci.md`](../../docs/src/development/ci.md)).

## More

Topology, the tmpfs/`--max_size` root cause, auth design, and runbooks are in
[`docs/src/development/ci.md`](../../docs/src/development/ci.md) ("Cache
strategy").
