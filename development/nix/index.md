# Nix dev shell

Nix's role in SIPI is **dev-shell provisioning only**. It assembles a reproducible bash environment with bazelisk, the host tools that `rules_foreign_cc` shells out to (perl, cmake, pkg-config, autoconf/automake/libtool/m4), `gh` (consumed by the `kakadu_archive` repository_rule), `crane` (used by `bazel-docker-publish-manifest`), and `jpylyzer` (JP2 conformance validator). Rust + LLVM toolchains are provisioned hermetically by Bazel (`rules_rust` + `toolchains_llvm` in `MODULE.bazel`) — neither `rustc`/`cargo` nor `clang` ship in the dev shell.

It is **not** the build system. Sipi's binaries, tests, and Docker image are all produced by Bazel. See [Building with Bazel](https://sipi.io/development/bazel/index.md).

## One-time setup

**Install [Determinate Nix](https://docs.determinate.systems/).** The Determinate installer enables flakes and `nix develop` by default. macOS and Linux are both supported.

```
curl -fsSL https://install.determinate.systems/nix | sh -s -- install
```

After the installer finishes, restart your shell and verify:

```
nix --version
```

That's the entire setup. There is no Cachix dependency, no Kakadu FOD pre-fetch step, and no GH_TOKEN export needed at the shell layer (the `kakadu_archive` Bazel repository_rule reads `GH_TOKEN` itself from the build environment; locally `gh auth login` is enough).

## Entering the dev shell

```
nix develop                    # default shell — Clang + libc++ + host tools
just bazel-build               # subsequent commands work as usual
```

Three shells are exposed:

| Shell        | Stdenv                         | Purpose                                                                                                                                                                                                                                                       |
| ------------ | ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `default`    | `llvmPackages_19.libcxxStdenv` | Matches the Bazel toolchain (`toolchains_llvm`). 99% of work happens here.                                                                                                                                                                                    |
| `gcc`        | `pkgs.gcc14Stdenv`             | Diagnostic escape hatch when the LLVM toolchain produces a confusing error. Not used by CI.                                                                                                                                                                   |
| `llvm-tools` | `llvmPackages_19.libcxxStdenv` | `default` + `llvmPackages_19.llvm` (host `llvm-cov`, `llvm-profdata`, `llvm-symbolizer`). Used by `coverage.yml` (coverage) and `sanitizer.yml` (ASan/LSan symbolization); local devs running `just bazel-coverage` or sanitizer e2e should enter this shell. |

```
nix develop .#gcc              # GCC + libstdc++ environment
nix develop .#llvm-tools       # default + LLVM host binaries on PATH
```

`bazel build` invokes its own hermetic LLVM toolchain regardless of which shell is active — the dev-shell stdenv only matters for ad-hoc shell-level compilation.

## What's on the PATH

`flake.nix` is the source of truth (60 lines, dev-shells only). Highlights:

- **bazelisk** + a `bazel` shim — reads `.bazelversion` and downloads the matching Bazel.
- **perl, cmake, pkg-config, autoconf, automake, libtool, m4** — host tools that `rules_foreign_cc` invokes during ext/\* builds.
- **gh, cacert** — the `kakadu_archive` Bazel repository_rule shells out to `gh release download`; `cacert` provides a TLS bundle on headless Linux dev shells.
- **go-containerregistry** — provides `crane`, used by `just bazel-docker-publish-manifest` to assemble the multi-arch manifest from per-arch digests.
- **just** — recipe runner.
- **python3Packages.jpylyzer** — JP2 conformance validator used to verify regenerated JP2 goldens (see ADR-0010 / Phase 15.11).
- **llvmPackages_19.llvm** *(coverage shell only)* — `llvm-cov` / `llvm-profdata` for `just bazel-coverage`. Bazel's `collect_cc_coverage.sh` hard-requires `COVERAGE_GCOV_PATH` and `LLVM_COV` env vars on every test action; the justfile recipe resolves them via `$(command -v llvm-{cov,profdata})`.

## Shell hooks

The default shell hook does three things:

- `git config core.hooksPath .githooks` — points Git at the repo-tracked pre-commit hooks (the SIPI → shttps boundary check).
- Exports `SSL_CERT_FILE` from the `cacert` package so `gh`'s Go-based TLS works on headless Linux dev shells.
- On macOS, prepends `/usr/bin` to `PATH` so toolchains_llvm's `xcrun --show-sdk-path` probe (run inside Bazel repo rules) finds Apple's `/usr/bin/xcrun` ahead of nixpkgs' xcbuild stub (which returns the apple-sdk-14.4 stub — private frameworks only, no `libc++.tbd`).

## direnv

If you use [direnv](https://direnv.net/), drop a one-line `.envrc`:

```
# .envrc
use flake
```

`direnv allow` once and the dev shell auto-activates whenever you `cd` into the repo. Combined with editor integrations (e.g. JetBrains [direnv plugin](https://plugins.jetbrains.com/plugin/15285-direnv-integration), [VS Code direnv extension](https://marketplace.visualstudio.com/items?itemName=Rubymaniac.vscode-direnv)) this gives the IDE the same PATH/env as the terminal.
