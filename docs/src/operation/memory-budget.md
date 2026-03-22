# Memory Budget

Global decode memory budget that prevents OOM from concurrent large image decodes by tracking aggregate memory consumption across all in-flight decode operations.

## Why

The thread pool controls CPU concurrency but has no awareness of memory. Multiple concurrent full-resolution JP2 decodes (e.g., 20000x30000 pixels) can each consume 1-2GB, exhausting a 4GB container. The per-request pixel limit caps individual requests and the per-client rate limiter throttles per-client throughput, but neither prevents aggregate memory exhaustion from multiple legitimate clients requesting large images simultaneously.

## How It Works

1. **Precise estimation from IIIF parameters:** Before each decode, the actual decode buffer size is computed from IIIF region/size parameters. For JP2, this accounts for DWT reduce levels and ROI restrictions — a tile request on a 20000x30000 source estimates ~4MB, not 4.8GB.

2. **Pipeline-aware peak estimation:** Walks the processing stages (decode → scale → rotate → ICC convert) and returns the maximum concurrent allocation at any point, accounting for 2-stage downscale intermediates and rotation expansion.

3. **Lock-free accounting:** Uses `std::atomic<size_t>` with compare-exchange for zero-contention acquire/release. Budget check adds nanoseconds vs. millisecond decode times.

4. **RAII release:** `MemoryBudgetGuard` releases budget on all exit paths including exceptions. No manual cleanup needed.

## Configuration

| Parameter | Default (binary) | Default (ops-deploy) | Description |
|-----------|-----------------|---------------------|-------------|
| `max_decode_memory` | `"0"` (auto) | `"0"` (auto) | Budget in bytes. `0` = auto-detect (75% of container memory). Accepts `M`/`G` suffixes: `"2G"`, `"500M"` |
| `decode_memory_mode` | `"off"` | `"monitor"` | `"off"`, `"monitor"` (log only), `"enforce"` (HTTP 503) |

All parameters available via:
- Lua config: `max_decode_memory`, `decode_memory_mode`
- CLI flags: `--max-decode-memory`, `--decode-memory-mode`
- Environment: `SIPI_MAX_DECODE_MEMORY`, `SIPI_DECODE_MEMORY_MODE`

### Auto-Detection

When `max_decode_memory = "0"` (default), the budget is set to 75% of detected memory:
1. **cgroups v2:** `/sys/fs/cgroup/memory.max`
2. **cgroups v1:** `/sys/fs/cgroup/memory/memory.limit_in_bytes`
3. **Linux fallback:** `/proc/meminfo` MemTotal
4. **macOS:** `sysctl hw.memsize`
5. **Fallback:** 1 GB if detection fails

The 25% headroom covers kernel buffers, Sipi heap, cache, Lua, and thread stacks.

## Monitor to Enforce Workflow

1. **Deploy in monitor mode** (default in ops-deploy):
   - Budget is tracked and logged but requests are never rejected
   - `sipi_decode_memory_decisions_total{action="shadow_rejected"}` shows what *would* be blocked

2. **Observe metrics** (1-2 weeks):
   - Budget utilization: `sipi_decode_memory_used_bytes / sipi_decode_memory_budget_bytes` — should be < 0.8 normally
   - Shadow rejection rate: `rate(sipi_decode_memory_decisions_total{action="shadow_rejected"}[5m])`
   - Request size distribution: `histogram_quantile(0.99, sipi_decode_memory_estimate_bytes)`

3. **Tune budget** if needed:
   - If shadow rejections are frequent on normal tile traffic, budget is too low
   - Use the histogram to understand what size requests are being served

4. **Switch to enforce**: Set `SIPI_DECODE_MEMORY_MODE=enforce` (or `DSP_IIIF_DECODE_MEMORY_MODE=enforce` in ops-deploy). Redeploy.

## Prometheus Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `sipi_decode_memory_budget_bytes` | Gauge | — | Configured budget (set once at startup) |
| `sipi_decode_memory_used_bytes` | Gauge | — | Currently allocated to in-flight decodes |
| `sipi_decode_memory_decisions_total` | Counter | `action` | `acquired`, `rejected`, `shadow_rejected` |
| `sipi_decode_memory_near_limit_total` | Counter | — | Acquisitions where usage > 80% of budget |
| `sipi_decode_memory_estimate_bytes` | Histogram | — | Per-request peak memory estimates |

## Operational Dashboards

```promql
# Budget utilization (should be < 0.8)
sipi_decode_memory_used_bytes / sipi_decode_memory_budget_bytes

# Rejection rate (should be 0 under normal load)
rate(sipi_decode_memory_decisions_total{action="rejected"}[5m])

# Early warning (budget getting tight)
rate(sipi_decode_memory_near_limit_total[5m])

# Largest 1% of requests
histogram_quantile(0.99, rate(sipi_decode_memory_estimate_bytes_bucket[5m]))
```

## Traffic Patterns

| Request Type | Typical Estimate | Budget Impact |
|-------------|-----------------|---------------|
| Tile (256x256) | < 1 MB | Negligible — passes instantly |
| Thumbnail (/full/,128/) | < 100 KB | Negligible |
| Medium (/full/,2000/) | 50-120 MB | Moderate |
| Full resolution (/full/max/) | 1-5 GB | High — budget limits concurrency |
| Full + rotation (/full/max/90/) | 2-10 GB | Very high |

## Troubleshooting

**Budget seems too restrictive (503s on normal traffic):**
- Check `histogram_quantile(0.5, sipi_decode_memory_estimate_bytes)` — median should be < 1MB for tile traffic
- If median is high, check for clients not using tiles (direct `/full/max/` requests)
- Increase budget or add more container memory

**OOM despite budget enabled:**
- Check mode is `enforce`, not `monitor`
- Check `sipi_decode_memory_budget_bytes` matches expected container memory
- Memory outside decode pipeline (cache, Lua, HTTP buffers) is not budgeted
