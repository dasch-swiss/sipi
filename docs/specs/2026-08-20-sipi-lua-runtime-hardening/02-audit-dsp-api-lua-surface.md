---
title: "audit: dsp-api SIPI Lua surface vs the hardened Rust runtime"
type: refactor
date: 2026-08-20
author: "Ivan Subotic"
status: final
repository: dasch-swiss/dsp-api
linear: DEV-7013
---

# audit: dsp-api SIPI Lua surface vs the hardened Rust runtime

Production-safety gate for the plan in `01-refactor-sipi-lua-runtime-rust-mlua-plan.md` (Phase 0). Audited: dsp-api @ `36dd9530f`, all 25 `.lua` files under `modules/sipi/` plus the test-resource configs, image packaging (`modules/sipi/BUILD.bazel`), and deployment wiring. Production runs `daschswiss/knora-sipi` (docker-compose → `sipi.docker-config.lua` → initscript `sipi.init.lua`); ops-deploy confirms the image.

## Production-live script closure (9 files)

`sipi.init.lua`, `file_specific_folder_util.lua`, `authentication.lua`, `log_util.lua`, `util.lua`, `env.lua`, `send_response.lua`, `strings.lua`, `basexx.lua`. Everything else is test-only or dead (`cache.lua`, `exit.lua`, `debug.lua` are unrouted in every config; `json.lua` is never required; `delete_temp_file.lua` / `upload_without_processing.lua` are routed but were deleted in dsp-api `b74a33c5d`; `test_knora_session_cookie.lua` calls an undefined `get_session_id` and 500s today).

## Delta list (hardened runtime vs dsp-api reality)

| # | Finding | Severity | Resolution in plan |
|---|---|---|---|
| D1 | `os.date("!%Y-%m-%dT%H:%M:%S")` at `log_util.lua:5` runs inside `log()` — called 44× in the live closure, including the first statements of `pre_flight` on **every IIIF request**. An os shim of only getenv+clock = total outage on first request. | breaks-prod | os shim = `getenv` + `clock` + `date` (UTC-format subset); e2e regression |
| D6 | `server.header` lookups are exact-match **lowercase** (`authentication.lua:135,141,148-149` — `authorization`, `cookie`). Original-case header keys would silently degrade every request to anonymous. | breaks-prod (guard rail) | `server.header` keys pinned lowercase; regression test. Only `server.cookies` moves to original case (DEV-6119) — dsp-api never uses it (parses the raw Cookie header itself, case-insensitively) |
| D8 | `send_response.lua:40` uses integer division `//` — syntax error on Lua 5.1/5.2/LuaJIT; the file is in the initscript require chain. | breaks-prod if wrong Lua | Runtime is mlua `lua53` against `@lua` 5.3.6 — satisfied by construction |
| D9 | The entire inter-module contract is bare globals (`log`, `send_error`, `find_file`, `basexx`, `pre_flight` itself, …). Read-only `_G`, per-module `_ENV`, or strict-global guards break everything. | breaks-prod if sandboxed | `_G` stays writable and shared between initscript and route chunk within a request VM; per-request isolation is *across* requests only |
| D11 | `os.getenv` is load-bearing: `KNORA_WEBAPI_KNORA_API_EXTERNAL_HOST/_PORT` (hard 500 if unset), `SIPI_WEBAPI_HOSTNAME/_PORT`. | breaks-prod if narrowed | `getenv` unrestricted in the shim |
| D12 | Prod config routes two non-existent scripts (`delete_temp_file.lua` in all 6 configs; `upload_without_processing.lua` in test resources). Boot-time route-target validation would brick the production config. | breaks-prod if strict | Missing route script stays a request-time 404 (today's semantics); dsp-api cleanup recommended, not required |
| D4 | `server.http(…, 5000)` at `sipi.init.lua:41` is on the critical path of every authorized IIIF request; 5000 is connect-only today, total under reqwest. A slow dsp-api `/admin/files` now becomes a visible failure instead of a hang (script maps it to `"allow", "file_does_not_exist"`). | behavior-change | Validate dsp-api `/admin/files` p99 ≪ 5 s before cutover (Phase 0); net improvement, changelogged |
| D5 | `decode_jwt` validating `exp`: dsp-api already self-checks `exp` against `server.systime()` (`authentication.lua:65-72`) and never relies on expired tokens passing; only the 401 message shape changes. No script mints JWTs; `os.time` unused (D13 verified negative). | behavior-change (low) | Validate `exp` (HS256 pinned); message-shape change changelogged |
| D2 | `io.popen` at `util.lua:86-88` (`file_checksum`) — dead function, zero call sites; reference is inside a function body so `require "util"` still loads under the whitelist. | benign | dsp-api cleanup: delete `util.lua:82-95` |
| D3 | `config.adminuser` (`cache.lua:4`, `exit.lua:4`), `server.shutdown` (`exit.lua:15`), nonexistent `cache` global — all in unrouted, already-broken scripts. `config.password`: zero occurrences. | benign | Divergences confirmed safe; dsp-api cleanup: delete `cache.lua`, `exit.lua`, `debug.lua`, `json.lua` |
| D7 | `server.cookies`: zero occurrences in dsp-api. | benign | DEV-6119 fix invisible to dsp-api (depends on D6 holding) |
| D10 | `server.fs.chdir`: zero occurrences. Used: `fs.exists` (3), `fs.mkdir(path, 511)` (2 — note decimal mode arg semantics must be preserved). | benign | chdir drop confirmed safe |
| D14 | `SipiImage`, `sqlite`, `helper.filename_hash`: zero occurrences (watermark is data-driven via the preflight return, `sipi.init.lua:137`). | benign | Bindings still provided (SIPI is general-purpose, ADR-0017) |

## Stdlib usage (exhaustive over the removed surface)

The **only** hits across all 25 files: `os.date` (D1), `os.getenv` (D11 + 3 dead `SIPI_EXTERNAL_*` reads), `io.popen` (dead, D2), and json.lua-internal `rawget`/`next`/`select` (file never loaded). **Zero**: `debug.*`, `coroutine.*`, `dofile`, `loadfile`, `load(string)`, `collectgarbage`, explicit `package.*`, bitwise ops/`bit32`, `setmetatable`, `pcall`/`xpcall`, bare `print`, `os.time/execute/remove/rename/exit`, `io.open/read/write/lines`, `string.dump`, `string.pack`, `utf8.*`. `basexx.lua` needs only string/table/math. All 18 `require` calls are literal dot-free names resolving inside the script dir — compatible with the restricted loader.

## `config.*` reads in the live closure

`imgroot`, `prefix_as_path` (`file_specific_folder_util.lua:34-37`), `thumb_size` (`sipi.init.lua:139`), `knora_path`/`knora_port` (`util.lua:63,76`, fallbacks when `SIPI_WEBAPI_*` unset). `hostname`/`port` appear only in dead functions (`util.lua:38,51`); `docroot` only in the test-config-routed `admin_upload.lua:21`; `adminuser` only in unrouted dead scripts; `password`: zero occurrences. This is the inventory behind the plan's `bindings/config.rs` checkbox.

## Cutover pre-flight checklist (carried into the plan)

1. `os.date` in the shim (D1) — plan updated.
2. `server.header` lowercase pin + regression test (D6) — plan updated.
3. Lua 5.3 (D8) — satisfied by `@lua` 5.3.6.
4. Writable shared `_G` per request VM (D9) — plan updated.
5. Live prod surface to keep intact: `server.systime`, `server.fs.exists`, `server.fs.mkdir`, `server.http`, `server.decode_jwt`, `server.json_to_table`, `server.table_to_json`, `server.print`, `server.sendHeader`, `server.sendStatus`, `server.log`, `server.loglevel.*`, `server.header`, `server.request`.
6. Missing route scripts stay request-time 404s (D12) — plan updated.
7. dsp-api cleanup PR (recommended, non-blocking): delete `cache.lua`, `exit.lua`, `debug.lua`, `json.lua`, `util.lua:82-95`; remove the 4 dead route entries; fix or drop `test_knora_session_cookie.lua`.
8. Validate dsp-api `/admin/files` p99 before the total-timeout semantics ship (D4).
