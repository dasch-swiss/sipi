#ifndef SIPI_OBSERVABILITY_PROFILING_H
#define SIPI_OBSERVABILITY_PROFILING_H

// The single first-party touch-point for Tracy (https://github.com/wolfpld/tracy).
// Instrumented code includes this header and uses the SIPI_* macros below; it
// never includes <tracy/Tracy.hpp> directly. That keeps the profiler dependency
// at one site and lets the whole codebase be re-pointed at a different profiler
// (or none) by editing this file alone.
//
// Tracy is opt-in and dev-only: the macros expand to Tracy zones only under
// `--config=tracy` (which defines TRACY_ENABLE; see .bazelrc and
// docs/src/development/profiling.md). In every other build
// <tracy/Tracy.hpp> defines its own macros as no-ops, so SIPI_ZONE* compile to
// nothing — zero overhead, nothing to strip.
//
// Worker-thread naming in src/shttps/ uses <tracy/Tracy.hpp> directly rather
// than this shim, because shttps sits below observability in the dependency
// graph and cannot include this header without inverting that direction.
#include <tracy/Tracy.hpp>

// Profile the enclosing scope; the zone is labelled with the function name.
#define SIPI_ZONE() ZoneScoped

// Profile the enclosing scope under an explicit name. Prefer this where the
// function name alone is ambiguous (e.g. the four format handlers' read()/
// write(), which would otherwise all show up as "read"/"write").
#define SIPI_ZONE_N(name) ZoneScopedN(name)

// Mark the end of one logical unit of work (one IIIF request) for Tracy's
// per-frame throughput view.
#define SIPI_FRAME(name) FrameMarkNamed(name)

#endif  // SIPI_OBSERVABILITY_PROFILING_H
