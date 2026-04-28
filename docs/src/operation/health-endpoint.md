# Health Endpoint

## `/health` Endpoint

Returns JSON with server status, version, and uptime:

```json
{"status":"ok","version":"3.18.0","uptime_seconds":3600}
```

- **Response time**: < 5ms (no I/O, no Lua, no auth)
- **Method**: GET only
- **Route**: Registered before the catch-all IIIF handler

## Docker Swarm HEALTHCHECK

The image (built from `flake.nix` via `pkgs.dockerTools.streamLayeredImage`)
declares the equivalent of:

```
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -sf http://localhost:1024/health || exit 1
```

The actual encoding is in `flake.nix` under `config.Healthcheck`
(durations in nanoseconds).

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
