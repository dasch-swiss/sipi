# IP-Based Pixel Consumption Rate Limiter

## Context

After the OOM fix (per-request `max_pixel_limit`, connection liveness checks, `bad_alloc` handling), we need a second layer of defense: per-client pixel budget rate limiting. The OOM analysis showed a single bot (216.73.216.108) systematically requesting full-resolution renders, consuming all server memory through cumulative load even when individual requests are within the pixel limit. A per-client sliding-window pixel budget returns HTTP 429 (Too Many Requests) to clients that consume too many pixels in aggregate, with a `Retry-After` header.

### Traefik Fingerprinting Research

Traefik does **not** support JA3/JA4 TLS client fingerprinting — the raw TLS Client Hello isn't exposed to Traefik's plugin API ([GitHub issue #8627](https://github.com/traefik/traefik/issues/8627), [community forum discussion](https://community.traefik.io/t/is-there-a-way-to-get-the-tls-client-hello-in-a-plugin/26557)). However, Traefik **can** inject arbitrary custom headers via its Headers middleware or a ForwardAuth service (e.g., a hash of User-Agent + Accept-Language + other signals).

The design supports a configurable `rate_limit_client_header` (e.g. `"X-Client-Fingerprint"`) — if present, use that header value as client identity; otherwise fall back to `X-Forwarded-For` rightmost IP → `peer_ip()`. This means if you later add a ForwardAuth service that computes a client fingerprint, SIPI will use it automatically without code changes.

## Implementation

### 1. New class: `SipiRateLimiter`

**`include/SipiRateLimiter.h`** (new) + **`src/SipiRateLimiter.cpp`** (new)

Sliding-window rate limiter tracking per-client pixel consumption.

```cpp
// Data structures
struct PixelRecord { steady_clock::time_point timestamp; size_t pixels; };
struct ClientState { std::vector<PixelRecord> records; };

// State
std::unordered_map<std::string, ClientState> _clients;
std::mutex _mutex;
unsigned _window_sec;
size_t _max_pixels;

// Public API
struct CheckResult { bool allowed; size_t retry_after_sec; };
CheckResult check_and_record(const std::string &client_id, size_t pixels);
bool enabled() const;  // returns _max_pixels > 0
```

**Logic of `check_and_record`:**
1. Lock mutex. If disabled (`_max_pixels == 0`), return `{true, 0}`.
2. Compute cutoff = `now - window`. Prune expired entries for this client.
3. Sum remaining pixels. If `sum + new_pixels > _max_pixels`: compute `retry_after_sec` = seconds until oldest record expires (ceiling), return `{false, retry_after}`.
4. Otherwise push `{now, pixels}` and return `{true, 0}`.
5. Every 1000 checks, sweep the full map to evict clients with no remaining records (memory hygiene).

Budget is deducted **optimistically** (at check time, before image processing begins). This prevents burst-then-fail scenarios where multiple concurrent large requests all pass the check before any record their consumption. Slight over-penalization of clients whose requests fail for other reasons is acceptable — the window slides.

Use `steady_clock` (monotonic, not affected by clock adjustments).

### 2. Client identity resolution

**`src/SipiHttpServer.cpp`** — new static helper near top (after `send_error` functions):

```cpp
static std::string resolve_client_id(Connection &conn_obj, const std::string &client_header)
```

Priority:
1. Custom header value if configured and present (e.g. `X-Client-Fingerprint` from Traefik middleware)
2. Rightmost entry in `X-Forwarded-For` (the IP Traefik observed)
3. `conn_obj.peer_ip()` (direct connection IP)

### 3. Rate limit check in `serve_iiif`

**`src/SipiHttpServer.cpp`** — insert after the existing `max_pixel_limit` check (line ~1356), before the canonical URL block:

- Resolve client identity via `resolve_client_id()`
- Call `server->rate_limiter()->check_and_record(client_id, output_pixels)`
- If not allowed: set `Retry-After` header on `conn_obj`, then `send_error(conn_obj, Connection::TOO_MANY_REQUESTS, ...)`

Also add `TOO_MANY_REQUESTS` case to the `send_error` switch (line ~79).

### 4. Server integration

**`src/SipiHttpServer.hpp`** — add to protected section:
```cpp
std::unique_ptr<SipiRateLimiter> _rate_limiter;
std::string _rate_limit_client_header;
```
Plus public accessors: `rate_limiter(unsigned, size_t)` (factory), `rate_limiter()` (getter), `rate_limit_client_header()` getter/setter.

### 5. Configuration

**`include/SipiConf.h`** — add private fields (after `max_pixel_limit` at line 67):
```cpp
unsigned rate_limit_window{60};       // seconds
size_t rate_limit_max_pixels{0};      // 0 = disabled
std::string rate_limit_client_header; // optional custom header name
```
Plus getters/setters following existing pattern.

**`src/SipiConf.cpp`** — parse from Lua config (after `max_pixel_limit` parsing):
- `rate_limit_window` (integer, default 60)
- `rate_limit_max_pixels` (integer, default 0)
- `rate_limit_client_header` (string, default "")

**`src/sipi.cpp`** — CLI options (after `--max-pixel-limit`, ~line 619):
- `--rate-limit-max-pixels` / `SIPI_RATE_LIMIT_MAX_PIXELS`
- `--rate-limit-window` / `SIPI_RATE_LIMIT_WINDOW`
- `--rate-limit-client-header` / `SIPI_RATE_LIMIT_CLIENT_HEADER`

Wiring block (after `max_pixel_limit` wiring, ~line 1369): CLI/env overrides config, create rate limiter on server.

**`config/sipi.config.lua`** — add after `max_pixel_limit`:
```lua
rate_limit_max_pixels = 0,
rate_limit_window = 60,
rate_limit_client_header = "",
```

### 6. Metrics

**`include/SipiMetrics.h`** — add counter:
```cpp
prometheus::Counter &rate_limited_total;
```

**`src/SipiMetrics.cpp`** — initialize as `sipi_rate_limited_total`.

### 7. Build

**`CMakeLists.txt`** — add `src/SipiRateLimiter.cpp` + `include/SipiRateLimiter.h` after `SipiConf.cpp` (line ~435).

## Files to modify

| File | Change |
|------|--------|
| `include/SipiRateLimiter.h` | **New** — rate limiter class |
| `src/SipiRateLimiter.cpp` | **New** — implementation |
| `src/SipiHttpServer.cpp` | `resolve_client_id` helper, rate limit check in `serve_iiif`, `TOO_MANY_REQUESTS` in `send_error` switch |
| `src/SipiHttpServer.hpp` | Rate limiter member + client header member + accessors |
| `include/SipiConf.h` | 3 config fields + getters/setters |
| `src/SipiConf.cpp` | Parse 3 fields from Lua config |
| `src/sipi.cpp` | 3 CLI options + wiring block |
| `config/sipi.config.lua` | 3 config entries with docs |
| `include/SipiMetrics.h` | `rate_limited_total` counter |
| `src/SipiMetrics.cpp` | Counter initialization |
| `CMakeLists.txt` | Add new source files |

## Design Notes

### Thread Safety
The rate limiter uses a single `std::mutex`. With 8 worker threads and a short critical section (prune vector, sum, push), contention will be negligible at typical request rates. If profiling shows contention, a future optimization could shard by client ID hash.

### Memory Bounds
- Per-client lazy cleanup on every check (prune expired records)
- Full map sweep every 1000 checks to evict stale client entries
- Records only live for `window_sec` (default 60s), so memory is naturally bounded

### Retry-After Header
The `Retry-After` value is computed as seconds until the oldest record in the client's window expires: `ceil(oldest.timestamp + window - now)`. Minimum 1 second.

## Example Deployment Config

```lua
-- Rate limit: 50 megapixels per 60 second window per client
rate_limit_max_pixels = 50000000,
rate_limit_window = 60,
rate_limit_client_header = "",
```

Or via environment:
```bash
SIPI_RATE_LIMIT_MAX_PIXELS=50000000
SIPI_RATE_LIMIT_WINDOW=60
```

For context: a typical IIIF tile request is ~256x256 = 65,536 pixels. A 50MP budget allows ~762 tile requests per minute — more than enough for normal browsing. A full-resolution 3888px-wide render of a large image is ~3888x5000 = ~19.4MP, so a bot could only get ~2.5 full-res renders per minute before being rate-limited.
