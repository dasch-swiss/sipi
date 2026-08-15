# Fuzz Testing

> **Status: retired, replacement tracked.** The C++ libFuzzer harness that
> fuzzed the C++ IIIF URI parser (`//fuzz/handlers:iiif_handler_uri_parser_fuzz`,
> its seed corpus, and the `fuzz/handlers/` package) has been removed. IIIF
> request parsing now lives in the Rust shell
> (`//src/iiifparser/rust:iiif_parser` (`parse_request`)), so the parser worth fuzzing is
> Rust code. A Rust fuzz harness against `parse_request` (e.g. `cargo-fuzz` /
> `libfuzzer-sys` under `rules_rust`) is a tracked follow-up.

Until that harness lands there is no working fuzz target. The
`just bazel-build-fuzz` / `just bazel-run-fuzz` recipes, the `.github/workflows/fuzz.yml`
workflow, and the `//tools/fuzz` platform definitions still reference the removed
C++ target and do not build; treat them as stale until the Rust harness replaces
them.

## What a replacement harness needs to cover

The retired harness fed random and mutated bytes to the parser and looked for
crashes, memory-safety issues, and undefined behavior. The Rust replacement
should target the same surface — `parse_request` — with a seed corpus of
known-good IIIF URIs (basic image request, `info.json`, `knora.json`, region /
size / rotation / quality / format permutations) so the fuzzer starts from broad
coverage rather than rediscovering the URI grammar from scratch.
