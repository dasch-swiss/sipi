---
status: superseded
superseded-by: 0013-shttps-as-internal-module.md
---

# shttps is a separate context, slated for strangler-fig replacement by Rust

> **Superseded by [ADR-0013](0013-shttps-as-internal-module.md) (2026-05-19).** The system-level Context Map for `dsp-repository` places SIPI as one implementation of the IIIF subdomain of the Access Area bounded context — a module *inside* an implementation cannot also be a peer bounded context. The strict one-way dependency direction and the Rust-rewrite plan both survive into ADR-0013; the bounded-context framing does not. The Rust-rewrite scope is also widened in ADR-0013 from "the HTTP layer only" to "the whole of SIPI, starting with the HTTP layer." Body preserved below as historical record.

`shttps/` is treated as its own bounded context with its own namespace, library, language, and test surface — not an internal subdirectory of SIPI. The dependency direction is strictly SIPI → shttps; SIPI must never be named or included from shttps.

We accept this stricter boundary because the medium-term plan is to replace the C++ HTTP layer with a Rust implementation using the strangler-fig pattern. Every leak across the boundary today (e.g. `shttps/Server.cpp` calling `SipiMetrics::instance()`) becomes a migration cost tomorrow, so leaks are tracked as bugs rather than tolerated.

The alternative — treating `shttps/` as a courtesy namespace inside one big SIPI codebase — was rejected because it would erase the seam the migration depends on.
