---
status: accepted
---

# Request-shaping extensibility: Lua scripts and Rust traits, both first-class

SIPI is a **general-purpose IIIF server**. Its request-shaping policy — the
`pre_flight` / `file_pre_flight` auth hooks and configured custom routes — has
historically been written in Lua, executed by the C++ `LuaServer`. The strangler
rewrite ([ADR-0013](0013-shttps-as-internal-module.md)) moves the HTTP shell to
Rust over the C++ image engine via a narrow C FFI seam, which raises the
question: does Lua stay, and how does a Rust-first user extend SIPI without it?

## Decision

SIPI supports **two coexisting, permanently first-class** ways to supply
request-shaping policy:

1. **Lua scripts** — run by the C++ `LuaServer` behind the FFI seam
   (`sipi_preflight` / `sipi_file_preflight` / `sipi_run_lua_route`). This stays
   a supported path. SIPI does **not** deprecate or remove Lua.
2. **Rust custom extensions** — `pre_flight` / `file_pre_flight` (and custom
   routes) as Rust **traits** a downstream crate implements, with SIPI consumed
   as a library crate (Phase T). The trait's **default implementation wraps the
   Lua FFI**, so the out-of-the-box binary behaves exactly as the Lua path; a
   user crate opts in by providing its own impl. This is the Rust-native
   alternative to Lua, not a replacement for it.

The choice is the operator's. DaSCH/DSP will likely adopt the Rust trait for its
own deployment; that is a DSP choice and does **not** make Lua a deprecated or
transitional feature of SIPI. Config parsing (the `config.*` surface) is **not**
trait-ified — it stays config.

## Consequences

- **`sipi_run_lua_route` is the permanent Lua-route serving path.** The Phase C
  cutover implements it (Rust owns route dispatch once the transport's
  `script_handler` is deleted); it is not a throwaway bridge. The Phase T Rust
  route trait runs in parallel as an alternative.
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
  not required for the Rust-native extension story, which the trait already
  delivers without it. Keeping the C++ Lua runtime behind the FFI is an
  acceptable long-term state. mlua becomes a "drop the C++ Lua dep" nicety, not a
  strangler goal.
- **`PreflightDecision` is modelled as a clean value type** (a permission-type
  enum + an open key/value map), so the C++ `build_preflight` return and the
  future Rust trait return are a 1:1 port — no seam change is needed to add the
  trait later.

Refines decision #9 of the strangler plan; extends ADR-0013. The as-built trait
API (`trait PreFlight`, `trait RouteHandler`, `ServerConfig::with_preflight`) is
specified and amended into this record when Phase T lands.
