---
status: accepted
supersedes: 0001-shttps-as-strangler-fig-target.md
---

# shttps is an internal SIPI module, not a peer bounded context

`src/shttps/` is an internal module of this codebase, depended on one-way by the rest of `src/`. It is not a peer bounded context. The Rust rewrite scope is the whole of SIPI, with shttps as the first strangler-fig slice — not the only slice that ever gets replaced.

We accept this because the system-level Context Map for `dsp-repository` (the wider system SIPI ships into) names exactly one bounded context that contains SIPI: **Access Area**, the OAIS Access functional entity. Inside Access Area, **IIIF** is a subdomain; SIPI is one implementation of that subdomain. A module inside an implementation cannot also be a peer bounded context — the DDD framing in [ADR-0001](0001-shttps-as-strangler-fig-target.md) (`CONTEXT-MAP.md`, `shttps/CONTEXT.md` as a peer to the SIPI `CONTEXT.md`) over-claimed shttps's autonomy. Demoting shttps to an internal-module API surface aligns the in-repo modeling with that system-level reality.

The strict SIPI → shttps dependency direction stays. The mechanism evolves: visibility (`//src:__subpackages__` only) plus a curated direct-deps list (with `//src/logging:logging` as the single SIPI dep, explicitly visibility-allowlisted because Logger is a generic primitive, not domain code) enforce direction at the Bazel-graph level. That Bazel-graph enforcement is the sole gate — the legacy `scripts/shttps-context-check.sh` regex check has been retired as redundant (shttps is now the frozen differential-parity oracle, removed after deploy). The broader Y+8 effort (DEV-6353) still tracks landing `--features=layering_check` across the whole tree.

## Considered Options

- **Keep ADR-0001's two-bounded-contexts framing** — rejected. It contradicts the upstream `dsp-repository` Context Map. New readers (and future maintainers) see two different stories depending on whether they read in-repo or system-level docs; the conflict erodes both.
- **Treat shttps as a module but keep `shttps/` at the repo root** — rejected. The physical location signalled peer-context status. Moving to `src/shttps/` is a one-time `git mv` that makes the layout match the model. Done in the commit immediately preceding this ADR.
- **Defer the reframing until the full Bazel `layering_check` rollout lands** — rejected. The reframing is a docs-and-conventions change that costs ~6 files; deferring it leaves the `CONTEXT-MAP.md` / `shttps/CONTEXT.md` story actively misleading for weeks. The actual `layering_check` flip can land later under its own ticket; the framing should not wait on it.

## Consequences

- `CONTEXT-MAP.md` deleted; one `CONTEXT.md` at the repo root, anchored in the Access Area Published Language (Preservation File / Service File / Access File) and the IIIF subdomain vocabulary.
- `shttps/CONTEXT.md` renamed to `src/shttps/README.md` and reframed as a module API doc — the four seam types (`Server`, `Connection`, `RequestHandler`, `LuaServer`) stay canonical, the bounded-context framing drops.
- `include/Logger.h` + `src/Logger.cpp` re-characterised as a generic logging primitive — moved to `//src/logging:logging` (DEV-6487 / DEV-6488) and visibility-allowlisted for shttps as a shared support library. The previously-claimed "Logger leak" was a packaging mistake, not a layering violation; the callback-inversion interface earlier proposed (mirror of `ConnectionMetrics`) is no longer needed.
- Re-homing schedule for shttps-internal utilities (`Hash` / `HashType` / `Parsing` / `Error` / `Global` / `makeunique`) is relaxed from *Rust precondition* to *consistency cleanup*. The whole codebase is going to Rust anyway; those utilities will be re-implemented in the target language regardless of which side they live on in C++.
- Bazel `--features=layering_check` enablement on `//src/shttps:shttps` is **deferred** to the Y+8 effort (DEV-6353). Foreign_cc deps do not emit Clang modulemaps, which breaks `layering_check` on any cc_library that consumes them. The deferral is documented in `src/shttps/BUILD.bazel`'s header comment.
- [ADR-0001](0001-shttps-as-strangler-fig-target.md) is marked Superseded. Its body is preserved as the historical record of the original strangler-fig framing.
