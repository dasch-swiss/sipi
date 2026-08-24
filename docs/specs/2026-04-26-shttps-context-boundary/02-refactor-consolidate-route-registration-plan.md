---
date: 2026-04-26
type: refactor
status: implemented
repositories:
  - name: sipi
related-context:
  - sipi/CONTEXT-MAP.md
  - sipi/CONTEXT.md
  - sipi/shttps/CONTEXT.md
related-plans:
  - 01-feat-shttps-include-gate-plan.md
commit-prefix: refactor
---

# refactor: Consolidate route registration in SipiHttpServer

## Problem

`sipi/shttps/CONTEXT.md` declares shttps **route-blind**: the consumer (SIPI) chooses every URL path; the framework only ships handlers and the `add_route` mechanism. Today SIPI's route registration is split across two files for no domain reason:

- `src/SipiHttpServer.cpp:2258-2262` registers `/health`, `/metrics`, `/favicon.ico`, and `GET|HEAD "/"` (IIIF) inside `SipiHttpServer::run()`.
- `src/sipi.cpp:1688-1693` registers `GET|POST {wwwroute} → shttps::file_handler` inside `main()`, passing a local `std::pair<std::string, std::string> filehandler_info` as the handler's per-route `handler_data` (4th arg of `add_route`).

The split makes "what routes does SIPI expose?" a multi-grep question and lets unrelated code in `main()` reach into `add_route`. Consolidating yields one grep target, removes a cross-file lifetime dependency on a local variable, and aligns the code with the CONTEXT-declared boundary.

## Acceptance criteria

- All `add_route(...)` calls in SIPI live in `SipiHttpServer::run()`.
- `src/sipi.cpp` contains zero `add_route` calls. `grep -n "add_route" src/sipi.cpp` returns nothing.
- `grep -cE '\badd_route\(' src/SipiHttpServer.cpp` returns 7 (5 fixed routes + 2 conditional `wwwroute` routes), all inside `SipiHttpServer::run()`.
- The `wwwroute` and `docroot` strings, plus the `(wwwroute, docroot)` pair passed as `shttps::file_handler`'s `handler_data`, are owned by `SipiHttpServer` (members), so their lifetime is tied to the server object rather than a `main()` frame local.
- Behaviour is unchanged: when `wwwroute` and `docroot` are both non-empty, all seven routes are registered (5 fixed + `GET wwwroute` + `POST wwwroute`); when either is empty, only the five fixed routes (`/health`, `/metrics`, `/favicon.ico`, `GET /`, `HEAD /`) are registered.
- Existing Hurl tests (`just hurl-test`) and Rust e2e tests (`just rust-test-e2e`) pass without modification.
- The shttps→sipi include gate from plan 01 still passes (this refactor does not introduce any new `Sipi::` reference inside `shttps/`).

## Files

### Modify: `sipi/src/SipiHttpServer.hpp`

Add three protected members and two public setter/getter pairs.

Protected members, inserted in the existing protected block at the head of the class. Place `_docroot` immediately before `_imgroot`, `_wwwroute` immediately after `_salsah_prefix`, and `_filehandler_info` directly after `_wwwroute`:

```cpp
// Document root + URL prefix for shttps::file_handler. Empty = route disabled.
std::string _docroot;
std::string _wwwroute;
// Stable storage for the (wwwroute, docroot) pair passed to shttps::file_handler
// as its handler_data argument (4th arg of add_route). Must outlive the run loop.
std::pair<std::string, std::string> _filehandler_info;
```

Public setter/getter pairs, placed alongside the existing `imgroot()` and `salsah_prefix()` pairs (around line 93-99). Mirror the existing pattern: setter takes `const std::string &`, getter returns `std::string` by value and is non-const for parity with `imgroot()`:

```cpp
void docroot(const std::string &docroot_p) { _docroot = docroot_p; }
std::string docroot() { return _docroot; }

void wwwroute(const std::string &wwwroute_p) { _wwwroute = wwwroute_p; }
std::string wwwroute() { return _wwwroute; }
```

### Modify: `sipi/src/SipiHttpServer.cpp`

Replace the route block at lines 2258-2262 inside `run()` with a single consolidated block that includes the `wwwroute` route conditionally:

```cpp
add_route(Connection::GET, "/health", health_handler);
add_route(Connection::GET, "/metrics", metrics_handler);
add_route(Connection::GET, "/favicon.ico", favicon_handler);
add_route(Connection::GET, "/", iiif_handler);
add_route(Connection::HEAD, "/", iiif_handler);

if (!_wwwroute.empty() && !_docroot.empty()) {
  _filehandler_info = { _wwwroute, _docroot };
  add_route(Connection::GET,  _wwwroute, shttps::file_handler, &_filehandler_info);
  add_route(Connection::POST, _wwwroute, shttps::file_handler, &_filehandler_info);
}
```

The conditional preserves the original gate: register the `wwwroute` route only when *both* `wwwroute` and `docroot` are configured (De Morgan equivalent of the current `!(wwwroute.empty() || docroot.empty())` in `sipi.cpp`).

### Modify: `sipi/src/sipi.cpp`

Replace lines 1679-1693 (the `// now we set the routes for the normal HTTP server file handling` comment block, the local `docroot`/`wwwroute`/`filehandler_info` variables, and the conditional `add_route` block) with two setter calls before `server.run()`:

```cpp
server.docroot(sipiConf.getDocRoot());
server.wwwroute(sipiConf.getWWWRoute());
```

The setter names are self-documenting; no replacement comment is added (and the deleted `// now we set the routes …` line would be stale — the setter calls don't register any routes).

No base-class setter call is needed: `shttps::Server` exposes no `docroot(...)` member (`grep docroot shttps/Server.h` is empty), and `shttps::file_handler` (`shttps/Server.cpp:250-266`) reads `docroot` and `route` exclusively from its per-route `handler_data` argument — the `std::pair<std::string, std::string>*` passed as the 4th argument to `add_route`. The Lua-exposed `server.docroot` table entry is set inside `file_handler` from that same pair, not from any `Server` member.

## Verification

Build and test locally:

```
just nix-build               # unit tests in the Nix sandbox
just hurl-test               # HTTP contract tests
just rust-test-e2e           # end-to-end against ./result/bin/sipi
just shttps-context-check    # plan 01: still green
```

Smoke verification of the consolidated routes:

```
nix run .#default -- --config config/sipi.localdev-config.lua &
curl -fsS http://localhost:1024/health
curl -fsS http://localhost:1024/metrics | head -3
curl -fsSI http://localhost:1024/favicon.ico
# IIIF info.json on a known test image (pick from test/_test_data)
curl -fsS http://localhost:1024/{prefix}/{id}/info.json
```

Empty-`wwwroute` smoke check (covers the conditional's false branch — server starts cleanly with no `wwwroute` route registered):

```
# Stamp out wwwroute in a config copy.
sed 's/^\(\s*wwwroute\s*=\s*\).*/\1"",/' config/sipi.localdev-config.lua > /tmp/sipi-no-wwwroute.lua
nix run .#default -- --config /tmp/sipi-no-wwwroute.lua &
curl -fsS http://localhost:1024/health    # 200 — server up, fixed routes still registered
```

Greps that should hold after the change:

```
grep -cE '\badd_route\(' src/sipi.cpp                  # 0
grep -cE '\badd_route\(' src/SipiHttpServer.cpp        # 7, all inside SipiHttpServer::run()
```

## Out of scope

- Removing `shttps::file_handler` from shttps or replacing it. The `wwwroute → file_handler` registration stays; only its location changes.
- The metrics-hook fix for the known `SipiMetrics` leak in `shttps/Server.cpp` (separate plan).
- Re-homing `Hash`, `Parsing`, `Error`, `Global`, `makeunique` from shttps into SIPI (separate plan).
- Changes to the IIIF route URL space (still mounted at `"/"`) or the `wwwroute` config knob.

## Commit shape

One commit, prefix `refactor:`. PR title: `refactor: consolidate route registration in SipiHttpServer`. Conventional Commit prefix is `refactor:` because there is no behaviour change and no public-API change visible to operators or HTTP clients.
