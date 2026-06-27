# infra/bootstrap

Terraform/OpenTofu config that manages **one thing**: the shared remote-state
bucket `gs://dasch-tf-state` (project `dsp-repository-automation`, `us-central1`).
Every other config in `infra/` stores its state in this bucket under a distinct
`prefix`.

The bucket has object **versioning** (state history / recovery), uniform access,
public-access prevention, a noncurrent-version lifecycle rule (cap version
growth), and `lifecycle { prevent_destroy = true }` so it cannot be removed by an
accidental `tofu destroy`.

## One-time bootstrap

The bucket that holds this config's state is created *by* this config
(chicken-and-egg), so the first apply runs on the local backend, then migrates
its own state into the bucket:

```bash
cd infra/bootstrap
tofu init                 # local backend
tofu apply                # creates gs://dasch-tf-state
# then uncomment the backend "gcs" block in versions.tf and:
tofu init -migrate-state  # moves this state into the bucket it just created
```

After that, normal `tofu plan` / `tofu apply` runs against the GCS backend.

## More

The RBE topology, rationale, and runbooks live in
[`docs/src/development/rbe.md`](../../docs/src/development/rbe.md) and
[`infra/nativelink/RUNBOOK.md`](../nativelink/RUNBOOK.md). The service this
state backs is in [`infra/nativelink`](../nativelink).
