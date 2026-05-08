# Building with Nix

`nix` is the single entry point for every Sipi build artifact (dev binary,
static binary, Docker image, debug symbols). It is reproducible, hermetic,
and shares its dependency cache with CI via Cachix.

## Nix primer (for newcomers)

A few concepts that make the rest of this page click:

**Derivation.** Each package in the flake — `dev`, `default`, `release`,
`docker`, etc. — is a separate *derivation*: a build recipe
with fully declared inputs (source, compiler, flags, deps).
Nix hashes the recipe, builds once, caches the result in `/nix/store`,
and serves future requests instantly.

**Outputs.** A single derivation can produce multiple outputs. For
example `.#dev` emits:

- default output — the sipi binary + runtime files
- `^debug` — split debug symbols (from `separateDebugInfo = true`)
- `^coverage` — `coverage.xml` from gcovr

Select an output with `^`: `nix build .#dev^coverage`. Without `^`, you
get the default output.

**List what the flake offers:**

```bash
nix flake show                # current system only
nix flake show --all-systems  # every system (darwin + linux)
```

**Cross-platform dispatch.** Each derivation declares its target
`system` (e.g., `aarch64-linux`). `nix build` reads it and either builds
locally (if the host matches) or hands off to a configured external
builder. No `--system` flag needed — see
[Building Linux binaries from macOS](#building-linux-binaries-from-macos).

**Common syntax shapes:**

| Form | Meaning |
|---|---|
| `nix build .#dev` | Current-system `dev` derivation, default output |
| `nix build .#packages.aarch64-linux.dev` | Explicit target system |
| `nix build .#dev^coverage` | A specific output of a derivation |
| `nix flake show` | List everything the flake exposes |

## One-time setup

**1. Install [Determinate Nix](https://docs.determinate.systems/).**
On macOS, add the `native-linux-builder` feature so you can build any
Linux variant from macOS without a VM. As of April 2026 this is
**not yet generally available** — request access via Determinate
Systems support, per the
[native-linux-builder setup notes](https://docs.determinate.systems/troubleshooting/native-linux-builder/).
Once enabled, verify:

```bash
nix config show | grep external-builders
```

You should see entries for `aarch64-linux` and `x86_64-linux`. Without
`native-linux-builder`, Linux-targeted builds on macOS need a remote
Linux builder configured by hand.

**2. Enable the shared Cachix cache.** This is what turns a 15-minute
cold build into a 2-minute warm one — CI pushes every `ext/` dep and
every `sipi-*` output to `dasch-swiss.cachix.org`.

```bash
cachix use dasch-swiss
```

This writes `~/.config/nix/nix.conf` entries for the substituter and
trusted public key. After the first successful `nix build`, subsequent
builds on the same flake closure are near-instant.

**3. (Optional) GH_TOKEN for cold-cache Kakadu fetches.** The Kakadu
FOD only runs when Cachix doesn't already have its output. In that
rare case it needs a GitHub token to fetch `dsp-ci-assets`. Put this
in `.envrc` once:

```bash
# .envrc
export GH_TOKEN=$(gh auth token)
```

On the hot path you never notice this — Cachix serves the Kakadu
output and `GH_TOKEN` is never read.

## Build artifacts (via `just` — what CI invokes)

Every build recipe wraps `nix build .#<variant>`. No imperative cmake
invocations in recipes — CI invokes only `just <recipe>`.

```bash
just nix-build                         # .#dev: Debug + coverage; unit tests run in the sandbox
just nix-build-default                 # .#default: RelWithDebInfo + tests
just nix-build-release                 # .#release: stripped, no tests
just nix-coverage                      # .#dev^coverage: writes result-coverage/coverage.xml
```

The Docker image is owned by Bazel `rules_oci` (`//src:image`,
DEV-6346) and its split-debug `.debug` artifact by
`//src:sipi_debug_layout`. See
[`building.md`](building.md#docker-build) for the `bazel-docker-*`
recipes; Nix builds no longer expose `.#docker`, `.#docker-stream`, or
`.#sipi-debug` outputs.

Debug symbols for the Nix-built sipi binary remain available on the
`.debug` output of the dev variant for inner-loop debugger work:

```bash
nix build .#dev.debug                  # extracted symbols at result/lib/debug/...
```

## Rust test binaries (via crane)

The Rust e2e harness (`test/e2e-rust/`) and the Docker smoke test build
through dedicated Nix derivations rather than `cargo test` from the dev
shell. CI runs the resulting binaries directly — no cargo on PATH is
required, and crate sources are vendored from `Cargo.lock` so a clean
runner re-substitutes from Cachix instead of re-fetching from
crates.io.

```bash
just nix-test-e2e                      # .#e2e-tests: every tests/<name>.rs
just nix-test-smoke                    # .#smoke-test: docker_smoke (--features docker)
```

Source of truth: [`nix/rust-tests.nix`](https://github.com/dasch-swiss/sipi/blob/main/nix/rust-tests.nix).
The module exposes `e2e-tests` and `smoke-test` derivations, both
sharing a single `cargoArtifacts` deps build via [crane](https://crane.dev/)
(pinned to `v0.23.3` in `flake.nix`).

`flake.nix` stays an orchestrator — topical Nix expressions live in
`nix/<topic>.nix` as functions over `{ pkgs, … }` returning attrsets
that the flake merges into its outputs. `nix/rust-tests.nix` is the
first module under this pattern; existing in-flake builders (Kakadu
FOD, static builds, Docker image, dev shells) follow in later PRs.

Two crane-specific notes:

- The default `installFromCargoBuildLogHook` filters cargo's JSON
  build log with `.profile.test == false`, which is the inverse of
  what's needed for test binaries. The module sets
  `doNotPostBuildInstallCargoBinaries = true` and parses the log
  directly with `jq` to install each `.profile.test == true`
  artifact under its `target.name`.
- Test binaries cannot rely on `env!("CARGO_MANIFEST_DIR")` at
  runtime — that resolves to a Nix sandbox path that no longer
  exists. The `sipi_e2e::repo_root()` helper reads `$SIPI_REPO_ROOT`
  first, falling back to `CARGO_MANIFEST_DIR` for the inner-loop
  `cargo test` path. The `nix-test-e2e` and `nix-test-smoke` recipes
  set `SIPI_REPO_ROOT={{justfile_directory()}}`.

## Docker image

The Docker image is owned by Bazel `rules_oci` (`//src:image`,
DEV-6346 / PR Y+4) — Nix no longer builds it. See
[`building.md`](building.md#docker-build) for the `bazel-docker-*`
recipes, base-image pin, and stamping. `STABLE_*` keys (consumed by
`oci_image`'s `created` and the `oci_push` `remote_tags`) come from
`tools/workspace_status.sh`, the same source that already drives
`SipiVersion.h`.

For ad-hoc Nix-built sipi binaries with a custom `--version` string
(hotfix branches, internal pre-releases), `pkgs.sipi.override
{ providedVersion = "..."; }` still works — only the image-build path
is gone.

## Dev shell inner loop (non-recipe — local iteration only)

The justfile does NOT expose an imperative build recipe. The fast
edit/rebuild cycle is intentionally documented here as a dev-shell
pattern instead of a recipe — recipes are contracts that CI runs the
same command, and CI always goes through a Nix derivation.

```bash
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

To run the reproducible CI build (same commands as CI), use
`just nix-build` instead. For tests against the built sipi, use:

```bash
just rust-test-e2e             # resolves $SIPI_BIN, default ./result/bin/sipi
just hurl-test                 # Hurl HTTP contract tests
just nix-run                   # sipi with the dev config
just nix-valgrind              # sipi under Valgrind
```

## Building Linux binaries from macOS

Requires `native-linux-builder` enabled on your Determinate Nix install
(see [One-time setup](#one-time-setup) — not yet GA as of April 2026,
request via support). Once it's on, just name the Linux package
explicitly:

```bash
nix build .#packages.aarch64-linux.dev -L
```

Swap `aarch64-linux` for `x86_64-linux` to reproduce the amd64 CI
variant. Works for every package: `dev`, `default`, `release`. The
sanitized and fuzz variants are Bazel-native (`just bazel-build-sanitized`
and `just bazel-build-fuzz` respectively) — see [Building from source](./building.md).

`native-linux-builder` dispatches the build automatically; you don't
pass a `--system` flag. Warm Cachix → seconds. Cold Cachix → ~15 min
(and the Kakadu FOD will need `GH_TOKEN` in your env — see step 3 of
One-time setup).
