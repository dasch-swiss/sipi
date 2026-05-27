# Health Endpoint

## `/health` Endpoint

Returns JSON with server status, version, and uptime:

```json
{"status":"ok","version":"3.18.0","uptime_seconds":3600}
```

- **Response time**: < 5ms (no I/O, no Lua, no auth)
- **Method**: GET only
- **Route**: Registered before the catch-all IIIF handler

## Healthcheck Ownership

The image does **not** embed a Docker `HEALTHCHECK`. `HEALTHCHECK` is a
Docker-specific extension, not part of the OCI image spec, so the Bazel
`rules_oci` image carries none. Liveness is owned by the orchestration layer —
the docker-compose / Swarm `healthcheck:` stanza (provisioned in ops-deploy,
INFRA-1226):

```yaml
healthcheck:
  test: ["CMD", "/sbin/sipi", "health", "--port", "1024"]
  interval: 30s
  timeout: 5s
  start_period: 10s
  retries: 3
```

The check uses the self-contained
[`sipi health`](../guide/running.md#health-check) subcommand, which probes
`http://127.0.0.1:1024/health` and exits `0` (healthy) or `1` (unhealthy).
Because the probe is a sipi subcommand, the image needs no `curl` binary for the
healthcheck.

**Swarm behavior**: Docker Swarm has a single HEALTHCHECK — no separate liveness/readiness probes. When HEALTHCHECK fails after `retries`, Swarm kills and replaces the container. The `start_period` gives 10s for initialization (failures don't count, but container *receives traffic* during this period).

**Design consequence**: `/health` returns healthy once initialization completes. Only return unhealthy for truly fatal conditions (deadlocks, corrupted state), not for transient load.

## Traefik Configuration

The `/health` endpoint is exposed externally via Traefik for UptimeRobot monitoring:

```yaml
- traefik.http.routers.{{ STACK }}-iiif-health.rule=Host(`{{ DSP_IIIF_HOST }}`) && Path(`/health`)
- traefik.http.routers.{{ STACK }}-iiif-health.service={{ STACK }}-iiif
- traefik.http.routers.{{ STACK }}-iiif-health.entrypoints=websecure
- traefik.http.routers.{{ STACK }}-iiif-health.tls=true
- traefik.http.routers.{{ STACK }}-iiif-health.tls.certresolver=leresolver
```

## UptimeRobot Configuration

Monitor `https://iiif.example.com/health`. Expected status: 200,
expected body contains: `"status":"ok"`. The Traefik label is
provisioned in ops-deploy.
