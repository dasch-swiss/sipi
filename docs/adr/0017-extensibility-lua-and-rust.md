---
status: accepted
---

# Extensibility: runtime scripting for request-shaping (no compile-time toolchain), TOML for config

## Context

SIPI is a **general-purpose IIIF server**. Its request-shaping policy — the
`pre_flight` / `file_pre_flight` auth hooks and configured custom routes — is
written as scripts, executed by an embedded runtime. The strangler rewrite
([ADR-0013](0013-shttps-as-internal-module.md)) moves the HTTP shell to Rust over
the C++ image engine via a narrow C FFI seam, which raises the question of how
extensions are written going forward: a compile-time Rust extension point (a
trait linked into the binary), or a runtime-loaded script.

Two facts decide it:

1. **Only DaSCH can compile SIPI.** Kakadu (the JP2 codec) is license-gated, so
   no third party can build SIPI from source. A compile-time extension mechanism
   has no external audience — the only party who could compile an implementation
   is the maintainer — and even internally it forces a full rebuild + redeploy of
   a slow, licensed binary for every policy change.
2. **A non-technical user must be able to extend SIPI without a toolchain.**
   Changing request-shaping should not require a compiler, a build environment,
   or a link step. It should be editing a script the running server loads.

## Decision

**Request-shaping extensions (auth hooks, custom routes) are authored as
runtime-loaded scripts, requiring no compilation or toolchain from the author.**
Config is a separate, declarative concern (**TOML**), not scripting.

- **Lua is the current scripting implementation** and is fully supported. The
  seam carries the full request as an opaque `RequestContext`, so scripts have
  every `server.*` field available. Existing Lua scripts run unchanged.
- **A more approachable scripting language may succeed Lua.** The selection
  criterion is the goal above: a non-technical user can write and change an
  extension **without a compilation/toolchain setup**, by editing a script the
  server loads at runtime. **Roc (roc-lang) is the leading candidate** — its
  platform/application split (a Rust/Zig/C "platform" host exposes effects; the
  "application" is the user's code) maps cleanly onto "SIPI host exposes
  `http`/`respond`/… effects + auth logic as the script." **Roc is a candidate,
  not a commitment:** its maturity and compilation/distribution story must be
  checked against the no-toolchain goal; if it does not meet that bar, another
  approachable scripting language is chosen instead.
- **LLM-assisted authoring is assumed.** Authors write these scripts with LLM
  support, so the language need not be maximally familiar; a clean host-effect
  boundary, safety, and ergonomics matter more than a large existing user base.
  This **widens** the candidate set but does **not** relax the no-toolchain
  constraint — LLM help with authoring is not a substitute for the extension
  being runtime-loaded.

## What we reject

- **A compile-time Rust extension point** (a `pre_flight` / route trait linked
  into the binary) as the user-facing extension mechanism, and the
  **SIPI-as-library** end-state that goes with it. It fails both drivers above:
  no external party can compile it, and it forces a rebuild/redeploy for every
  change. A Rust trait may still exist *internally* as a detail of how the host
  dispatches to scripts, but it is not how users extend SIPI.
- **Forced migration off Lua.** No user is moved off Lua before a stable,
  prod-proven replacement exists, with a documented migration path.

## Consequences

- **`sipi_run_lua_route` is the Lua-route serving path** for as long as Lua is
  the scripting language. A successor scripting language adds its own serving path
  beside it (or replaces it) under the no-forced-migration constraint.
- **Server-side upload is a SIPI capability served as a Lua route** (the
  `/api/upload` route), not hardcoded into the Rust shell. A deployment that
  routes ingest elsewhere simply does not configure the route (e.g. DSP uses
  dsp-ingest). Whether a deployment enables it is configuration, not a capability
  SIPI drops.
- **The seam carries the full request as an opaque `RequestContext`** (not a
  narrowed struct), so any field a script might read stays resolvable — the same
  shape a successor scripting host would consume.
- **TOML config is the declarative config surface** (serde + toml). It is config,
  not the extension mechanism, and is independent of the scripting-language
  question.
- **The script → dsp-api distributed-tracing gap is independent of the language
  choice.** Whatever the script language, the script's outbound call to dsp-api
  goes through a host HTTP binding; connecting it to the trace requires the host
  to inject `traceparent` across the seam. The language choice does not affect it.
- **A pure-Rust Lua runtime (mlua) is low priority.** If a different scripting
  language replaces Lua, reimplementing Lua in mlua is moot; while Lua persists,
  mlua only removes the C++ `LuaServer` dependency. Not a priority either way.

Refines decision #9 of the strangler plan; extends [ADR-0013](0013-shttps-as-internal-module.md).
