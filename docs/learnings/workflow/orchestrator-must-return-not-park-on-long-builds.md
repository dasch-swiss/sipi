---
title: "A supervised subagent must return at a long build, not park on it — the parent gates and respawns from on-disk state"
date: 2026-09-06
category: "workflow"
component: "workflow-orchestration"
module: "eng three-tier work-orchestration (session → opus orchestrator → sonnet workers)"
problem_type: "orchestration-liveness"
severity: "medium"
symptoms: "During a three-tier orchestration the opus orchestrator went silent for long stretches (rounds 2, 7, and the T1 rounds). It had not crashed — it was blocking on a multi-minute `just bazel-build`/test, so it never returned its status report, and the session couldn't gate, inject a blocker resolution, or tell alive-but-busy from dead."
root_cause: "The orchestrator treated a long-running build/test as something to wait on inside the round, rather than as a point to return control. A blocked agent that holds its turn open is invisible to the parent and cannot be steered."
tags: [orchestration, subagents, work-orchestrate, parking, liveness, respawn, on-disk-state, ListAgents, TaskStop, fable, opus, sonnet, sipi]
related: []
issue: "DEV-7131"
---

# A supervised subagent must return at a long build, not park on it

From the SIPI security-hardening wave-2 orchestration (DEV-7131): a three-tier design —
interactive session (Fable) → an opus `work-orchestrator` subagent → sonnet
`implementation-worker` subagents. Several times the orchestrator "parked": it kicked off a
long `just bazel-build` or test suite and sat inside its turn waiting for it to finish, instead
of returning a status report. The session had no way to distinguish a healthy long build from a
hung agent, could not advance, and could not hand the orchestrator a decision it was waiting on.

## Problem

- Rounds 2, 7, and the T1 rounds: the orchestrator produced no output for long stretches.
- `TaskOutput` on the agent was unhelpful/unsafe; the useful signal was `ListAgents` (is it still
  alive?), which showed **alive but busy** — parked on a build.
- Because the agent never returned a structured report, the session couldn't run its Phase-4
  escalation loop (parse `run_status`, resolve blockers, respawn). Everything stalled behind one
  build.
- A related worker-level instance: a round-7 worker hung ~1h on an unbounded e2e test, which also
  never returned — see the companion testing learning on bounding timeout e2e.

## Root Cause

The orchestrator's brief did not make "a long build is a *return* boundary" explicit, so the agent
did the locally-natural thing: wait for the build in-turn. A supervised agent that blocks on
long-running work holds its turn open, which makes it invisible to the parent (no report to parse)
and unsteerable (no way to inject a resolution mid-wait). Liveness ≠ progress.

## Solution

- **Recover by respawn, never resume.** Diagnose with `ListAgents` (not `TaskOutput`); `TaskStop`
  the parked agent; spawn a *fresh* orchestrator that reconstructs state from on-disk artifacts
  (the plan journal + `[x]`/`[ ]` checkboxes + `git log <base>..HEAD`). No orchestrator is ever
  resumed; every continuation is a fresh spawn over durable state.
- **Fix the brief so it can't recur.** Add an explicit instruction: *never park on a build —
  RETURN THE REPORT.* When a build/test will take more than a couple of minutes, or when the round's
  work is done, return the structured status report (`run_status: done | partial | blocked`,
  commits landed, `context_health`, blockers, `next`) and let the session gate and respawn. Return
  `partial` rather than holding the turn open.

## Prevention

- **Make "return, don't block" the default at any multi-minute wait.** For a supervised agent, a
  long build or test suite is a point to hand control back with a `partial` report, not a point to
  wait. The parent can always respawn; it cannot steer a parked child.
- **Design every tier to reconstruct from on-disk state, so returning early is free.** Journal +
  checkboxes + `git log` are the source of truth; the agent's in-memory turn is disposable. This is
  what makes "return `partial` and respawn" cheap and lossless — and why *no resume* is safe.
- **Use `ListAgents` to tell alive-but-busy from dead; never `TaskOutput` to probe a supervised
  agent.** Then `TaskStop` + respawn — there is exactly one recovery mechanism, not two.
- **Watch for the same failure one tier down.** A worker that blocks on an unbounded test parks
  just like an orchestrator that blocks on a build; bound the work (see the timeout-e2e learning) and
  keep worker chunks short enough to return.

## References

- eng `work-orchestrating` skill and `work-orchestrator` agent (the three-tier design).
- Companion learning: [a timeout/slowloris e2e must be bounded](../testing/timeout-e2e-must-be-bounded.md)
  (the worker-level instance of the same "blocks and never returns" failure).
