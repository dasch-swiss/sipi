# Nix dev shell

Nix's role in SIPI is **dev-shell provisioning only**. It assembles
a reproducible bash environment with bazelisk, the host tools that
`rules_foreign_cc` shells out to (perl, cmake, pkg-config,
autoconf/automake/libtool/m4), `gh` (consumed by the
`kakadu_archive` repository_rule), `crane` (used by
`bazel-docker-publish-manifest`), and the test runtimes
(rustc, cargo, hurl, nginx, graphicsmagick, imagemagick).

It is **not** the build system. Sipi's binaries, tests, and Docker
image are all produced by Bazel. See [Building with Bazel](bazel.md).

## One-time setup

**Install [Determinate Nix](https://docs.determinate.systems/).**
The Determinate installer enables flakes and `nix develop` by
default. macOS and Linux are both supported.

```bash
curl -fsSL https://install.determinate.systems/nix | sh -s -- install
```

After the installer finishes, restart your shell and verify:

```bash
nix --version
```

That's the entire setup. There is no Cachix dependency, no Kakadu
FOD pre-fetch step, and no GH_TOKEN export needed at the shell layer
(the `kakadu_archive` Bazel repository_rule reads `GH_TOKEN` itself
from the build environment; locally `gh auth login` is enough).

## Entering the dev shell

```bash
nix develop                    # default shell — Clang + libc++ + host tools
just bazel-build               # subsequent commands work as usual
```

Two shells are exposed:

| Shell | Stdenv | Purpose |
|---|---|---|
| `default` | `llvmPackages_19.libcxxStdenv` | Matches the Bazel toolchain (`toolchains_llvm`). 99% of work happens here. |
| `gcc` | `pkgs.gcc14Stdenv` | Diagnostic escape hatch when the LLVM toolchain produces a confusing error. Not used by CI. |

```bash
nix develop .#gcc              # GCC + libstdc++ environment
```

`bazel build` invokes its own hermetic LLVM toolchain regardless of
which shell is active — the dev-shell stdenv only matters for
ad-hoc shell-level compilation.

## What's on the PATH

`flake.nix` is the source of truth (60 lines, dev-shells only).
Highlights:

- **bazelisk** + a `bazel` shim — reads `.bazelversion` and downloads
  the matching Bazel.
- **perl, cmake, pkg-config, autoconf, automake, libtool, m4** —
  host tools that `rules_foreign_cc` invokes during ext/* builds.
- **gh, cacert** — the `kakadu_archive` Bazel repository_rule shells
  out to `gh release download`; `cacert` provides a TLS bundle on
  headless Linux dev shells.
- **go-containerregistry** — provides `crane`, used by
  `just bazel-docker-publish-manifest` to assemble the multi-arch
  manifest from per-arch digests.
- **just, gcovr, lcov, llvmPackages_19.llvm** — recipe runner +
  coverage tooling.
- **rustc, cargo, hurl** — the Rust e2e harness's inner-loop tools
  (used by `just rust-test-e2e` and `just hurl-test`; CI uses
  `bazel test` instead and does not need `cargo` on PATH).
- **nginx, graphicsmagick, imagemagick, libxml2, libxslt** — test
  runtimes consumed by the e2e + smoke tests.

## Shell hooks

The default shell hook does three things:

- `git config core.hooksPath .githooks` — points Git at the
  repo-tracked pre-commit hooks (the SIPI → shttps boundary
  check).
- Exports `SSL_CERT_FILE` from the `cacert` package so `gh`'s
  Go-based TLS works on headless Linux dev shells.
- On macOS, prepends `/usr/bin` to `PATH` so toolchains_llvm's
  `xcrun --show-sdk-path` probe (run inside Bazel repo rules)
  finds Apple's `/usr/bin/xcrun` ahead of nixpkgs' xcbuild stub
  (which returns the apple-sdk-14.4 stub — private frameworks
  only, no `libc++.tbd`).

## direnv

If you use [direnv](https://direnv.net/), drop a one-line `.envrc`:

```bash
# .envrc
use flake
```

`direnv allow` once and the dev shell auto-activates whenever you
`cd` into the repo. Combined with editor integrations (e.g. JetBrains
[direnv plugin](https://plugins.jetbrains.com/plugin/15285-direnv-integration),
[VS Code direnv extension](https://marketplace.visualstudio.com/items?itemName=Rubymaniac.vscode-direnv))
this gives the IDE the same PATH/env as the terminal.
