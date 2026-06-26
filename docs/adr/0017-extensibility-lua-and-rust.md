---
status: accepted
---

# Extensibility: native-first (Rust + TOML), Lua deprecated once native alternatives stabilize

> **Amended 2026-06-26.** The original decision (below, 2026-06-22) was that Lua
> and Rust would be *permanently* coexisting first-class paths and SIPI would
> never deprecate Lua. That is superseded by the staged native-first plan in the
> Decision section: introduce a native alternative to every Lua use, mark it
> experimental until prod-validated, stabilize, then deprecate Lua. The original
> rationale is retained as the *constraint on how* Lua is deprecated (only behind
> a stable native replacement), not as a permanence commitment.

SIPI is a **general-purpose IIIF server**. Its request-shaping policy — the
`pre_flight` / `file_pre_flight` auth hooks and configured custom routes — has
historically been written in Lua, executed by the C++ `LuaServer`. The strangler
rewrite ([ADR-0013](0013-shttps-as-internal-module.md)) moves the HTTP shell to
Rust over the C++ image engine via a narrow C FFI seam, which raises the
question: does Lua stay, and how does a Rust-first user extend SIPI without it?

## Decision

SIPI is moving to **native (Rust + TOML) extensibility as the primary path**. We
introduce a native alternative to **everything Lua is used for today** — config,
the `pre_flight` / `file_pre_flight` auth hooks, and custom routes — and will
deprecate Lua once those alternatives are stable. Because the native surfaces are
new and *will* change, the migration is **staged, one surface at a time**:

1. **Introduce** a native alternative alongside the existing Lua path — TOML
   config beside Lua config; a Rust `pre_flight` / `file_pre_flight` trait beside
   the Lua hooks; Rust route handlers beside Lua routes (Phase T).
2. **Mark it experimental.** The native surface (the TOML schema, the trait
   signatures) is **not yet API-stable and may change without the usual
   compatibility guarantees** until it is validated in production.
3. **Validate in production, then stabilize** — once a native alternative has
   proven itself in real deployments, drop the experimental designation and
   commit to its stability.
4. **Deprecate Lua for that surface** — but only once a stable, prod-proven
   native replacement exists, so no user is pushed off Lua before there is
   somewhere stable to go.

While a native alternative is experimental, **both paths coexist and Lua stays
fully functional**: the Rust trait's default implementation wraps the Lua FFI,
and the seam carries the full request as an opaque `RequestContext`, so existing
Lua scripts run unchanged. There is **no forced migration during the experimental
phase**. The end state is **Rust/TOML-native primary with Lua deprecated** — a
multi-year arc, executed surface by surface.

This **supersedes the original decision in this ADR** (2026-06-22) that Lua and
Rust were "two coexisting, permanently first-class" paths with Lua never
deprecated. That decision's rationale — SIPI is a general-purpose server, the
choice is the operator's, no user is forced to migrate — is **retained as the
constraint on *how* Lua is deprecated** (gradually, surface by surface, only
behind a stable native replacement), not as a commitment that Lua is permanent.

**Experimental native surfaces today** (subject to change until prod-validated):

- **TOML config** — `--config *.toml` (M5). The schema may change.
- **Rust `pre_flight` / `file_pre_flight` and route traits** — Phase T. The trait
  signatures may change.

## Consequences

- **`sipi_run_lua_route` is the Lua-route serving path for the transition.** The
  Phase C cutover implements it (Rust owns route dispatch once the transport's
  `script_handler` is deleted); it is not a throwaway bridge — it serves Lua
  routes for as long as Lua is supported. The Phase T Rust route trait runs in
  parallel as the experimental native alternative; `sipi_run_lua_route` is
  retired only once Lua routes are deprecated behind a stabilized route trait.
- **Server-side upload is a retained SIPI capability, served as a Lua route.**
  Multipart upload (`POST`, today the `/api/upload` Lua route) stays a SIPI
  capability through `sipi_run_lua_route` — it is **not** hardcoded into the Rust
  shell, and removing it is not a SIPI decision. A deployment that routes ingest
  elsewhere (e.g. DSP, which uses dsp-ingest and disallows upload over SIPI)
  simply does not configure the route; that is the operator's config choice, not
  a capability SIPI drops. This resolves the strangler plan's open question on
  multipart upload (retain vs cede): SIPI retains it as a Lua route; whether a
  given deployment enables it is configuration.
- **The seam carries the full request as an opaque `RequestContext`** (not a
  narrowed struct), so any `server.*` field a Lua script might read stays
  resolvable — load-bearing for the no-forced-migration guarantee for existing
  Lua users.
- **The D+ Lua → mlua rewrite is lower priority.** Reimplementing the Lua
  runtime in pure-Rust mlua only removes the C++ `LuaServer` dependency; it is
  not required for the native extension story, which the trait + TOML config
  deliver without it. Since the end state deprecates Lua, the C++ Lua runtime is
  transitional; mlua matters only if the Lua deprecation tail runs long enough to
  want a pure-Rust Lua in the interim. Lower priority either way.
- **Docs mark the native alternatives experimental + state the long-term plan.**
  Each native surface (TOML config, the Phase T traits) is documented as
  experimental — may change until prod-validated — with a pointer to this ADR's
  lifecycle. Lua is documented as supported-but-on-the-path-to-deprecation, not
  removed.
- **`PreflightDecision` is modelled as a clean value type** (a permission-type
  enum + an open key/value map), so the C++ `build_preflight` return and the
  future Rust trait return are a 1:1 port — no seam change is needed to add the
  trait later.

Refines decision #9 of the strangler plan; extends ADR-0013. The as-built trait
API (`trait PreFlight`, `trait RouteHandler`, `ServerConfig::with_preflight`) is
specified and amended into this record when Phase T lands.
