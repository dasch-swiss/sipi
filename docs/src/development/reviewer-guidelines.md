# Reviewer Guidelines

Checklist for human and AI reviewers. Not every item applies to every PR — use judgment.

## Documentation & Discoverability

- [ ] New config keys: documented in `docs/src/guide/sipi.md`, `docs/src/guide/running.md`, and config file inline comments
- [ ] Deprecation warnings: include the new key name and an example of the corrected config line
- [ ] New CLI flags/env vars: `--help` text updated, documented in `running.md`
- [ ] New HTTP endpoints: documented with request/response format
- [ ] If a feature is only discoverable by reading source, it's not done

## Configuration & Defaults

- [ ] Lua config, CLI args, and env vars all accept the same semantics and produce the same defaults
- [ ] Defaults are consistent across all entry points (`SipiConf.cpp`, `sipi.cpp` CLI, documentation)
- [ ] Invalid values produce clear startup errors with guidance on valid values
- [ ] Deprecated keys: old names accepted with warning, both old+new in same config is a hard error

## Commit & PR Hygiene

- [ ] Commits follow [commit-conventions.md](commit-conventions.md) — `feat:` / `fix:` for changelog-visible changes, `build:` / `test:` / `refactor:` for internal
- [ ] One topic per commit (rebase-merge = commits land as-is on `main`)
- [ ] PR description follows the template (Motivation, Summary, Key Changes, Test Plan)

## C++ Quality

- [ ] Builds clean under Clang 15+ and GCC 13+ with `-Wall -Werror`
- [ ] No new compiler warnings introduced
- [ ] Thread safety: shared data structures accessed under appropriate locks
- [ ] No raw `new`/`delete` — use smart pointers or RAII
- [ ] Error paths: resources cleaned up, partial state not left behind
- [ ] GoogleTest unit tests added for new logic; existing tests updated if behavior changes
- [ ] E2E tests added or updated for user-visible behavior changes

## Logging

- [ ] Per-item operations at DEBUG level, summaries at INFO
- [ ] Warnings for recoverable issues (e.g., missing optional files, deprecated config)
- [ ] Errors for unrecoverable issues that prevent operation

## Metrics

- [ ] New metrics use correct Prometheus types (counter for monotonic, gauge for current state, histogram for distributions)
- [ ] Metric names follow `sipi_` prefix convention with `_total` suffix for counters
- [ ] Instrumentation points are in the correct layer (not duplicated across call chain)

## Consistency

- [ ] Follow existing patterns (route registration in `SipiHttpServer::run()`, ExternalProject in `ext/`, test layout in `test/unit/`)
- [ ] Config example files updated alongside code changes
- [ ] New fields mirror structure of similar existing fields

## Testing Strategy Compliance

- [ ] New tests placed in the correct pyramid layer — consult the [decision tree](testing-strategy.md#test-decision-tree)
- [ ] New HTTP behavior tests are Rust e2e or Hurl (not Python) — Python tests are frozen
- [ ] Tests verify behavior (dimensions, content, structure), not just status codes
- [ ] Snapshot tests use `insta` with appropriate redactions for dynamic fields
- [ ] No new `test/unit/` directories — C++ unit tests are frozen (maintain existing only)
- [ ] If a gap from the [coverage matrix](testing-strategy.md#iiif-image-api-30-coverage-matrix) is closed, the matrix is updated

## Security

- [ ] No path traversal possible via user-supplied inputs (IIIF identifiers, config paths, cache file names)
- [ ] Internal-only endpoints (e.g., `/metrics`) documented as requiring reverse proxy protection
- [ ] No secrets or credentials in log output
