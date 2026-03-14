# Rate Limiter

Per-client sliding-window rate limiter that tracks pixel budget consumption over a configurable time window.

## Why

Prevents resource exhaustion from bots or abusive clients that bypass tile-based browsing and request full-resolution renders. The February 2026 OOM incident was caused by a bot sending 111 full-region requests in 18 minutes (~2.2 GP total pixels).

## Configuration

| Parameter | Default (binary) | Default (ops-deploy) | Description |
|-----------|-----------------|---------------------|-------------|
| `rate_limit_mode` | `off` | `monitor` | `off`, `monitor` (log only), `enforce` (HTTP 429) |
| `rate_limit_max_pixels` | `0` (disabled) | `500000000` (500MP) | Max pixels per client per window |
| `rate_limit_window` | `600` (10 min) | `600` | Sliding window in seconds |
| `rate_limit_pixel_threshold` | `2000000` (2MP) | `2000000` | Requests below this are free (exempts tiles) |

All parameters available via Lua config, CLI flags (`--rate-limit-*`), and environment variables (`SIPI_RATE_LIMIT_*`).

## Monitor to Enforce Workflow

1. **Deploy in monitor mode** (default in ops-deploy):
   - Rate limiter logs and counts but never returns 429
   - `sipi_rate_limit_decisions_total{action="shadow_rejected"}` shows what *would* be blocked

2. **Observe metrics** (1-2 weeks):
   - Dashboard: `rate(sipi_rate_limit_decisions_total{action="shadow_rejected"}[5m])`
   - If shadow rejection rate > 5%, thresholds may be too aggressive
   - Check Loki logs for which IPs would be affected

3. **Tune thresholds** if needed:
   - Raise `rate_limit_max_pixels` if legitimate heavy users are affected
   - Adjust `rate_limit_pixel_threshold` if tile sizes differ

4. **Switch to enforce**: Change `DSP_IIIF_RATE_LIMIT_MODE` to `enforce`. Redeploy.

## Prometheus Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `sipi_rate_limit_decisions_total` | Counter | `action` (allowed/rejected/shadow_rejected) | Core metric — rejection ratio |
| `sipi_rate_limit_near_limit_total` | Counter | — | Clients at >80% of budget (early warning) |
| `sipi_rate_limit_clients_tracked` | Gauge | — | Active client entries in map |

## Loki Queries

Per-IP detail (not in Prometheus to avoid cardinality explosion):

```logql
{job="sipi"} | json | event="rate_limit_exceeded"
```

Find heavy hitters:
```logql
{job="sipi"} | json | event="rate_limit_exceeded" | line_format "{{.client_ip}} {{.pixels_consumed}}/{{.budget}}"
```

## Traffic Patterns

- **1024x1024 JP2 tiles** (~1MP): Free — below `rate_limit_pixel_threshold`
- **Full-resolution render** (~19MP): Consumes budget
- **Legitimate deep zoom** (10 images, 150 tiles each): ~600MP-1.5GP in tile pixels, but tiles are free
- **Harvesting bot**: Full-region requests bypass tiles, quickly exceeds 500MP budget

## Troubleshooting

- **False positives**: Raise `rate_limit_max_pixels` or lower `rate_limit_pixel_threshold`
- **False negatives**: Lower `rate_limit_max_pixels` or raise `rate_limit_pixel_threshold`
- **Unbounded memory**: Check `sipi_rate_limit_clients_tracked` gauge — map is swept every 1000 checks
