# Building with Nix

`nix` is the single entry point for every Sipi build artifact (dev binary, static binary, Docker image, debug symbols). It is reproducible, hermetic, and shares its dependency cache with CI via Cachix.

## Nix primer (for newcomers)

A few concepts that make the rest of this page click:

**Derivation.** Each package in the flake — `dev`, `default`, `release`, `sanitized`, `fuzz`, `docker`, `static-amd64`, etc. — is a separate *derivation*: a build recipe with fully declared inputs (source, compiler, flags, deps). Nix hashes the recipe, builds once, caches the result in `/nix/store`, and serves future requests instantly.

**Outputs.** A single derivation can produce multiple outputs. For example `.#dev` emits:

- default output — the sipi binary + runtime files
- `^debug` — split debug symbols (from `separateDebugInfo = true`)
- `^coverage` — `coverage.xml` from gcovr

Select an output with `^`: `nix build .#dev^coverage`. Without `^`, you get the default output.

**List what the flake offers:**

```
nix flake show                # current system only
nix flake show --all-systems  # every system (darwin + linux)
```

**Cross-platform dispatch.** Each derivation declares its target `system` (e.g., `aarch64-linux`). `nix build` reads it and either builds locally (if the host matches) or hands off to a configured external builder. No `--system` flag needed — see [Building Linux binaries from macOS](#building-linux-binaries-from-macos).

**Common syntax shapes:**

| Form                                     | Meaning                                         |
| ---------------------------------------- | ----------------------------------------------- |
| `nix build .#dev`                        | Current-system `dev` derivation, default output |
| `nix build .#packages.aarch64-linux.dev` | Explicit target system                          |
| `nix build .#dev^coverage`               | A specific output of a derivation               |
| `nix flake show`                         | List everything the flake exposes               |

## One-time setup

**1. Install [Determinate Nix](https://docs.determinate.systems/).** On macOS, add the `native-linux-builder` feature so you can build any Linux variant from macOS without a VM. As of April 2026 this is **not yet generally available** — request access via Determinate Systems support, per the [native-linux-builder setup notes](https://docs.determinate.systems/troubleshooting/native-linux-builder/). Once enabled, verify:

```
nix config show | grep external-builders
```

You should see entries for `aarch64-linux` and `x86_64-linux`. Without `native-linux-builder`, Linux-targeted builds on macOS need a remote Linux builder configured by hand.

**2. Enable the shared Cachix cache.** This is what turns a 15-minute cold build into a 2-minute warm one — CI pushes every `ext/` dep and every `sipi-*` output to `dasch-swiss.cachix.org`.

```
cachix use dasch-swiss
```

This writes `~/.config/nix/nix.conf` entries for the substituter and trusted public key. After the first successful `nix build`, subsequent builds on the same flake closure are near-instant.

**3. (Optional) GH_TOKEN for cold-cache Kakadu fetches.** The Kakadu FOD only runs when Cachix doesn't already have its output. In that rare case it needs a GitHub token to fetch `dsp-ci-assets`. Put this in `.envrc` once:

```
# .envrc
export GH_TOKEN=$(gh auth token)
```

On the hot path you never notice this — Cachix serves the Kakadu output and `GH_TOKEN` is never read.

## Build artifacts (via `just` — what CI invokes)

Every build recipe wraps `nix build .#<variant>`. No imperative cmake invocations in recipes — CI invokes only `just <recipe>`.

```
just nix-build                         # .#dev: Debug + coverage; unit tests run in the sandbox
just nix-build-default                 # .#default: RelWithDebInfo + tests
just nix-build-release                 # .#release: stripped, no tests
just nix-build-sanitized               # .#sanitized: Debug + ASan + UBSan
just nix-build-fuzz                    # .#fuzz: libFuzzer binary only
just nix-build-static-amd64            # .#static-amd64: Zig-in-Nix musl
just nix-build-static-arm64            # .#static-arm64: Zig-in-Nix musl
just nix-build-release-archive-amd64   # .#release-archive-amd64: tarball + sha256 + debug
just nix-build-release-archive-arm64   # .#release-archive-arm64: tarball + sha256 + debug
just nix-coverage                      # .#dev^coverage: writes result-coverage/coverage.xml
just nix-docker-build                  # streams .#docker-stream into the local Docker daemon
```

Debug symbols for any variant are available on the `.debug` output:

```
nix build .#dev.debug                  # extracted symbols at result/lib/debug/...
```

## Dev shell inner loop (non-recipe — local iteration only)

The justfile does NOT expose an imperative build recipe. The fast edit/rebuild cycle is intentionally documented here as a dev-shell pattern instead of a recipe — recipes are contracts that CI runs the same command, and CI always goes through a Nix derivation.

```
nix develop                    # default = clang + libc++
nix develop .#fuzz             # libstdc++ for libFuzzer ABI
nix develop .#gcc              # gcc14Stdenv

# Inside the shell (non-reproducible, will NOT match CI byte-for-byte):
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON
cmake --build build --parallel
./build/sipi --config config/sipi.localdev-config.lua
# subsequent edits:
cmake --build build            # incremental
```

To run the reproducible CI build (same commands as CI), use `just nix-build` instead. For tests against the built sipi, use:

```
just rust-test-e2e             # resolves $SIPI_BIN, default ./result/bin/sipi
just hurl-test                 # Hurl HTTP contract tests
just nix-run                   # sipi with the dev config
just nix-valgrind              # sipi under Valgrind
```

## Building Linux binaries from macOS

Requires `native-linux-builder` enabled on your Determinate Nix install (see [One-time setup](#one-time-setup) — not yet GA as of April 2026, request via support). Once it's on, just name the Linux package explicitly:

```
nix build .#packages.aarch64-linux.dev -L
```

Swap `aarch64-linux` for `x86_64-linux` to reproduce the amd64 CI variant. Works for every package: `dev`, `default`, `release`, `sanitized`, `fuzz`, `static-amd64`, `static-arm64`.

`native-linux-builder` dispatches the build automatically; you don't pass a `--system` flag. Warm Cachix → seconds. Cold Cachix → ~15 min (and the Kakadu FOD will need `GH_TOKEN` in your env — see step 3 of One-time setup).

## Static Linux binaries

Static musl binaries are produced by the Nix flake via a Zig-based toolchain (`cmake/zig-toolchain.cmake`). No Zig install is needed on the host — Zig ships inside the Nix dev shell and is invoked by `mkStaticBuild` in `flake.nix`.

```
just nix-build-static-amd64            # x86_64-linux-musl
just nix-build-static-arm64            # aarch64-linux-musl
just nix-build-release-archive-amd64
just nix-build-release-archive-arm64
```

Validation:

```
just nix-static-linkage-verify result/bin/sipi
```
