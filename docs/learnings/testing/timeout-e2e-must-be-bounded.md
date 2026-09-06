---
title: "A timeout/slowloris e2e test must be bounded on server, client, teardown, and harness — or it hangs the runner and leaks servers"
date: 2026-09-06
category: "testing"
component: "testing"
module: "test/e2e timeout/slowloris tests (SIPI_REQUEST_TIMEOUT / SIPI_BODY_READ_TIMEOUT)"
problem_type: "e2e-hang-and-process-leak"
severity: "medium"
symptoms: "An e2e test exercising a request/body-read timeout (slow-header / slowloris / trickle-body) ran for ~1 hour without failing, blocking the whole orchestration round. Afterwards, orphaned `sipi server --config` processes were still running on the machine and had to be killed with `pkill -f 'sipi server --config'`."
root_cause: "A test that drives a timeout behaviour was bounded on only some axes. If any of the server timeout, the client timeout, the teardown, or the Bazel test timeout is missing, the slow client and a long production timeout combine so nothing ever returns — the worker parks and leaks the server it spawned."
tags: [e2e, timeout, slowloris, slow-header, test-timeout, process-leak, sipi, reqwest, teardown, bazel-test-timeout, SIPI_REQUEST_TIMEOUT, SIPI_BODY_READ_TIMEOUT]
related:
  - ../debugging/parallel-e2e-shared-state-and-primary-evidence.md
issue: "DEV-7131"
---

# A timeout/slowloris e2e test must be bounded on every axis

From SIPI security-hardening wave-2 (DEV-7131), Phase 6b, which added request- and
body-read-timeout behaviour (`SIPI_REQUEST_TIMEOUT`, `SIPI_BODY_READ_TIMEOUT`) plus slow-client
handling. The e2e tests that exercise those paths are a special hazard: a test whose *whole point*
is that something takes a long time will hang the runner unless it is bounded on **all four**
axes. One such test ran ~1h during an orchestration round without ever failing, stalled the round,
and left orphaned `sipi server --config` processes behind.

## Problem

- A slowloris/trickle-body test opens a connection and sends data slowly (or never completes the
  request), expecting the server to time out and close.
- If the server is started with its **production** timeout default (e.g. `SIPI_REQUEST_TIMEOUT=60`,
  or longer), and the client trickles indefinitely, and there is no client-side timeout and no
  harness cap, the test simply waits — for an hour, until something external kills it.
- Worse, the hung test never runs its teardown, so the `sipi server` subprocess it spawned is
  **leaked**: `pkill -f 'sipi server --config'` found several orphaned servers still running after
  the round.

## Root Cause

The behaviour under test is "a slow client eventually gets cut off." That only terminates if the
cutoff is short. A production timeout is deliberately generous; used verbatim in a test it becomes
a near-infinite wait. And a Rust e2e that blocks in a `reqwest` call with no client timeout, no
`Drop` that kills the server, and no Bazel `--test_timeout`, has no other actor that will end it.

## Solution / the pattern

Bound a timeout/slowloris e2e on **every** axis:

1. **Short server timeout via env.** Start the server under test with a *small*
   `SIPI_REQUEST_TIMEOUT` / `SIPI_BODY_READ_TIMEOUT` (e.g. 1–2s) so the server closes the slow
   connection fast — never the production default.
2. **Client-side timeout.** Build the `reqwest` client with an explicit `.timeout(...)` so the
   client gives up even if the server misbehaves.
3. **Deterministic teardown.** Kill the spawned `sipi server` in a `Drop`/guard so a failing or
   hung test never leaks the process (mirror the `start()`/`Drop` pattern the other e2e harnesses
   use).
4. **Harness cap.** Give the Bazel target a bounded `--test_timeout` / `size` so a runaway is
   killed by the harness as a last resort.

Missing any one of these reintroduces the hang. (1) is the one most easily forgotten because the
server "works" — it just uses a timeout meant for production.

## Prevention

- **Never run a timeout test against a production-length timeout.** Override it low via env in the
  test's server; the value is the whole point of the test.
- **Every e2e that spawns a server needs `Drop`-based teardown**, so a hang or panic can't leak the
  process. A leaked `sipi server` also holds ports/caches and poisons later tests (see the companion
  learning on parallel-e2e shared state).
- **Bound timeout/slowloris targets at the harness too** (`--test_timeout`/size), so the worst case
  is a fast harness kill, not an hour of wall-clock.
- **A hung test is invisible up the chain.** In a supervised/orchestrated run it parks the worker
  exactly like an orchestrator parking on a build — bounding the test is also what keeps the worker
  returnable (see the workflow learning on returning instead of parking).

## References

- Wave-2 Phase 6b timeouts: `SIPI_REQUEST_TIMEOUT`, `SIPI_BODY_READ_TIMEOUT` (see the server config
  and ADR-0022 admission context).
- Companion learnings: [parallel-e2e shared state](../debugging/parallel-e2e-shared-state-and-primary-evidence.md),
  [a supervised subagent must return, not park](../workflow/orchestrator-must-return-not-park-on-long-builds.md).
