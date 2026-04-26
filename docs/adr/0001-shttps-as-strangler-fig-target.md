# shttps is a separate context, slated for strangler-fig replacement by Rust

`shttps/` is treated as its own bounded context with its own namespace, library, language, and test surface — not an internal subdirectory of SIPI. The dependency direction is strictly SIPI → shttps; SIPI must never be named or included from shttps.

We accept this stricter boundary because the medium-term plan is to replace the C++ HTTP layer with a Rust implementation using the strangler-fig pattern. Every leak across the boundary today (e.g. `shttps/Server.cpp` calling `SipiMetrics::instance()`) becomes a migration cost tomorrow, so leaks are tracked as bugs rather than tolerated.

The alternative — treating `shttps/` as a courtesy namespace inside one big SIPI codebase — was rejected because it would erase the seam the migration depends on.
