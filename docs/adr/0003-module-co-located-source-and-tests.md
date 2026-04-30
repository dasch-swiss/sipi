---
status: proposed
---

# Module-co-located source, headers, and unit tests with flat-style includes

A SIPI module's `.cpp`, `.h`, and `*_test.cpp` files live in the same directory (e.g. `src/metadata/{SipiIcc.cpp, SipiIcc.h, sipiicc_test.cpp}`) — not in the historical `src/<mod>/` + `include/<mod>/` shadow + `test/unit/<mod>/` split. Cross-module includes use the flat path-prefixed form `#include "metadata/Foo.h"`; siblings within a module use `#include "Foo.h"`. The `include/<mod>/` shadow directories are deleted; `include/` retains only the generated headers (`SipiVersion.h.in`, `ICC-Profiles/`).

We accept this because Bazel's per-module `cc_library` plus `--strict_deps` and `features = ["layering_check", "parse_headers"]` turns module boundaries from a markdown convention (`CONTEXT-MAP.md`, `scripts/shttps-context-check.sh`) into a build-graph invariant: a forbidden `#include` fails analysis, not code review. The flat-style include already matches what `shttps/` and `src/handlers/` use today, so cross-codebase consistency improves while diff churn stays minimal. SIPI ships a single binary with no public C++ API to gate, so the `include/` shadow exists only to mirror `src/` — pure ceremony. The pattern aligns with Abseil, Bloomberg BDE (Lakos's "physical hierarchy" doctrine), Chromium, and WG21 P1204R0 (Kolpackov's "Canonical Project Structure").

This decision is also a response to agentic coding. AI now writes the bulk of application-level code in this codebase; the human's role has shifted from writing code to defining and policing architecture. AI is excellent at local correctness — making a test pass, fixing a bug — and unreliable at architectural correctness — respecting module boundaries, refusing the convenient shortcut. Markdown rules and shell-script gates are advisory and do not scale with AI throughput. Module co-location lets Bazel's per-package `cc_library` + `package_group` + `layering_check` convert architectural intent into build invariants: the AI cannot accidentally violate the rule, and the human reviews high-leverage `BUILD.bazel` and visibility diffs instead of every implementation line. Review surface area shrinks to the leverage points; the build graph polices everything below.

## Considered Options

- **Keep the split layout** — rejected. The `include/<mod>/` directories exist only to mirror `src/<mod>/`; the three coexisting include styles (`"Foo.h"`, `"mod/Foo.h"`, `"../Foo.h"`) are accidental complexity; and `libsipi_testable` (the CMake static lib that compiles every production `.cpp` for the test binaries) hides intra-module coupling that per-module `cc_library` targets would expose.
- **Abseil-style fully path-prefixed includes** (`#include "sipi/metadata/Foo.h"`) — rejected. Verbose without semantic benefit on a single-binary project; would require touching every `#include` line in the codebase. Flat-style covers the same disambiguation needs at lower churn.

## Consequences

- The CMake `libsipi_testable` God-library is replaced by per-module `cc_library` targets. Each module's unit test depends only on the module's own library, surfacing implicit cross-module coupling as build errors.
- `scripts/shttps-context-check.sh` is deleted — `package_group()` + `visibility` on `//shttps:shttps` enforces the SIPI → shttps direction at analysis time. The known violation (`shttps/Server.cpp` calling `SipiMetrics::instance()`) becomes a build error, forcing a real fix.
- `src/formats/` (today: zero unit tests, only approval coverage) gains a per-format unit-test target as part of the same flip.
- Migration is staged as five mechanical PRs (Y+8a..Y+8e in the Bazel migration plan), gated on the Bazel build-tool migration (Y+6) being merged. Each PR is one module and is revertable.
