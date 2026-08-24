---
title: "Migrate Sipi build orchestration from Nix to Bazel"
type: refactor
date: 2026-04-29
author: "Ivan Subotic"
status: reviewed(4)
linear: DEV-6341
repositories:
  - sipi
---

# Migrate Sipi build orchestration from Nix to Bazel

## Overview

Replace Nix as Sipi's build orchestrator with Bazel. Nix's role contracts to environment provisioning only — `flake.nix` keeps `devShells` (bazelisk + just + gh + dev tools) and loses every package output. The migration ships in 8 sequential PRs (Y through Y+7); the Nix flake remains authoritative on `main` until Y+6 deletes `package.nix`. Failure pre-Y+6 reverts cleanly with no production impact.

**The primary near-term driver is architectural enforcement in the age of agentic coding.** AI now writes the bulk of application-level code in this codebase; the human's role has shifted from writing code to defining and policing design. Markdown conventions (`CONTEXT-MAP.md`, ADRs, `CLAUDE.md` invariants) and a grep script (`scripts/shttps-context-check.sh`) are advisory and do not scale with AI throughput. Bazel's `package_group()` + `visibility` + `--strict_deps` + Clang `layering_check` turn module-boundary rules into build invariants — a forbidden `#include` fails analysis rather than a code-review TODO. The human review function gains a tool that catches what AI cannot self-police; humans review BUILD-file diffs and visibility changes (high-signal, low-volume) instead of every line of generated implementation. See Problem Statement §1.

The polyglot-monorepo future is the secondary, longer-term driver: validating Bazel on Sipi (now) and dsp-repository's new Rust IIIF service (later, separate plan) unblocks the eventual `dasch-monorepo`.

## Problem Statement

### 1. Architecture and design enforcement in the age of agentic coding

The human engineer's role in this codebase has shifted. AI assistance is normal — the recent ICC determinism work (sipi PR #587), ASan e2e harness fix (PR #590), `OUTPUT_WRITE_FAIL` correction (PR #588), and the bulk of this plan are Claude Code-authored. The author's job has moved from writing code to *defining and enforcing design*: reviewing PRs against `CONTEXT-MAP.md`, running multi-perspective reviewer agents, writing ADRs, deciding which abstractions earn their keep, and gating which violations stay tracked vs. get fixed.

AI is excellent at *local correctness* — one test passes, one bug closes, one refactor lands cleanly inside a function. It is unreliable at *architectural correctness* — respecting module boundaries, refusing the convenient shortcut, recognising when "the cleanest fix" silently violates an invariant the maintainer cares about. AI optimises for the prompt; prompts rarely contain "and don't break the bounded context."

Today's architectural rules live in markdown (`CONTEXT-MAP.md`, ADRs, `CLAUDE.md` invariants) and a grep script (`scripts/shttps-context-check.sh`). Both are advisory. AI *can* be argued into "this case is special"; a tired human reviewer *can* miss a leak. The enforcement loop is human-only, and human review does not scale with AI's code-generation throughput. The known live `shttps/Server.cpp` → `SipiMetrics::instance()` violation (`CONTEXT-MAP.md:14`) is the canonical example: tracked as an exception, never fixed, because nothing forces the fix. The rehome lands as a pre-Y prerequisite ([DEV-6339](https://linear.app/dasch/issue/DEV-6339)) — the migration's first concrete proof-point that the new framing changes behaviour.

**Bazel turns architectural rules into build invariants.** `package_group()` + `visibility` declares who may depend on whom; `--strict_deps` rejects undeclared transitive `#include`s; `features = ["layering_check", "parse_headers"]` makes Clang verify the module map. A `#include` crossing a forbidden boundary fails analysis. A `dep` widening a package's visibility surfaces as a high-signal `BUILD.bazel` diff in the PR. AI cannot accidentally violate the architecture, and the human reviews *the architecture itself* — BUILD-file diffs, visibility changes, new `package_group()`s — not every line of AI-generated implementation. Review surface area shrinks to the leverage points; trust the build graph for everything below.

This is the inversion the AI era requires: humans review the architectural surface AI cannot self-police; the build system polices everything below that line. Markdown conventions and grep scripts cannot deliver this. Bazel can. The shttps→SipiMetrics rehome ([DEV-6339](https://linear.app/dasch/issue/DEV-6339)) lands pre-Y on the existing CMake build; Y through Y+7 establish the *capability* (per-module `cc_library` scaffold in Y, `layering_check` on `//shttps:shttps` as a verification gate confirming the rehome stuck); Y+8a..Y+8e *realise* it across the codebase. The remaining problems below — Nix-build's AI-dependence for *operation*, the maintenance liability of `ext/`, the polyglot-monorepo future — remain valid; this driver is what makes the cost worth paying *now*.

### 2. The current build is AI-supported, not team-sustainable

`flake.nix` (492 lines), `package.nix` (246 lines), and the per-lib `ext/<lib>/CMakeLists.txt` files (24 of them) form a build pipeline that is operable today only because Claude Code carries the cognitive load. The constructs that drive this — `pkgs.llvmPackages_19.libcxxStdenv` overrides, `dockerTools.streamLayeredImage` with `fakeRootCommands`, the Kakadu fixed-output derivation with `gh release download` inside a sandboxed build, the `crane` integration in `nix/rust-tests.nix`, the `separateDebugInfo` debug-info splitting, the per-arch `eachSystem` matrix — are individually documented and individually correct, but their composition exceeds what the team can maintain by hand. Removing AI assistance from any single change to this stack stalls the change.

`nix-darwin` and `home-manager` have a different shape: they are declarative configuration the user has operated for years pre-AI. Nix-as-build (Sipi's current shape) is operationally distinct from Nix-as-config and is the only part to be retired.

### 3. The 22 ext/ libraries hide a maintenance liability

Each `ext/<lib>/CMakeLists.txt` reimplements `ExternalProject_Add` with a per-lib URL, SHA-256, configure flags, and platform fork. The Kakadu file (`ext/kakadu/CMakeLists.txt:1-90`) carries per-arch `sed` commands rewriting upstream Makefiles to swap `CC = g++` for `CC = clang`, strip `-march=armv8.1-a`, strip `-mno-avx256-split-unaligned-load`, and strip `-z noexecstack`. These mutations are conceptually patch files but written as in-CMake string substitution.

`ext/shttp/CMakeLists.txt:11` is a fake `ExternalProject_Add` that "downloads" by `cp -r ${SIPI_SOURCE_DIR}/shttps`. shttps is in-tree code (declared as a separate bounded context in `sipi/CONTEXT-MAP.md` with a one-way SIPI → shttps dependency direction) routed through external-project machinery for historical reasons.

### 4. The polyglot stack ahead is not served by Nix-as-build

Sipi's long-term replacement is a Rust IIIF service in dsp-repository. Both will eventually live in `dasch-monorepo` alongside dsp-api (Scala→Rust over years), dsp-ingest (Rust), dsp-tools (Python), dsp-app (TypeScript). Nix can be coerced to drive a polyglot build graph (`crane` for Rust, `mkDerivation` for C++, `buildSbtPackage` for Scala, `buildPythonApplication` for Python), but each language is glued in by hand — and the maintenance load grows linearly with languages.

Bazel is built for this case. `rules_cc`, `rules_rust`, `rules_scala`, `rules_python`, `rules_js` share one query model, one cache model, one BUILD-file syntax, and link cross-language at the action-graph level. The pilot establishes whether the team can drive Bazel; the dsp-repository pilot extends to Rust greenfield; the monorepo absorption follows.

## Proposed Solution

After Y+7:

- `flake.nix` is ~50 lines: `devShells.{default, clang, fuzz, gcc}` only. No package outputs. No Kakadu FOD. No crane. No dockerTools.
- `MODULE.bazel` declares the build: `bazel-contrib/toolchains_llvm` (LLVM 19.1.x, hermetic, identical across macOS-aarch64, linux-x86_64, linux-aarch64); `rules_foreign_cc` for the 22 third-party libs; `rules_oci` for the Docker image; `rules_rust` + Crate Universe for the Rust e2e/smoke tests. `rules_nixpkgs` is **not** used.
- `//src:sipi` is a thin native `cc_binary` deps-only on `//src:sipi_lib`, a native `cc_library` containing every translation unit except `main.cpp`. The library/binary split is what lets Y+1's per-component unit tests (e.g. `test/unit/sipiicc`, which links against private internal headers like `include/metadata/SipiIccDetail.h`) build cleanly without re-linking the binary's `main`. `//shttps:shttps` is a native `cc_library` (no longer a fake-`ExternalProject`). The 22 third-party libs are `//ext/<lib>:lib` foreign_cc rules wrapping their existing CMake/autotools/Make builds.
- `//src:image` is an `oci_image` built per-arch on its native runner. Per-arch images are pushed individually by `oci_push`; a coordinator job runs `crane index append` to assemble the multi-arch manifest. (`oci_image_index` cannot consume external digests across runners — see `02-research-findings-bazel-implementation.md` §1.)
- The `justfile` invariant carries over: every `bazel-*` recipe wraps `bazel build|test //<target>`; CI invokes only `just <recipe>`. During the migration `bazel-*` and `nix-*` recipes coexist; each `nix-*` recipe is deleted in the same PR that promotes the corresponding `bazel-*` recipe to CI.
- Inner-loop development uses `bazel build //src:sipi` directly (no justfile recipe), matching today's raw-`cmake`-in-dev-shell pattern. The justfile invariant binds CI contracts only.
- The vendored offline-fallback flow (`vendor-download`, `vendor-verify`, `vendor-checksums`, `kakadu-fetch`, `set(_local "${COMMON_VENDOR}/${DEP_<LIB>_FILENAME}")` blocks) is removed; Bazel's `repository_cache` provides equivalent download caching.

## Technical Approach

### Architecture

#### Bazel version — 9.0.2 LTS

`.bazelversion` pins **9.0.2** ([Bazel 9 LTS announcement](https://blog.bazel.build/2026/01/20/bazel-9.html), GA 2026-01-20). LTS support runs to **2028-12-31**.

Bazel 9 implications baked into Y:
- **`load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")` required at the top of every `BUILD.bazel`.** Built-in `cc_*` rules were removed from the global namespace; `--incompatible_autoload_externally` defaults to empty.
- **`load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")` required** for any explicit `http_archive` calls (the kakadu repository_rule continues to use `ctx.execute` directly, unaffected).
- **`.bazelrc` is minimal.** `--enable_bzlmod`, `--incompatible_strict_action_env`, and `--incompatible_enable_cc_toolchain_resolution` are defaults; `.bazelrc` keeps `--incompatible_strict_action_env` only as a documented cache-hygiene anchor and drops the other two.

Known Bazel 9 issues, weighed and accepted:
- **`rules_oci` 2.3.x maintainer CI matrix does not yet list Bazel 9.** Community reports indicate it works; no maintainer-tested guarantee. Pre-Y spike (see *Dependencies & Prerequisites*) validates a hello-world `oci_image` on Bazel 9.0.2 before Y opens.
- **`rules_rust#3817`** — module-extension splicing runs unnecessarily on no-op rebuilds (~30s penalty). Cosmetic for inner-loop performance; affects Y+5. Tracked upstream; expected fix in `rules_rust` 0.59.x+.
- **`layering_check` + `parse_headers`** — `bazel#23460` (parse_headers incompatible with C `copts`) is open and matters for the foreign_cc-built C deps. Mitigation: enable `layering_check` only on first-party `//src` and `//shttps` packages in Y+8e, never on `//ext/<lib>`. `bazel#21592` (sandbox interaction) is closed but unverified on 9.x — Y+8e validates.

#### Toolchain — hermetic LLVM 19.1.x

`MODULE.bazel` declares `bazel-contrib/toolchains_llvm` 1.7.x and pins LLVM 19.1.x. The toolchain registers across `@platforms//os:{macos,linux}` × `@platforms//cpu:{aarch64,x86_64}`. `bazel build` works outside `nix develop`; the dev-shell's `clang` is **not** consulted by Bazel.

**Linux libc++ hermeticity requires a sysroot.** The LLVM tarball's libc++ on Linux is built against glibc and depends on libc/sysroot headers and runtime libs. Without an explicit sysroot the build silently links against the host's glibc — non-hermetic. Y therefore ships a Chromium debian sysroot for both linux-x86_64 and linux-aarch64, declared via `sysroot()` repo rules and `llvm.sysroot()` extension calls. macOS uses the system Xcode CLT SDK (no bundled macOS sysroot in `toolchains_llvm`); `MACOSX_DEPLOYMENT_TARGET=12.0` is pinned in `.bazelrc` for binary portability across macOS versions. Cross-compilation (macOS host → linux target) is a deferred post-launch follow-up (see *Future Considerations*); Phase 1 builds natively per arch.

**The libc++ vs libstdc++ split for fuzz needs a second registered toolchain.** `stdlib` is baked into the `cc_toolchain_config`, not exposed as a per-build flag. A `.bazelrc --config=fuzz` block alone cannot swap stdlib. Y+3 registers `llvm_toolchain_fuzz` (sharing the downloaded LLVM via `llvm.toolchain_root`) with `stdlib = {"": "stdc++"}`, gates it via `//tools/fuzz:fuzz_enabled` constraint, and `--config=fuzz` flips the platform selector to `//tools/fuzz:linux_x86_64_fuzz`. This replaces today's libstdc++ stdenv override (`flake.nix:340-371`); see `02-research-findings-bazel-implementation.md` §5 for the full pattern. Modern libFuzzer (LLVM ≥ 16) ships its own private libc++ — the libstdc++ swap is belt-and-suspenders parity with today's behavior, eligible for removal in a later refactor.

#### Third-party libraries — `rules_foreign_cc`

Each `ext/<lib>/CMakeLists.txt` is replaced by `ext/<lib>/BUILD.bazel`. The mapping mirrors today's build-system fork:

| Pattern (current) | Pattern (Bazel) | Libs |
|---|---|---|
| `ExternalProject_Add` with `CMAKE_ARGS` | `cmake()` | tiff, exiv2, lcms2, webp, jansson, sentry-native, prometheus-cpp, fmt, zstd, sqlite3 |
| `ExternalProject_Add` with `CONFIGURE_COMMAND ./configure` | `configure_make()` | jpeg, openssl, curl, lua, xz, zlib, bzip2, jbigkit, libmagic, expat, png, luarocks |
| `ExternalProject_Add` with raw `make` + Makefile patching | `make()` + patch files | kakadu |

Per-lib `URL` and `URL_HASH` move from `cmake/dependencies.cmake` to `http_archive` calls in `MODULE.bazel`. Configure flags, CMake variables, and `make` arguments port verbatim from `ExternalProject_Add` to the foreign_cc rule's `cache_entries=` / `args=` parameters. The Bazel Central Registry is **not** used in Phase 1 — version drift from the explicit pins in `cmake/dependencies.cmake` is unacceptable during a build-system migration.

#### shttps — first-party library

`ext/shttp/BUILD.bazel` does not exist. shttps becomes `//shttps:shttps` — a native `cc_library` whose `srcs = glob(["*.cpp"])`, `hdrs = glob(["*.h", "*.hpp"])`, `deps = [...]`. The `cp -r` mechanism in `ext/shttp/CMakeLists.txt` is deleted. The bounded-context boundary declared in `sipi/CONTEXT-MAP.md` is preserved at the BUILD-file layer (`//src:sipi_lib` depends on `//shttps:shttps`; `//shttps:shttps` does not depend on `//src:sipi_lib`).

#### Architectural-boundary enforcement at the build-graph layer

Today's `sipi/CONTEXT-MAP.md` declares the SIPI → shttps one-way rule and `scripts/shttps-context-check.sh` greps for violations. Bazel encodes the same rule as a build invariant via three composable mechanisms:

1. **`package_group()` + `visibility`** — `//shttps:shttps` declares `visibility = ["//src/...", ...]`; reverse dependencies fail analysis.
2. **`features = ["layering_check", "parse_headers"]`** (Clang) — every `#include` must resolve to a header declared in a *direct* `deps` entry of the consuming `cc_library`. Catches undeclared transitive `#include`s that `--strict_deps` alone misses.
3. **`--strict_deps=error`** (default) — same idea at the Bazel-action layer; complements layering_check.

The known live violation (`shttps/Server.cpp` → `SipiMetrics::instance()`, tracked in `CONTEXT-MAP.md:14`) is being rehomed pre-Y on the existing CMake build under [DEV-6339](https://linear.app/dasch/issue/DEV-6339) (observer-pattern inversion: shttps owns an abstract `ConnectionMetrics` interface, SIPI installs a concrete adapter at startup). Y itself does **not** enable layering_check repo-wide — that gating happens in the post-Y+6 layout-flip series (Future Considerations) once every module is a proper `cc_library`. Y enables it on `//shttps:shttps` only as a verification gate confirming the rehome stuck.

#### Kakadu — closed source, private fetch via custom `repository_rule`

**Auth UX preserved 1:1 with today's Nix FOD.** A custom `kakadu_archive` `repository_rule` (in `bazel/kakadu.bzl`) wraps `gh release download`, mirroring the FOD pattern in `flake.nix:36-78`. Plain `http_archive` + `auth_patterns` is **not** used because:

- `auth_patterns` consumes `~/.netrc`, which `gh` does not natively populate. `gh auth setup-git` configures `git`'s `credential.helper`, not `~/.netrc` — so the "auth_patterns + .netrc" path requires a manual `~/.netrc` write step out-of-band, regressing UX vs. today.
- `auth_patterns` does not read env vars directly, so passing `DASCHBOT_PAT` to a Bazel CI run would not authenticate `http_archive` either — same `~/.netrc` requirement.

Both regressions are avoided by keeping the existing `gh release download` mechanism. The repository_rule is ~30 lines.

**Implementation sketch (`bazel/kakadu.bzl`).**

```python
def _kakadu_archive_impl(ctx):
    token = ctx.os.environ.get("GH_TOKEN") or ctx.os.environ.get("GITHUB_TOKEN")
    gh = ctx.which("gh") or fail("gh CLI not on PATH (declared in flake.nix devShells)")
    if not token:
        # Local dev: read token from gh CLI's stored credentials
        result = ctx.execute([gh, "auth", "token"])
        if result.return_code != 0:
            fail("Failed to read gh auth token: " + result.stderr +
                 "\n  Run `gh auth login` and retry, or set GH_TOKEN explicitly.")
        token = result.stdout.strip()

    result = ctx.execute(
        [gh, "release", "download", ctx.attr.tag,
         "--repo", ctx.attr.repo, "--pattern", ctx.attr.asset, "--output", "kakadu.zip"],
        environment = {"GH_TOKEN": token},
    )
    if result.return_code != 0:
        fail("gh release download failed: " + result.stderr)

    ctx.extract("kakadu.zip", stripPrefix = ctx.attr.strip_prefix)
    for patch in ctx.attr.patches:
        ctx.patch(patch, strip = 1)
    ctx.symlink(ctx.attr.build_file, "BUILD.bazel")

kakadu_archive = repository_rule(
    implementation = _kakadu_archive_impl,
    attrs = {
        "tag": attr.string(mandatory = True),         # "kakadu-v8.5"
        "asset": attr.string(mandatory = True),       # "v8_5-01382N.zip"
        "repo": attr.string(mandatory = True),        # "dasch-swiss/dsp-ci-assets"
        "sha256": attr.string(mandatory = True),
        "strip_prefix": attr.string(),
        "patches": attr.label_list(allow_files = True),
        "build_file": attr.label(allow_single_file = True, mandatory = True),
    },
    environ = ["GH_TOKEN", "GITHUB_TOKEN"],   # invalidate when token state changes
)
```

`MODULE.bazel` invokes via a module extension that calls `kakadu_archive(name = "kakadu", tag = "kakadu-v8.5", asset = "v8_5-01382N.zip", repo = "dasch-swiss/dsp-ci-assets", sha256 = "c19c7579d1dee023316e7de090d9de3eb24764e349b4069e5af3a540fb644e75", patches = [...per-arch patch labels...], build_file = "//bazel:kakadu.BUILD.bazel")`.

**Authentication flow (preserved 1:1 from today's FOD):**

| Environment | Token source |
|---|---|
| Local dev | `gh auth token` (from `~/.config/gh/hosts.yml` populated by `gh auth login`) |
| CI | `GH_TOKEN=${{ secrets.DASCHBOT_PAT }}` env var on the Bazel job |
| First-time setup | `gh auth login` once + `dasch-swiss` org membership — same prerequisite as `just kakadu-fetch` today |

**`gh` CLI is a confirmed host-tool dep** in `flake.nix` `devShells.{default, clang, fuzz, gcc}`. Today it is in the FOD's `nativeBuildInputs = [pkgs.gh ...]`; Y moves it to the regular `devShells` package list (since the FOD goes away in Y+6 but the repository_rule still needs `gh` on PATH).

**Caching.** Bazel's `repository_cache` keys by `sha256` — once `kakadu.zip` is fetched, subsequent local builds substitute from cache without re-invoking `gh`. After remote-cache adoption (post-Y+4 decision), the Kakadu repository_rule output substitutes from the remote cache and downstream consumers no longer need a token (mirrors today's Cachix substitution behavior).

The per-arch sed mutations on Kakadu's Makefiles become patch files in `patches/`:

| Arch | Patch (replaces sed in `ext/kakadu/CMakeLists.txt:30-54`) |
|---|---|
| linux-aarch64 | `patches/kakadu-Makefile-Linux-arm-64-clang.patch` — strips `-march=armv8.1-a`, `-march=armv8-a`, `HAVE_ARM81 = 1`, `-z noexecstack`; sets `CC = clang` |
| linux-x86_64 | `patches/kakadu-Makefile-Linux-x86-64-clang.patch` — strips `-mno-avx256-split-unaligned-load/store`, `-z noexecstack`; sets `CC = clang` |
| darwin-aarch64 | (no patch — Kakadu's macOS Makefiles use Clang natively) |

The macOS libtool-PATH workaround (`/usr/bin` first to find Apple's `libtool` instead of the GNU one Nix puts on `$PATH`) becomes `env = {"PATH": "/usr/bin:$PATH"}` on the `make()` rule's darwin `select()` branch.

The Kakadu overlay refactor — replacing the `make()` wrapper with a clean `cc_library` glob over Kakadu source for fine-grained file-level caching — is a deliberate post-launch follow-up. Phase 1 keeps the Makefile-driven build to minimize migration risk.

#### Docker image — `rules_oci` + per-arch CI runners + `crane index append`

`//src:image` is an `oci_image` consuming `//src:sipi` plus runtime contents (`tini`, `curl`, `cacert`, `tzdata`, `ffmpeg-headless`, fakeNss-equivalent passwd/group files via `rules_distroless` `passwd()` and `group()` macros). Per-arch CI runners (linux-x86_64 + linux-aarch64) each build their `oci_image` natively and run `oci_push` against the registry. A coordinator job runs `crane index append` to assemble the multi-arch manifest from the two pushed digests. CI matrix unchanged from today's Nix per-arch flow.

`oci_image_index` is **not** used for split-runner manifest assembly because its `images=[...]` attribute accepts only same-build-graph labels, not external digests pushed by other runners (see `02-research-findings-bazel-implementation.md` §1).

**HEALTHCHECK is moved to compose-level in ops-deploy.** The image ships HEALTHCHECK-agnostic. `roles/dsp-deploy/templates/docker-compose-iiif.yml.j2` gains a `healthcheck:` stanza matching today's image-baked values (interval 30s, timeout 5s, start_period 10s, retries 3). Tracked on the infra side as [INFRA-1226](https://linear.app/dasch/issue/INFRA-1226) — a standalone ops-deploy change motivated independently by the OCI spec (HEALTHCHECK is a Docker extension, not part of OCI), so it can land before Y+4 with no coordination dance. Sipi Y+4 should not merge until INFRA-1226 is deployed to staging + prod. Pattern is consistent with the existing `db` service compose-level healthcheck override (`docker-compose-db.yml.j2:88-90`). See `02-research-findings-bazel-implementation.md` §2 for full reasoning.

Cross-compile from macOS to Linux is **not** introduced in Y+4. The dev workflow for "I want to run a Linux Sipi locally on my Mac" is documented in `docs/src/development/building.md` as: spin up an OrbStack/colima Linux VM, mount the source, run `bazel build` inside.

Reproducibility: `oci_image`'s `created` attribute is deterministic — derived from `STABLE_IMAGE_CREATED` (set by `tools/workspace_status.sh` from `git log -1 --format=%cI`, not wall-clock) and consumed via `expand_template` + `stamp_substitutions`. The `STABLE_*` prefix is critical: volatile-status keys (`BUILD_TIMESTAMP`, etc.) cause Bazel to invalidate the action on every build (see [rules_oci#269](https://github.com/bazel-contrib/rules_oci/issues/269)). Layer tarballs are produced by `@tar.bzl` (deterministic by construction). `oci_pull` for the base image pins by `digest =`, never `tag =`. Goal: same `MODULE.bazel.lock` + same source = same OCI tarball SHA on every build, verified via `diffoci`.

Debug-info split (replacing Nix's `separateDebugInfo = true`): `//src:sipi`'s `cc_binary` sets `linkopts = ["-Wl,--build-id=sha1"]` so the build-id is content-addressed (reproducible across identical inputs). A Bazel `genrule` post-processes the binary via `llvm-objcopy --only-keep-debug` → `llvm-objcopy --strip-debug --strip-unneeded` → `llvm-objcopy --add-gnu-debuglink`, laying out the `.debug` file under `lib/debug/.build-id/<xx>/<yy>.debug` (GNU build-id convention) via a follow-up `genrule` reading the build-id from `llvm-readelf -n`. `publish.yml`'s Sentry upload (`sentry-cli debug-files upload`) consumes the same path layout it does today.

#### Rust e2e + smoke tests — `rules_rust` + Crate Universe

`//test/e2e-rust:e2e_test` and `//test/e2e-rust:smoke_test` are `rust_test` targets. `crates_repository` in `MODULE.bazel` reads `test/e2e-rust/Cargo.lock` and generates Bazel build rules for the vendored crate sources. The current `nix/rust-tests.nix` (crane-based) is deleted in Y+5.

#### CI invariant carryover

`CLAUDE.md`'s build reproducibility invariant ("every `nix-*` recipe wraps `nix build .#<variant>`. CI invokes only `just <recipe>` — no inline cmake or `nix build` calls") is reworded in Y+7:

> Every `bazel-*` recipe wraps `bazel build|test|run //<target>`. CI invokes only `just <recipe>` — no inline `bazel` calls. Inner-loop incremental development drops into the dev shell and calls `bazel build //src:sipi` directly; that is a documented dev-shell pattern, not a recipe.

GHA caching uses `bazel-contrib/setup-bazel` (handles repository cache + action cache + output base separation under GHA's 10 GB limit). Every `bazel` invocation in CI passes `--incompatible_strict_action_env` to prevent GitHub-injected env vars (`GITHUB_RUN_ID`, `GITHUB_SHA`, etc.) from poisoning Bazel cache keys.

Remote cache selection (BuildBuddy SaaS vs self-hosted bazel-remote vs GHA-cache-via-proxy) is deferred. Phase 1 ships with no remote cache; CI uses GHA cache via setup-bazel. Re-evaluate after Y+4 lands when CI cost data is available.

#### Versioning and release-please

**release-please does not change.** It is build-tool-agnostic at `release-type: simple` (`.github/release-please/config.json`). It still:
1. Watches conventional commits (`feat:`, `fix:`, `feat!:` ...) on `main`.
2. Opens release PRs that bump `version.txt` and update `CHANGELOG.md` per the configured `changelog-sections`.
3. On release-PR merge, creates a git tag `v<version>` and a GitHub release.

`version.txt` remains the single source of truth (currently `4.1.1`). `.github/release-please/manifest.json` (`{".":"4.1.1"}`) and `config.json` are unchanged in this migration.

**What changes is how the build *consumes* `version.txt`.** Today's path:
- CMake: `file(READ version.txt SIPI_VERSION_STRING)` → `configure_file(SipiVersion.h.in → SipiVersion.h)` substituting `@SIPI_VERSION_STRING@`, `@BUILD_TIMESTAMP@`, `@BUILD_SCM_TAG@`, `@BUILD_SCM_REVISION@`. The compiled `cc_binary` `#include`s `SipiVersion.h` and bakes `VERSION` into the binary.
- Nix: `version = pkgs.lib.strings.trim (builtins.readFile ./version.txt);` (or via the `providedVersion` parameter) → flows into `pkgs.sipi.version` → flows into `imageTag = "v" + sipiForImage.version`.

Bazel uses **`--workspace_status_command` + `expand_template` with `stamp_substitutions`** — the same `STABLE_*` mechanism that drives reproducible OCI image timestamps in Y+4. `tools/workspace_status.sh` (already in Y for image stamping) gains one more `STABLE_*` key:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "${BUILD_WORKSPACE_DIRECTORY:-.}"
echo "STABLE_GIT_COMMIT $(git rev-parse HEAD)"
echo "STABLE_GIT_VERSION $(git describe --tags --always --dirty)"
echo "STABLE_IMAGE_CREATED $(git log -1 --format=%cI)"
echo "STABLE_SIPI_VERSION $(tr -d '[:space:]' < version.txt)"   # NEW
```

`SipiVersion.h.in` is renamed to `SipiVersion.h.tmpl` (or kept as-is — the file extension is conventional) and consumed by `expand_template` in `src/BUILD.bazel`:

```python
load("@aspect_bazel_lib//lib:expand_template.bzl", "expand_template")

expand_template(
    name = "sipi_version_h",
    out = "SipiVersion.h",
    template = "include/SipiVersion.h.in",
    substitutions = {
        # Non-stamp-dependent placeholders — empty default values for clean local builds.
    },
    stamp_substitutions = {
        "@SIPI_VERSION_STRING@":   "{{STABLE_SIPI_VERSION}}",
        "@BUILD_SCM_TAG@":         "{{STABLE_GIT_VERSION}}",   # `git describe --tags --always --dirty`
        "@BUILD_SCM_REVISION@":    "{{STABLE_GIT_COMMIT}}",
        "@BUILD_TIMESTAMP@":       "{{STABLE_IMAGE_CREATED}}", # commit time, not wall-clock — matches reproducibility goal
    },
)

cc_binary(
    name = "sipi",
    srcs = [...] + [":sipi_version_h"],   # generated header is an input
    includes = ["."],                     # so #include "SipiVersion.h" resolves
    linkopts = ["-Wl,--build-id=sha1"],
    deps = [...],
)
```

**Cache behavior.** `expand_template` reads `STABLE_*` from `stable-status.txt`, which Bazel re-evaluates each build but only **invalidates downstream actions when a value changes**. So:
- Editing a `.cpp` file → `version.txt` unchanged → `STABLE_SIPI_VERSION` unchanged → `SipiVersion.h` unchanged → no recompile of unrelated translation units (only the edited `.cpp` recompiles).
- release-please bumps `version.txt` → `STABLE_SIPI_VERSION` changes → `SipiVersion.h` regenerates → `cc_binary` re-links (and the few translation units that include `SipiVersion.h` recompile).

This matches today's behavior exactly: bumping `version.txt` triggers a re-link, not a full rebuild of every dep.

**OCI image tag** is set in Y+4 via `oci_push`'s `remote_tags` attribute consuming the same `STABLE_SIPI_VERSION` via `expand_template` — produces `daschswiss/sipi:v<version>` matching today's tag scheme.

**`${{ github.ref_name }}` in `publish.yml` is unchanged.** It resolves to the git tag created by release-please on the release-PR merge. After Y+4, `publish.yml` continues using `${{ github.ref_name }}` for tagging the pushed image (in addition to `latest` and the per-arch tags).

**Verification gates** (added to Y+6 acceptance criteria):
1. Bump `version.txt` in a throwaway branch, run `bazel build //src:sipi`, run `./bazel-bin/src/sipi --version`, expect the new value.
2. Confirm `git diff` after the build shows no untracked/dirty changes (stamping does not write back to source files).
3. Confirm CI's release-please flow (`.github/workflows/release-please.yml`) runs unchanged on the post-Y+6 main branch.

#### CI side-channels: coverage, Sentry, SARIF, SBOM, scanning

The migration touches the build tool. It does **not** touch the side-channel uploaders. This section enumerates each side-channel, where it runs, and what the migration changes vs. preserves — so reviewers don't have to hunt across PRs.

**Coverage (Codecov, per-PR, amd64 only).**

- Today: `just nix-coverage` produces `result-coverage/coverage.xml` (cobertura). `codecov/codecov-action@v6` uploads with `CODECOV_TOKEN`. Conditional `if: matrix.arch == 'amd64'` (`ci.yml:104-114`).
- After Y+6: `just bazel-coverage` produces lcov via `bazel coverage --combined_report=lcov --instrumentation_filter='//src,//shttps'`. Codecov accepts lcov directly (no cobertura conversion needed); `codecov/codecov-action@v6`'s `files:` parameter changes from `result-coverage/coverage.xml` to `bazel-out/_coverage/_coverage_report.dat` (or wherever Bazel emits the combined report). Same `CODECOV_TOKEN` secret. Same amd64-only conditional.
- Y+6 verifies: Codecov receives the report; coverage percentage does not regress more than ±2% (sanity check on instrumentation_filter scope).

**Sentry debug symbols (`publish.yml` on tag push, per-arch).**

- Today: `just nix-docker-extract-debug ${arch}` extracts the `.debug` file from `result-debug/lib/debug/.build-id/<xx>/<yy>.debug` and renames it to `sipi-${arch}.debug`. `sentry-cli debug-files upload sipi-${arch}.debug` uploads with `SENTRY_AUTH_TOKEN`, `SENTRY_ORG`, `SENTRY_PROJECT` secrets. `sentry-cli` is npm-installed in the workflow (`publish.yml:137-154`).
- After Y+4: `just bazel-docker-extract-debug ${arch}` produces the same `sipi-${arch}.debug` filename from the new Bazel `genrule` debug-info layout (`bazel-bin/src/lib/debug/.build-id/<xx>/<yy>.debug` → renamed). publish.yml's `Upload Docker debug symbols to Sentry` step is **unchanged** — same `sentry-cli debug-files upload sipi-${arch}.debug` invocation, same secrets. The migration only changes the upstream of the filename, not the filename itself or the upload mechanism.
- Y+4 verifies: filename `sipi-amd64.debug` and `sipi-arm64.debug` are produced at the path the existing publish.yml step expects; `sentry-cli debug-files upload` exits 0 on a dispatch-only test workflow before Y+4 merges.

**Sentry release notification (`publish.yml` on tag push, once).**

- Today: `getsentry/action-release@v3` with `RELEASE_VERSION=$(git describe --tag --dirty --abbrev=7)` (`publish.yml:175-190`). Fires after the manifest job.
- After Y+6: **unchanged.** Sentry release notification is independent of the build tool — it consumes the git tag, not Bazel artifacts.

**Docker Scout — compare (per-PR, `ci.yml`, PRs only).**

- Today: `docker/scout-action@v1` `command: compare` against the local Bazel-loaded image, posts CVE-diff PR comment (`ci.yml:120-131`).
- After Y+4: **unchanged in mechanism.** Docker Scout compares OCI images regardless of how they were built. The image label `local://daschswiss/sipi:latest` (loaded by `oci_load`) is the same. PR-comment behavior preserved.
- Y+4 verifies: a draft PR triggers the Docker Scout compare action and produces a comment.

**Docker Scout — CVE report SARIF (per-PR, `ci.yml`, amd64 PRs only).**

- Today: `docker/scout-action@v1` `command: cves` produces `scout-results.sarif`, then `github/codeql-action/upload-sarif@v4` uploads it to GitHub Security (Code Scanning Alerts panel) (`ci.yml:132-146`).
- After Y+4: **unchanged.** SARIF generation is downstream of the OCI image; image origin is opaque to Scout.
- Y+4 verifies: a draft PR triggers the SARIF upload step; alerts show in the repo's Security tab.

**Docker Scout — record production (`publish.yml` on tag push, per-arch).**

- Today: `docker/scout-action@v1` `command: environment image: registry://daschswiss/sipi:${{ github.ref_name }}-${{ matrix.arch }}` records the per-arch image digest as the new "production" environment in Docker Scout's inventory (`publish.yml:115-121`).
- After Y+4: **unchanged.** Operates on the pushed registry image; agnostic to build tool. The image tag scheme `daschswiss/sipi:v<version>-<arch>` is preserved (driven by `STABLE_SIPI_VERSION` + `${{ github.ref_name }}`).

**Docker Scout — SBOM (`publish.yml` on tag push, per-arch, SPDX format).**

- Today: `docker/scout-action@v1` `command: sbom` produces `sbom-{arch}.spdx.json` (SPDX, not CycloneDX). Uploaded as a GitHub Actions artifact (`actions/upload-artifact@v7` named `sbom-{arch}`) (`publish.yml:123-135`).
- After Y+4: **unchanged.** Docker Scout's SBOM cataloger reads OCI image manifest layers — works on the Bazel-built image the same way.
- The SBOM is uploaded only as a GitHub artifact, **not attached to the Docker Hub image as an OCI referrer**. Registry-attached SBOMs (`cosign attest`) are introduced in the post-launch Y+11 follow-up; out of scope for the migration itself.
- An earlier note in `02-research-findings-bazel-implementation.md` §8 claimed "Trivy is paused, use Syft" — that claim was **retracted** (Trivy's March 2026 incident was resolved 23 March 2026; project is actively maintained). The corrected analysis lives in `03-research-findings-scout-replacement.md`. The Scout → OSS swap (Trivy + Syft + cosign) is sequenced as Y+9 / Y+10 / Y+11, not Y+4.

**LFS objects.**

- Today: `git lfs install && git lfs fetch --all && git lfs checkout` runs at the top of every CI job that needs test fixtures. dasch-specs assets and sipi LFS-tracked fixtures are pulled.
- After Y+6: **unchanged.** LFS is orthogonal to the build tool.

**Loadtest workflow (`loadtest.yml`).** Retired pre-migration in [sipi#592](https://github.com/dasch-swiss/sipi/pull/592) — the synthetic `wrk`-against-three-endpoints nightly run did not mirror dasch-prod-01's traffic shape and the artifacts were not consulted. Load testing moves to a staging environment as Future-scope work. **Out of migration scope** — there is nothing to port.

**mkdocs build verification (`ci.yml` `docs-build` job, per-PR).**

- Today: `dasch-swiss/sipi/.github/actions/setup-python@main` (composite action installing Python 3.11 via deadsnakes PPA) + `just docs-install-requirements` (`pip3 install -r docs/requirements.txt`) + `just docs-build` (`cd docs && mkdocs build`) (`ci.yml:194-198`). Merge gate — broken markdown / cross-links fail PRs.
- After Y+7: **unchanged.** mkdocs is a Python tool building markdown to HTML; it has never invoked Nix or Bazel and doesn't need to. The `docs-build` job stays Python-on-pip; Bazel does not own this path. The Y+7 markdown rewrite **must** keep this job green — verified explicitly in Y+7 acceptance below.

**mkdocs site deploy (`publish.yml` on tag push).**

- Today: `mhausenblas/mkdocs-deploy-gh-pages@master` deploys `docs/mkdocs.yml` to sipi.io (`publish.yml:192-203`); the action internally runs `pip install -r docs/requirements.txt` + `mkdocs gh-deploy`.
- After Y+7: **unchanged.** Documentation deploy is outside the build graph. The `docs/src/development/` markdown rewrites in Y+7 are *content* updates only.

**Python / mkdocs supply-chain hygiene (out of migration scope).** Three orthogonal improvements are flagged but **not bundled** into the Bazel migration: (1) pin `docs/requirements.txt` to exact versions (or move to `uv` + lockfile); (2) SHA-pin `mhausenblas/mkdocs-deploy-gh-pages@master` (same reasoning as the Y+9 Trivy SHA-pinning); (3) add `mkdocs` + `mkdocs-material` to `flake.nix` `devShells` so `just docs-serve` works inside `nix develop` without a global `pip install`. These belong in a separate sipi PR, not in any Y-numbered phase here.

**Net summary for review:** the migration changes only the *build* commands wrapped by justfile recipes (`nix-*` → `bazel-*`). Every uploader, every secret name, every artifact path, every PR-comment destination, every dashboard, and every external service integration is preserved — verified per side-channel above. Reviewers can confirm by diffing `.github/workflows/{ci,publish}.yml` between pre-Y+6 and post-Y+7: the only edits should be `just nix-*` → `just bazel-*` and the `files:` parameter feeding `codecov/codecov-action`. The `sentry-cli debug-files upload` invocation (and its filename) is unchanged.

**Docker Scout swap is on the post-launch roadmap** (Future Considerations Y+9 / Y+10 / Y+11) — Trivy + Syft + cosign replace Scout once Y+4 is stable, adding Sigstore signing + Rekor transparency. See `03-research-findings-scout-replacement.md`. **Not bundled into Y+4** — Y+4 is already load-bearing; mixing the build-tool migration with a scanning-stack swap multiplies review/revert risk.

### Implementation Phases

The migration executes as **8 sequential PRs (Y through Y+7)**. Each PR is independently reviewable, ships green CI, and can be reverted without rolling back its predecessors. The Nix flake remains authoritative on `main` until Y+6 deletes `package.nix`. Post-launch follow-ups (cross-compile; Y+8 layout flip; Y+9/Y+10/Y+11 Docker Scout → OSS swap; etc.) are documented in the *Future Considerations* section, not in the migration phase below.

**Progress is tracked on Linear.** The 8 phases are filed as sub-issues of the migration umbrella [DEV-6341](https://linear.app/dasch/issue/DEV-6341), each self-contained and individually executable from the Linear issue alone:

| Phase | Linear | What |
|---|---|---|
| Y | [DEV-6342](https://linear.app/dasch/issue/DEV-6342) | Land entire Bazel build graph |
| Y+1 | [DEV-6343](https://linear.app/dasch/issue/DEV-6343) | Unit + approval tests via Bazel |
| Y+2 | [DEV-6344](https://linear.app/dasch/issue/DEV-6344) | Sanitized variant; sanitizer.yml build cutover |
| Y+3 | [DEV-6345](https://linear.app/dasch/issue/DEV-6345) | Fuzz harness; fuzz.yml build+run cutover |
| Y+4 | [DEV-6346](https://linear.app/dasch/issue/DEV-6346) | Docker image via `rules_oci` |
| Y+5 | [DEV-6347](https://linear.app/dasch/issue/DEV-6347) | Rust e2e + smoke via `rules_rust` |
| Y+6 | [DEV-6348](https://linear.app/dasch/issue/DEV-6348) | Default + release variants; full cutover |
| Y+7 | [DEV-6349](https://linear.app/dasch/issue/DEV-6349) | Cleanup + docs rewrite |

Pre-Y prerequisites are tracked separately: [DEV-6339](https://linear.app/dasch/issue/DEV-6339) (shttps→SipiMetrics rehome) blocks Y; [INFRA-1226](https://linear.app/dasch/issue/INFRA-1226) (compose-level HEALTHCHECK) blocks Y+4. Linear blocking relationships chain Y → Y+1 → Y+2 → ... → Y+7 so the sequence is enforced at the issue-tracker layer too.

The phase-by-phase content below remains the canonical design; the Linear issues mirror it for execution-time reference. Any drift between this plan and the Linear issues should be resolved by updating both, with this plan staying authoritative for design and Linear authoritative for in-flight status.

#### PR Y ([DEV-6342](https://linear.app/dasch/issue/DEV-6342)) — `bazel build //src:sipi` produces a working binary

**Scope.** Land the entire Bazel build graph at once (~1500 lines of new Bazel + ~50 lines of `flake.nix` dev-shell additions for `bazelisk`). Single big-bang PR. Nix flake untouched. CI untouched. `justfile` untouched. No `bazel-*` recipes. Nothing in CI runs Bazel yet.

**Adds.**
- `MODULE.bazel`, `MODULE.bazel.lock`, `.bazelrc`, `.bazelversion` (pinned to **9.0.2** — see *Bazel version* subsection above)
- Explicit `load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")` at the top of every `src/`, `shttps/`, `ext/<lib>/`, and (in Y+1) `test/unit/<comp>/` `BUILD.bazel` (Bazel 9 removed built-in `cc_*` rules from the global namespace)
- `bazel_dep`s in `MODULE.bazel`: `rules_foreign_cc`, `toolchains_llvm`, `aspect_bazel_lib` (used by Y for `expand_template` / `SipiVersion.h`; Y+4 reuses it for image stamping), `platforms`. (`rules_oci`, `rules_distroless`, `@tar.bzl`, `rules_rust` are added later in Y+4 and Y+5.)
- `toolchains_llvm` 1.7.x in `MODULE.bazel` pinned to LLVM 19.1.x with `stdlib = "libc++"` for default + `cxx_standard = c++23`
- `sysroot()` repo rules + `llvm.sysroot()` extension calls for linux-x86_64 and linux-aarch64 (Chromium debian sysroots; required for hermetic libc++ on Linux per `02-research-findings-bazel-implementation.md` §4)
- `platforms/BUILD.bazel` declaring `linux_x86_64`, `linux_aarch64`, `darwin_aarch64`, `darwin_x86_64`
- 22 × `ext/<lib>/BUILD.bazel` files (foreign_cc rules; each sets `set_file_prefix_map = True` for cache portability and uses a tight `lib_source` `glob` excluding `**/test/**`, `**/tests/**`, `docs/**`, `**/*.md`)
- `bazel/kakadu.bzl` (custom `kakadu_archive` `repository_rule` invoking `gh release download` — preserves today's FOD auth UX, no `~/.netrc` plumbing needed) + `bazel/kakadu_extension.bzl` (module extension wiring it into `MODULE.bazel`) + `bazel/kakadu.BUILD.bazel` (the cc_library wrapper that consumes the patched Kakadu source via the foreign_cc `make()` rule)
- 2 × Kakadu patch files in `patches/` (Linux x86_64, Linux aarch64)
- `shttps/BUILD.bazel` defining `//shttps:shttps` as a native `cc_library`
- `src/BUILD.bazel` defining `//src:sipi_lib` as a native `cc_library` (every `.cpp` except `main.cpp`; private internal headers like `include/metadata/SipiIccDetail.h` exposed via `hdrs` so unit tests in Y+1 can link against them) and `//src:sipi` as a thin `cc_binary` with `srcs = ["main.cpp"]` and `deps = [":sipi_lib"]`. This split is mandatory infrastructure for Y+1's per-component unit tests; introducing it in Y avoids a cross-PR refactor of `src/BUILD.bazel` later. It also scaffolds the post-Y+6 module-co-located layout flip (see *Future Considerations*) — the eventual Y+8 series breaks `//src:sipi_lib` apart into per-module `cc_library` targets without re-touching `src/BUILD.bazel`'s public shape.
- `tools/workspace_status.sh` (executable) emitting `STABLE_GIT_COMMIT`, `STABLE_GIT_VERSION`, `STABLE_IMAGE_CREATED`, `STABLE_SIPI_VERSION` (the last reads `version.txt` — preserves release-please integration; see Versioning section)
- `expand_template(name = "sipi_version_h", template = "include/SipiVersion.h.in", out = "SipiVersion.h", stamp_substitutions = {...})` consumed by `//src:sipi`'s `cc_binary` (replaces today's CMake `configure_file()` flow; bakes VERSION into the binary)
- `.bazelrc` entries (Bazel 9): `build --incompatible_strict_action_env` (documented intent — Bazel 9 makes it default but the line stays as cache-hygiene anchor); `build --action_env=BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=1`; `build --workspace_status_command=tools/workspace_status.sh`; hardening neutralization block (`-U_FORTIFY_SOURCE`, `-fno-stack-protector`, `-fno-stack-clash-protection`, `-fno-pie`, `-no-pie` on link, plus host-copts) matching today's `flake.nix` `hardeningDisable = ["all"]`; `build:macos --copt=-mmacosx-version-min=12.0` + `--linkopt=-mmacosx-version-min=12.0` + `--action_env=MACOSX_DEPLOYMENT_TARGET=12.0`
- `bazelisk` added to `flake.nix`'s `devShells.{default, clang, fuzz, gcc}` package lists
- `perl` added to the same `devShells` package lists (host-tool requirement for openssl's `Configure` script — see `02-research-findings-bazel-implementation.md` §9)
- `cmake` added to the same `devShells` package lists (host-tool dep for `rules_foreign_cc` `cmake()` invocations on tiff, exiv2, lcms2, webp, jansson, sentry-native, prometheus-cpp, fmt, zstd, sqlite3 — `toolchains_llvm` does not bundle cmake)
- `pkg-config` added to the same `devShells` package lists (host-tool dep for `configure_make()` autotools libs that probe via pkg-config, e.g. curl finding openssl)
- `gh` moved from the Kakadu FOD's `nativeBuildInputs` into the regular `devShells` package list (the FOD goes away and the new `kakadu_archive` `repository_rule` invokes `gh` from PATH at fetch time)
- `cacert` added alongside `gh`, with `shellHook` exporting `SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt` — defensive parity with today's FOD (`flake.nix:62`); `gh`'s Go-based TLS needs an explicit cert bundle on Linux dev shells without `/etc/ssl/certs`

**Deletes.** Nothing. The Nix flake's outputs, `package.nix`, every `ext/<lib>/CMakeLists.txt`, the root `CMakeLists.txt`, and every `nix-*` justfile recipe stay untouched and authoritative.

**Acceptance.** A reviewer clones the branch, enters `nix develop`, runs `bazel build //src:sipi`, and runs `bazel-bin/src/sipi --config config/sipi.localdev-config.lua`. Sipi serves IIIF at port 1024. Hurl tests pass against the bazel-built binary. **All three platforms must build** per the build-completeness invariant: macOS-aarch64 (reviewer's machine), linux-x86_64 (CI runner), linux-aarch64 (CI runner). Build is verified on each via a dispatch-only CI workflow gated to the PR branch — Y itself does not yet make CI invoke Bazel for merge gating.

**Rollback.** Close the PR. Zero impact on `main`.

#### PR Y+1 ([DEV-6343](https://linear.app/dasch/issue/DEV-6343)) — Unit tests via Bazel

**Scope.** Port `test/unit/**/*.cpp` GoogleTest components and `test/approval/**/*.cpp` ApprovalTests to `cc_test` targets. Add `just bazel-test-unit` and `just bazel-test-approval` recipes. CI continues running the Nix-driven test path (`just nix-build` runs unit + approval tests in `checkPhase`).

**Adds.**
- `test/unit/<component>/BUILD.bazel` per component, each defining a `cc_test` with `deps = ["//src:sipi_lib", "@googletest//:gtest_main", ...]`. Components present at the time of writing: `sipiicc` (PR #587, ICC byte-mutation helper), plus the pre-existing GoogleTest dirs under `test/unit/`. The `//src:sipi_lib` `cc_library` (introduced in Y) is the linkage point — unit tests pull only the symbols they need rather than the full binary.
- `test/approval/BUILD.bazel` defining `cc_test(name = "approvaltests", env = {"SOURCE_DATE_EPOCH": "946684800"}, data = glob(["approval_tests/**", "_test_data/**"]), deps = ["//src:sipi_lib", "@approvaltests_cpp//:approval_tests", "@googletest//:gtest_main"], ...)`. The `env` injection mirrors the CMake `set_tests_properties(... ENVIRONMENT SOURCE_DATE_EPOCH=946684800 ...)` added in PR #587 — without it the new JPEG/PNG/JP2 goldens (15 tests post-#587) drift on every run because `SipiIcc::iccBytes()` falls back to lcms2's wall-clock-stamped header.
- `just bazel-test-unit` (wraps `bazel test //test/unit/...`) and `just bazel-test-approval` (wraps `bazel test //test/approval:approvaltests`) recipes.

**Deletes.** Nothing.

**Acceptance.**
- `just bazel-test-unit` exits green locally — including the `test/unit/sipiicc/icc_normalize_test` 8-case suite covering the ICC byte-mutation helper.
- `just bazel-test-approval` exits green locally with `SOURCE_DATE_EPOCH=946684800` injected; all 15 approval goldens (5 PR 0 TIFF + 10 added in PR #587) match bit-exactly. ApprovalTests `.received` files write under `bazel-testlogs/` (verified by deliberately corrupting one golden and confirming the failure surfaces with the expected diff).
- Test-count parity: `bazel test //test/unit/... //test/approval:approvaltests` reports the same number of test cases as `just nix-build`'s `checkPhase` on the pre-Y+1 main branch.

#### PR Y+2 ([DEV-6344](https://linear.app/dasch/issue/DEV-6344)) — Sanitized variant; CI cuts over

**Scope.** Implement `--config=asan` and `--config=ubsan` in `.bazelrc`. CI's sanitizer job switches its **build** step from `just nix-build-sanitized` to `just bazel-build-sanitized`. The **e2e run** in `sanitizer.yml` keeps invoking `just nix-test-e2e` against the Bazel-built `bazel-bin/src/sipi` binary (via `SIPI_BIN`); the e2e flow itself does not move to Bazel until Y+5. This split keeps Y+2 build-only — sanitizer-mode e2e tests still execute under the cargo/nix runner, so today's `LSAN_OPTIONS=suppressions=${{ github.workspace }}/.lsan_suppressions.txt` and `ASAN_OPTIONS=...:log_path=/tmp/asan-e2e` env vars and the sanitizer.yml post-processing step that scans `/tmp/asan-e2e.*` for `SUMMARY:` lines remain unchanged in mechanism.

**Adds.** `--config=asan` / `--config=ubsan` blocks in `.bazelrc`; `just bazel-build-sanitized` recipe; `.github/workflows/sanitizer.yml` updated only on the build step (line 64). `.lsan_suppressions.txt` stays at repo root unchanged.

**Deletes.** `pkgs.sipi.override { enableSanitizers = true; }` block (`flake.nix:323-329`); `just nix-build-sanitized` recipe; the corresponding `.#sanitized` package output.

**Acceptance.** Sanitizer CI job green with Bazel-built binary. ASan + UBSan reports identical signature to today's Nix-built sanitizer binary. `.lsan_suppressions.txt` still consulted (verify by deliberately removing one Lua suppression line; confirm the e2e step then surfaces the corresponding leak as a `SUMMARY:` line). Sanitizer.yml post-processing path (`/tmp/asan-e2e.*`) and the `SUMMARY:` grep are unchanged.

#### PR Y+3 ([DEV-6345](https://linear.app/dasch/issue/DEV-6345)) — Fuzz harness via second registered toolchain + `--config=fuzz`

**Scope.** Register a second `llvm_toolchain_fuzz` (libstdc++) sharing the downloaded LLVM via `llvm.toolchain_root`, gated by a `//tools/fuzz:fuzz_enabled` constraint. `--config=fuzz` flips the platform selector to `//tools/fuzz:linux_x86_64_fuzz`. Port `fuzz/handlers/iiif_handler_uri_parser_fuzz` to a `cc_binary` with `linkopts = ["-fsanitize=fuzzer"]`. CI's `fuzz.yml` workflow's *Build* and *Run* steps cut over to the new recipes; corpus persistence (the `fuzz-corpus` GitHub Actions artifact downloaded from the prior successful run via `gh api`) and crash-collection (the `crash-*` / `oom-* `/ `timeout-*` glob + `crash-summary.md` xxd writeup, uploaded as the `fuzz-crashes` artifact with 90-day retention) stay unchanged in mechanism — they operate on workspace `cwd`, which `bazel run` preserves.

**Adds.**
- `llvm.toolchain(name = "llvm_toolchain_fuzz", stdlib = {"": "stdc++"})` + `llvm.toolchain_root` + `llvm.extra_target_compatible_with` in `MODULE.bazel`
- `tools/fuzz/BUILD.bazel` declaring `constraint_setting`, `constraint_value`, and `platform(name = "linux_x86_64_fuzz")`
- `--config=fuzz` block in `.bazelrc` (`--platforms=//tools/fuzz:linux_x86_64_fuzz`, `--copt=-fsanitize=fuzzer-no-link`, `--copt=-fsanitize-coverage=trace-cmp` (matches today's `add_compile_options` block in `fuzz/handlers/CMakeLists.txt:31-36`), `--linkopt=-fsanitize=fuzzer`, `--copt=-fsanitize=address` + `--linkopt=-fsanitize=address` (today's fuzz target builds with ASan as well — `fuzz/handlers/CMakeLists.txt:41-43`))
- `fuzz/handlers/BUILD.bazel` defining:
  - `cc_library(name = "fuzz_subset", srcs = [<the 5 shttps files + iiif_handler.cpp listed in fuzz/handlers/CMakeLists.txt:18-30>], deps = ["@openssl//:ssl", "@openssl//:crypto"])` — a deliberate subset of `//shttps:shttps`, mirroring today's separate `lib_iiif_handler_uri_parser_fuzz` static lib that compiles only ChunkReader, Connection, Error, SockStream, plus `iiif_handler`. Linking the full `//shttps:shttps` under fuzz flags would pull symbols (Lua VM, prometheus, sentry) the harness doesn't exercise and inflates the corpus search-space cost. The subset stays explicit, not a `glob()`.
  - `cc_binary(name = "iiif_handler_uri_parser_fuzz", srcs = ["iiif_handler_uri_parser_target.cpp"], deps = [":fuzz_subset"], data = ["//fuzz/handlers/corpus:seed_corpus"], target_compatible_with = ["//tools/fuzz:fuzz_enabled"])` — the `data` dep makes the seed corpus available in runfiles for `bazel test` discovery, but `bazel run` resolves seed paths via `$BUILD_WORKSPACE_DIRECTORY` (set by Bazel for `bazel run`) so today's `fuzz/handlers/corpus` workflow path keeps working.
  - `filegroup(name = "seed_corpus", srcs = glob(["corpus/**"]))` (in `fuzz/handlers/corpus/BUILD.bazel`) — the committed seed corpus stays at `fuzz/handlers/corpus/`, just exposed as a Bazel target.
- `just bazel-build-fuzz` and `just bazel-run-fuzz <corpus> <duration> <seed>` recipes — the recipe interface preserves today's `just nix-run-fuzz <corpus> <duration> <seed>` argument triple verbatim, so `fuzz.yml`'s `Run fuzzer` step (`just nix-run-fuzz fuzz-corpus-live "$FUZZ_DURATION" fuzz/handlers/corpus`) needs only the recipe-name swap.
- `fuzz.yml` workflow updates: line 81 (`just nix-build-fuzz` → `just bazel-build-fuzz`); line 101 (`just nix-run-fuzz` → `just bazel-run-fuzz`). All other steps (`Find last successful fuzz run`, `Restore corpus from previous run`, `Prepare live corpus`, `Save corpus for next run`, `Collect crash details`, `Upload crash artifacts`) are unchanged — they manipulate workspace files, not Bazel artifacts. Both `fuzz-corpus` and `fuzz-crashes` GitHub artifact names + retentions are preserved.

**Deletes.** `.#fuzz` package output and its `overrideAttrs` block (`flake.nix:335-371`); the libstdc++ stdenv override gymnastics; `just nix-build-fuzz`, `just nix-run-fuzz` recipes; `fuzz/CMakeLists.txt`; `fuzz/handlers/CMakeLists.txt`.

**Acceptance.**
- `just bazel-run-fuzz fuzz-corpus-live 60 fuzz/handlers/corpus` produces libFuzzer output identical in shape to today's Nix-based fuzz invocation (header line with `INFO: Seed:`, `INFO: Loaded N modules`, periodic `#NN pulse cov:`/`exec/s:` lines).
- Fuzz CI job green on a manual `workflow_dispatch` run with `duration=60`. Verify: corpus restore from prior run succeeds (sanity check `fuzz-corpus-live/` non-empty before the fuzz step); corpus upload from this run succeeds (`fuzz-corpus` artifact size grows or stays flat); on a deliberately-broken seed input that triggers a known crash signature, `fuzz-crashes` artifact uploads with `crash-summary.md` containing the reproducer bytes via `xxd`.
- The second toolchain shares the downloaded LLVM artifact (no re-fetch — verified by inspecting `bazel-out/external/` after a clean fetch).
- Coverage instrumentation present: `objdump -t bazel-bin/fuzz/handlers/iiif_handler_uri_parser_fuzz | grep -c sancov` returns non-zero.

#### PR Y+4 ([DEV-6346](https://linear.app/dasch/issue/DEV-6346)) — Docker image via `rules_oci`

**Scope.** Replace `dockerTools.{buildLayeredImage, streamLayeredImage}` with `oci_image` + per-arch `oci_push` + `crane index append`. The sipi-side image ships HEALTHCHECK-agnostic (HEALTHCHECK is a Docker extension to OCI, not part of the spec). The compose-level healthcheck stanza is **not** part of Y+4's scope — it lands independently as [INFRA-1226](https://linear.app/dasch/issue/INFRA-1226), which must be deployed to staging + prod before Y+4 merges (see *Dependencies & Prerequisites*).

**Adds.**
- `rules_oci`, `rules_distroless`, `@tar.bzl` in `MODULE.bazel` (`aspect_bazel_lib` was already added in Y for `expand_template` / `SipiVersion.h`)
- `//src:image` `oci_image` (no `healthcheck`); per-arch `//src:image_push_amd64` and `//src:image_push_arm64` `oci_push`
- `//src:nss_layer` from `rules_distroless`'s `passwd()` and `group()` macros + a hand-written `nsswitch.conf` matching today's `dockerTools.fakeNss`
- `//src:debug_info` `genrule` invoking `llvm-objcopy` + `llvm-readelf`, producing the `.debug` file under `lib/debug/.build-id/<xx>/<yy>.debug`. The `linkopts = ["-Wl,--build-id=sha1"]` on `//src:sipi`'s `cc_binary` was already added in Y; Y+4 only adds the post-processing `genrule`
- `expand_template` rules for `image_created` and `image_labels` consuming `STABLE_GIT_COMMIT`, `STABLE_GIT_VERSION`, `STABLE_IMAGE_CREATED` via `stamp_substitutions` (`tools/workspace_status.sh` was already added in Y; the script's keys are reused here, not duplicated)
- `oci_pull` for the `distroless_base` image, pinned by `digest =` (not `tag =`)
- `just bazel-docker-build-amd64`, `just bazel-docker-build-arm64`, `just bazel-docker-publish-manifest` (wraps `crane index append`), `just bazel-docker-extract-debug` recipes

**Deletes.** `dockerTools.{buildLayeredImage, streamLayeredImage}` blocks (`flake.nix:184-289`); `mkDockerImage`, `imageTag`, `imageCreated`, `sipiForImage` helpers; `.#docker`, `.#docker-stream`, `.#sipi-debug` outputs; `just nix-docker-build*` and `just nix-docker-extract-debug` recipes.

**Acceptance.**
- Docker image SHA-256 reproducible across two consecutive builds with the same `MODULE.bazel.lock` + same source (verify via `diffoci`).
- `daschswiss/sipi:v<version>` multi-arch manifest pushed via `crane index append`.
- **Sentry debug symbols**: `just bazel-docker-extract-debug ${arch}` produces `sipi-${arch}.debug` at the path `publish.yml`'s existing `sentry-cli debug-files upload` step expects (filename unchanged from today). Verified by dispatch-only CI workflow before Y+4 merges; `sentry-cli` exits 0.
- **Docker Scout — compare**: PR-comment job runs against the Bazel-loaded `local://daschswiss/sipi:latest` image and produces the same CVE-diff comment shape as today.
- **Docker Scout — CVE SARIF**: `scout-results.sarif` is generated and uploaded to GitHub Security via `codeql-action/upload-sarif`; alerts visible in the Security tab.
- Smoke tests (`test/e2e-rust/tests/docker_smoke.rs`) pass against the Bazel-built image — note that smoke tests still run via Nix-built `.#smoke-test` until Y+5.
- Swarm `docker service ps {{ STACK }}_iiif` reports `Healthy` under the new compose-level healthcheck.

#### PR Y+5 ([DEV-6347](https://linear.app/dasch/issue/DEV-6347)) — Rust e2e + smoke tests via `rules_rust`

**Scope.** Replace `crane`-based `nix/rust-tests.nix` with `rules_rust` + Crate Universe consuming `test/e2e-rust/Cargo.lock`. The current crane setup produces ~22 e2e test binaries (one per `tests/<name>.rs`) plus one feature-gated `docker_smoke` binary; the Bazel migration preserves that one-binary-per-file mapping (`rust_test` semantics: one target = one Cargo `[[test]]` = one binary). Use `crate.from_cargo` (lazy resolution from `Cargo.lock`) — not `crates_vendor`. Repin via `CARGO_BAZEL_REPIN=1 bazel sync --only=crates`.

**Crate layout (today, `test/e2e-rust/`).** Single non-published crate `sipi-e2e` (not a workspace). Has both:
- `src/lib.rs` exporting shared helpers (`SipiServer` process manager, port allocator, `repo_root()` resolver) consumed by every test binary.
- 22 integration-test files in `tests/<name>.rs`: `bilevel_tiff`, `cache`, `cli`, `cli_json`, `config`, `connection`, `docker_smoke`, `health`, `heritage_jpeg`, `iiif_compliance`, `input_validation`, `latency`, `memory_budget`, `proptest_iiif_uri`, `range_requests`, `rate_limiter`, `resource_limits`, `security`, `server`, `shutdown`, `smoke`, `upload`.
- A shared module `tests/common/mod.rs` (conventional Cargo pattern).
- A single `[features] docker = []` flag gating `docker_smoke.rs` (top of file: `#![cfg(feature = "docker")]`).

**Ordering constraint.** In `MODULE.bazel`, register the LLVM toolchain (Y) *before* the Rust extension is invoked — `cargo_build_script` resolves `cc` from the registered cc_toolchain at materialization time, and a misordered registration silently picks up the host `cc`.

**Cargo.toml `*-sys` audit (already done; documented for review).** The current crane setup needs `cmake`, `perl`, `pkg-config`, `openssl`, and `LIBCLANG_PATH` at build time (`nix/rust-tests.nix:21-30`). Tracing back to the Cargo.toml dependencies:

| Crate | `*-sys` build-script behavior | Action in Y+5 |
|---|---|---|
| `jsonwebtoken[aws_lc_rs]` | `aws-lc-sys` builds aws-lc via cmake (canonical Crate Universe pain point) | `crate.annotation()` configuring `build_script_env` + `additional_build_script_data` for `aws-lc-sys`. Pre-Y+5 spike validates. Escape hatch: switch jsonwebtoken to a pure-Rust backend (`rust_crypto` feature) — code-level change to JWT verification, requires regression test. |
| `reqwest` (default features) | Pulls `default-tls` → `native-tls` → `openssl-sys` (Linux) / system Security framework (macOS) | Pin `reqwest = { version = "0.12", default-features = false, features = ["blocking", "json", "multipart", "rustls-tls"] }` to drop `openssl-sys` entirely. Pure-Rust + `ring`. |
| `image` | Mostly pure Rust; format-specific build scripts | No action expected; spot-check Crate Universe build. |
| `nix` (`0.29`) | Has small `build.rs` probing target features | No action expected; environment-agnostic. |

Native-build-tool host deps **already in flake `devShells` after Y**: `bazelisk`, `perl`, `cmake`, `pkg-config`, `gh`, `cacert`. Y+5 adds nothing new to `devShells` — `aws-lc-sys`'s `cmake` invocation reuses the `cmake` already present from Y's foreign_cc setup. `LIBCLANG_PATH` is exposed via `.bazelrc`'s `--action_env=LIBCLANG_PATH=...` resolved through the registered LLVM toolchain so bindgen can find libclang.

**Test-runtime contracts to preserve** (from today's `nix/rust-tests.nix:139-154` and `src/lib.rs`):
- `SIPI_BIN` env var → consumed by `SipiServer` to locate the binary under test. In Bazel: `env = {"SIPI_BIN": "$(rootpath //src:sipi)"}` on each e2e `rust_test` (uses Bazel runfiles resolution); `data = ["//src:sipi"]`.
- `SIPI_REPO_ROOT` env var → consumed by `sipi_e2e::repo_root()` to find test fixtures (cannot use `env!("CARGO_MANIFEST_DIR")` because that resolves to the Bazel sandbox). In Bazel: `env = {"SIPI_REPO_ROOT": "$(rootpath :test_fixtures)"}` with `:test_fixtures` a `filegroup` over `test/`, `config/`, etc. — paths sipi fixtures live under.
- `--test-threads=1` mandatory → sipi can't handle parallel test load. In Bazel: `args = ["--test-threads=1"]` on every e2e `rust_test`. Also `tags = ["exclusive"]` to prevent cross-target parallel execution at Bazel level.
- `CARGO_PROFILE=test` pin → release-profile binaries hammer sipi too fast and cause flakiness on `iiif_compliance` / `resource_limits`. In Bazel: `rust_test`'s default `--compilation_mode=fastbuild` is dbg-equivalent and matches the historical `cargo test`-default. Pin `--compilation_mode=fastbuild` for all e2e targets via `.bazelrc`'s `test --compilation_mode=fastbuild` if there's any drift risk.
- **Sanitizer-mode env (`LSAN_OPTIONS`, `ASAN_OPTIONS`)** → today's `sanitizer.yml:70-71` sets these on the workflow step running `just nix-test-e2e`. Under Bazel post-Y+5, those env vars must propagate into the test action *and* the suppressions file path must resolve inside Bazel's runfiles sandbox (the workflow's `${{ github.workspace }}/.lsan_suppressions.txt` does not exist inside the sandbox). Three pieces:
  1. **Suppressions file as runfiles data dep.** `:lsan_suppressions = filegroup(srcs = [".lsan_suppressions.txt"])` at repo root (or `//:lsan_suppressions`). Every e2e `rust_test` macro `data`s it: `data = [..., "//:lsan_suppressions"]`. Cheap (~80 B) and unconditional — the file only takes effect when LSan is on, so non-sanitizer runs are unaffected.
  2. **`LSAN_OPTIONS` injection.** The `sipi_e2e_test()` macro sets `env = {"LSAN_OPTIONS": "suppressions=$(rootpath //:lsan_suppressions)"}` (Bazel resolves `$(rootpath ...)` to the runfiles-relative path at action time). Sanitizer.yml's workflow-level `LSAN_OPTIONS` env var is **deleted** — Bazel owns it now.
  3. **`ASAN_OPTIONS` log-path migration.** Today: `ASAN_OPTIONS=...:log_path=/tmp/asan-e2e`; sanitizer.yml grep scans `/tmp/asan-e2e.*`. Under Bazel sandbox, `/tmp` is path-isolated. Switch to `log_path=$TEST_UNDECLARED_OUTPUTS_DIR/asan-e2e` (Bazel guarantees this env var inside test actions and surfaces files via `bazel-testlogs/.../test.outputs/`). Sanitizer.yml's post-processing step is updated: glob changes from `/tmp/asan-e2e.*` to `bazel-testlogs/test/e2e-rust/*/test.outputs/asan-e2e.*`. Workflow-level `ASAN_OPTIONS` keeps `detect_leaks=1:halt_on_error=0` but the `log_path=...` portion is moved into the asan `.bazelrc` config (`test:asan --test_env=ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:log_path=$TEST_UNDECLARED_OUTPUTS_DIR/asan-e2e`).
  4. **Sanitizer.yml e2e step rewrite.** Replace `just nix-test-e2e` with `just bazel-test-e2e --config=asan` (or equivalent). Drop the now-obsolete `LSAN_OPTIONS` env var on the step. Update `Check for sanitizer findings in e2e` and `Upload sanitizer reports` to use the new glob.

**Adds.**
- `rules_rust` `bazel_dep` in `MODULE.bazel`
- `rust = use_extension("@rules_rust//rust:extensions.bzl", "rust")` + `rust.toolchain(edition = "2021", versions = ["..."])` pinned to whatever current `Cargo.lock` resolved against
- `crate = use_extension("@rules_rust//crate_universe:extensions.bzl", "crate")` + `crate.from_cargo(name = "crates", cargo_lockfile = "//test/e2e-rust:Cargo.lock", manifests = ["//test/e2e-rust:Cargo.toml"])`
- `crate.annotation(crate = "aws-lc-sys", ...)` configuring its build-script env and data deps so the cmake invocation succeeds inside Bazel actions (reuses the `cmake` host-tool dep already added in Y)
- `test/e2e-rust/BUILD.bazel`:
  - `rust_library(name = "sipi_e2e", srcs = ["src/lib.rs"], deps = all_crate_deps())` for the shared helpers
  - 21 × `rust_test(name = "<test_name>", srcs = ["tests/<test_name>.rs", "tests/common/mod.rs"], deps = [":sipi_e2e", ...])` for the non-docker e2e tests
  - 1 × `rust_test(name = "docker_smoke", srcs = ["tests/docker_smoke.rs", "tests/common/mod.rs"], crate_features = ["docker"], deps = [":sipi_e2e", ...], data = [":sipi_image_tar", "//src:sipi"], env = {"SIPI_IMAGE_TAR": "$(rootpath :sipi_image_tar)", "SIPI_IMAGE_TAG": "sipi:e2e", "SIPI_BIN": "$(rootpath //src:sipi)"}, tags = ["requires-network", "requires-docker", "exclusive"])` consuming the Bazel-built `oci_load` tarball
  - `:sipi_image_tar` `filegroup` extracting the OCI tarball from the `oci_load` target's `tarball` output group (per `rules_oci/docs/rust.md`)
  - `:test_fixtures` `filegroup` aggregating `test/`, `config/` paths the e2e tests resolve via `SIPI_REPO_ROOT`
  - Common `env`/`data`/`args`/`tags` factored into a small Starlark macro `sipi_e2e_test()` to keep the BUILD readable across 21 targets
  - `:all_e2e` `test_suite` aggregating the 21 non-docker `rust_test` targets
- Cargo.toml change: pin `reqwest` features as documented above
- `just bazel-test-e2e` (wraps `bazel test //test/e2e-rust:all_e2e`) and `just bazel-test-smoke` (wraps `bazel test //test/e2e-rust:docker_smoke`) recipes
- CI workflows updated: e2e job runs in parallel with sipi binary build; smoke job runs after Docker build

**Deletes.** `nix/rust-tests.nix` (entire file: `mkTestBinaries`, `runE2eDriver`, `e2eTests`, `smokeTest`, `LIBCLANG_PATH` and `CARGO_PROFILE` overrides); `crane` flake input; `.#e2e-tests`, `.#smoke-test` package outputs; `just nix-test-e2e`, `just nix-test-smoke` recipes.

**Acceptance.**
- All 21 non-docker e2e `rust_test` targets pass against the Bazel-built `:sipi` binary.
- `docker_smoke` `rust_test` passes against the Bazel-built `oci_image` (consumed via `oci_load` → tarball → runfiles).
- **Test-count parity**: `bazel test //test/e2e-rust/...` runs the same set of test cases as today's `just nix-test-e2e` + `just nix-test-smoke`. Verify by counting reported test cases on both sides pre-merge.
- No flakiness regression on the high-load tests (`iiif_compliance`, `resource_limits`, `latency`) attributable to compile-mode change. Run each high-load target three times back-to-back to detect flakiness; all three runs must pass.
- **Sanitizer-mode e2e green end-to-end.** `bazel test //test/e2e-rust/... --config=asan` runs all e2e targets under ASan + UBSan; `.lsan_suppressions.txt` is consulted (verify by deliberately removing one Lua suppression line; confirm a corresponding `SUMMARY:` finding surfaces). Sanitizer.yml's `Check for sanitizer findings in e2e` step finds zero `SUMMARY:` lines on a clean run via the updated `bazel-testlogs/.../test.outputs/asan-e2e.*` glob. The `Upload sanitizer reports` step uploads the same artifact-name (`sanitizer-reports`) on failure with the new path.

#### PR Y+6 ([DEV-6348](https://linear.app/dasch/issue/DEV-6348)) — Default + release variants; CI fully on Bazel

**Scope.** Final cutover. CI no longer invokes `nix build` for any artifact.

**Adds.** `just bazel-build` (`-c dbg` + coverage), `just bazel-build-default` (`-c opt` with debug symbols), `just bazel-build-release` (`-c opt --strip=always`), `just bazel-coverage` (`bazel coverage --combined_report=lcov --instrumentation_filter='//src,//shttps'`; emits lcov consumed directly by `codecov/codecov-action@v6` — no cobertura conversion needed).

**Deletes.** `package.nix` (entire ~246 lines); `pkgs.sipi` attribute and overlay; `.#default`, `.#dev`, `.#release` outputs; `kakaduArchive` FOD and `mkKakaduArchive` helper; `just nix-build`, `just nix-build-default`, `just nix-build-release`, `just nix-coverage` recipes; the `crane`-related leftovers in `flake.nix`.

**Acceptance.**
- All CI jobs green. No CI workflow invokes `nix build`.
- **Coverage upload to Codecov continues to work**: `codecov/codecov-action@v6`'s `files:` parameter updated to point at Bazel's lcov output; coverage percentage stays within ±2% of pre-Y+6 baseline (validates `instrumentation_filter` scope).
- **Version stamping verified end-to-end**: throwaway branch bumps `version.txt` to a sentinel value, `bazel build //src:sipi` produces a binary whose `--version` output matches the sentinel, `git status` is clean (stamping never writes back to source), and the release-please workflow run on the merge commit creates the corresponding git tag without manual intervention.
- **All side-channel uploaders intact** (Codecov, Docker Scout compare, Docker Scout CVE SARIF, Docker Scout record production, Docker Scout SBOM, Sentry debug-files, Sentry release notification, mkdocs site deploy) — verified by diffing `.github/workflows/{ci,publish}.yml` and confirming only `just nix-*` → `just bazel-*` and the Codecov `files:` parameter changed.

#### PR Y+7 ([DEV-6349](https://linear.app/dasch/issue/DEV-6349)) — Cleanup

**Scope.** Pure deletions and documentation updates.

**Adds.** Nothing.

**Deletes.**
- `flake.nix` reduced to ~50 lines containing only `devShells.{default, clang, fuzz, gcc}`. Possible further collapse: if `--config=fuzz` made the separate `fuzz` shell redundant, consolidate to two shells (`default`, `gcc`).
- `iiif-validator.nix` removed if unused (verify via `grep -r iiif-validator` first).
- `vendor/` flow eliminated: `just vendor-download`, `just vendor-verify`, `just vendor-checksums`, `just kakadu-fetch` recipes deleted. `vendor/` directory removed from `.gitignore` if no longer needed.
- `cmake/dependencies.cmake` removed (its single-source-of-truth role is absorbed by `MODULE.bazel`).
- Every `ext/<lib>/CMakeLists.txt` removed. Root `CMakeLists.txt` removed. The `cmake/Modules/` directory removed if empty.
- `CLAUDE.md` "build reproducibility invariant" reworded to refer to Bazel. **Preserve** the separate "ICC determinism invariant" callout added by PR #587 — the rewording targets the build-tool invariant only; the ICC opt-in (`SOURCE_DATE_EPOCH` test-only contract) is independent and must survive verbatim.
- `docs/src/development/building.md` rewritten for Bazel-as-build, Nix-as-environment.
- `docs/src/development/developing.md` updated: inner-loop is `bazel build //src:sipi`, not `cmake --build build`. macOS dev workflow for local Linux builds documents OrbStack escape hatch.
- `docs/src/development/nix.md` reduced to dev-shell setup only; build-related content moved to a new `docs/src/development/bazel.md`.
- `docs/src/development/ci.md` updated to reflect Bazel CI flow.
- `docs/src/development/kakadu.md` updated: Kakadu now fetched by a custom `kakadu_archive` `repository_rule` invoking `gh release download` (auth UX preserved 1:1 with today's FOD). Local-dev: `gh auth login` once. CI: `GH_TOKEN=${{ secrets.DASCHBOT_PAT }}`. The `just kakadu-fetch` step is gone.
- `REVIEW.md` references to Nix-build-related concerns updated to reference Bazel equivalents.

**Acceptance.** `find . -name '*.nix' | wc -l` returns ≤ 4 (root flake.nix, flake.lock, possibly two shell-helper files). `find . -name 'CMakeLists.txt' | wc -l` returns 0. Documentation accurately reflects the post-migration build flow. New developer onboarding (per `docs/src/development/building.md`) takes a developer from clone to running Sipi binary using only `nix develop` + `bazel build` — no Claude Code required. **`docs-build` CI job green** (`ci.yml`'s mkdocs verification step) — Y+7's markdown rewrites must not break mkdocs strict-mode build or any cross-link; the existing per-PR `docs-build` job is the gate.

## Acceptance Criteria

### Functional Requirements

- [ ] **Y:** `bazel build //src:sipi` produces a binary that passes the existing Hurl test suite on macOS-aarch64 and linux-x86_64.
- [ ] **Y+1:** `bazel test //test/unit/...` passes with the same set of approved test outcomes as today's Nix-driven unit tests.
- [ ] **Y+2:** `just bazel-build-sanitized` produces an ASan + UBSan binary; sanitizer CI job replaces the Nix-driven equivalent.
- [ ] **Y+3:** `just bazel-run-fuzz` runs the libFuzzer harness with libstdc++ ABI; fuzz CI job replaces the Nix-driven equivalent.
- [ ] **Y+4:** `daschswiss/sipi:v<version>` multi-arch manifest pushed by Bazel; image SHA reproducible; smoke tests pass.
- [ ] **Y+5:** `bazel test //test/e2e-rust/...` runs all 22 e2e test binaries (21 non-docker via `:all_e2e` test_suite + 1 `docker_smoke` against the Bazel-built `oci_image`). Test-case count matches `just nix-test-e2e` + `just nix-test-smoke` on the pre-Y+5 main branch.
- [ ] **Y+6:** Zero CI workflow invokes `nix build`. `package.nix` deleted.
- [ ] **Y+7:** `flake.nix` ≤ 60 lines and contains only `devShells`. Documentation updated.

### Non-Functional Requirements

- [ ] **Reproducibility:** Same `MODULE.bazel.lock` + same source = same `:sipi` binary SHA-256 (verified by two independent CI runs). Same inputs → same OCI tarball SHA-256.
- [ ] **Build completeness:** Every variant builds on macOS-aarch64, linux-x86_64, linux-aarch64. Linux-only variants (`docker_image`) gated by the existing platform check.
- [ ] **Inner-loop performance:** `bazel build //src:sipi` after a single-file edit completes in < 30s (incremental). First-build cold-cache is allowed to be slow.
- [ ] **CI duration:** End-to-end CI (PR build + tests + Docker + e2e + smoke) within ±20% of today's Nix-driven CI duration. Re-evaluated at Y+4 (Docker) and Y+6 (full cutover).
- [ ] **Cache poisoning prevention:** `--incompatible_strict_action_env` set on every CI invocation. Manually verified by injecting a synthetic env var and confirming cache hits unchanged.

### Quality Gates

- [ ] Each PR Y through Y+7 reviewed by the eng team and merged sequentially.
- [ ] No PR in the sequence regresses test coverage as reported by Codecov.
- [ ] No PR merges with red CI.
- [ ] Y+7 includes a follow-up "lessons learned" learning under `dasch-specs/learnings/best-practices/` capturing what surprised us.

## Risk Analysis & Mitigation

Risks reframed in light of `02-research-findings-bazel-implementation.md`. Risks the research **eliminated or downgraded** are noted explicitly so reviewers can see the chain of reasoning. The full risk catalog (with citations) lives in the research findings doc; this is the operating subset.

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `rules_foreign_cc` cannot replicate `openssl` configure flags | L | L | *Downgraded from M/M.* Pattern documented in research findings §1 (`configure_command = "Configure"` (capital C), `configure_in_place = True`, per-arch target token in `configure_options` via `select()`). Upstream `rules_foreign_cc` has an openssl example. Pre-Y spike confirms before Y opens. |
| Kakadu Linux Makefile patches don't apply cleanly via `http_archive` `patches=[]` | L | L | *Downgraded from M/H.* `http_archive` `patches=[]` + `patch_args=["-p1"]` is canonical; patches apply in list order. Pre-Y spike validates. Fallback: `genrule` invoking `patch -p1` as part of a wrapper srcs target. |
| `rules_oci` image is not byte-reproducible | L | L | *Downgraded from L/M.* `STABLE_*` keys + `expand_template` + `stamp_substitutions` + `oci_pull(digest = ...)` is canonical. Verified via `diffoci`. `--build-id=sha1` for content-addressed build-IDs. |
| `rules_rust` + Crate Universe friction | L | M | *Downgraded from M/M.* `crate.from_cargo` consumes `Cargo.lock` directly. `CARGO_BAZEL_REPIN=1 bazel sync --only=crates` is the canonical workflow. `reqwest` pinned to `default-features = false, features = ["rustls-tls"]` to avoid `openssl-sys`. |
| Linux libc++ hermeticity requires Chromium sysroot | M | M | Pattern in research findings §4. Ship Chromium debian sysroot via `sysroot()` repo rule + `llvm.sysroot()` extension call. Escape hatch: `cerisier/toolchains_llvm_bootstrapped` if sysroot management becomes painful. |
| `--config=fuzz` requires second registered toolchain (cannot be `.bazelrc` only) | L | L | Pattern in research findings §5. Y+3 lands ~30 lines of `MODULE.bazel` + `tools/fuzz/BUILD.bazel` + `.bazelrc`. Modern libFuzzer ships private libc++ — eligible for simplification later. |
| `oci_image_index` cannot consume external digests | L | L | Coordinator job uses `crane index append` instead. Pattern in research findings §1. CI matrix unchanged. |
| `oci_image` has no `healthcheck` attribute (HEALTHCHECK is a Docker extension to OCI, not part of the spec); ops-deploy uses Docker Swarm | L | L | Move HEALTHCHECK to compose-level `healthcheck:` stanza in `docker-compose-iiif.yml.j2` — tracked on infra side as [INFRA-1226](https://linear.app/dasch/issue/INFRA-1226), must land before Y+4. Pattern already established for the `db` service. |
| `cargo_build_script` for `*-sys` crates breaks Crate Universe | H | M | `aws-lc-sys` (via `jsonwebtoken[aws_lc_rs]`) is the dominant case — needs `crate.annotation()` with `build_script_env` + `additional_build_script_data`. `reqwest` pinned to `rustls-tls` to drop `openssl-sys`. Pre-Y+5 spike validates `aws-lc-sys` builds inside Bazel actions; if it fails, switch jsonwebtoken to `rust_crypto` backend (code-level change, requires JWT verification regression test). Order constraint: register LLVM toolchain *before* Rust extension. |
| `--test-threads=1` requirement breaks under Bazel's per-target parallelism | M | M | Each e2e `rust_test` carries `args = ["--test-threads=1"]` and `tags = ["exclusive"]`. Verified via three consecutive runs of `iiif_compliance`/`resource_limits`/`latency` on the Bazel side; flakiness regression blocks merge. |
| `SIPI_BIN` / `SIPI_REPO_ROOT` env-var contracts break under Bazel runfiles layout | M | L | `data = ["//src:sipi", ":test_fixtures"]` + `env = {"SIPI_BIN": "$(rootpath ...)", "SIPI_REPO_ROOT": "$(rootpath ...)"}` on every e2e `rust_test`. Common pattern factored into a `sipi_e2e_test()` Starlark macro to prevent drift across 21 targets. Pre-Y+5: validate `SipiServer` startup works under runfiles (one test target spike). |
| openssl `Configure` requires host Perl | H | L | Add `perl` to `flake.nix` `devShells` in Y. Document as host-tool requirement. |
| Each `rules_foreign_cc` rule is a single Bazel action | H | L | Tight `lib_source` glob excluding tests/docs (largest cache-stability win). `set_file_prefix_map = True` for cache portability across machines. Document `bazel clean` footgun for partial-build state recovery. |
| Hardening defaults conflict with foreign_cc deps | M | L | `.bazelrc` neutralization block in Y matching today's `hardeningDisable = ["all"]` (research findings §12). Not via `compile_flags` override on `llvm.toolchain` (replaces, doesn't append). |
| macOS depends on Xcode CLT SDK — not "fully hermetic" | L | L | Pin `MACOSX_DEPLOYMENT_TARGET=12.0` in `.bazelrc`. Document as a macOS requirement (Xcode CLT install is mandatory). |
| `rules_oci` 2.3.x not in maintainer Bazel 9 CI matrix (community-reports-working only) | M | M | Pre-Y spike: hello-world `oci_image` on Bazel 9.0.2 before Y opens (0.5 day). On failure: escalate before committing to Y. Y+4 doubles as the real validation. |
| `rules_rust#3817` — Bazel 9 module-extension splicing penalty (~30s no-op rebuild) | H | L | Cosmetic for inner-loop performance; affects Y+5 onward. Tracked upstream, expected fix in `rules_rust` 0.59.x+. Document as a known limitation in `docs/src/development/developing.md`. |
| `bazel#23460` — `parse_headers` incompatible with C `copts` (mixed C/C++ in foreign_cc deps) | M | L | Enable `features = ["layering_check", "parse_headers"]` only on first-party `//src` and `//shttps` packages in Y+8e; **never** on `//ext/<lib>` foreign_cc rules. The bounded-context enforcement only needs first-party coverage to deliver value. |
| `bazel#21592` — `layering_check` sandbox interaction (closed but unverified on 9.x) | L | L | Y+8e enables `layering_check` initially with `--spawn_strategy=sandboxed` and validates on a clean checkout. If broken, defer to `--features=` per-target rather than `.bazelrc` global until upstream patches. |
| GHA cache via `setup-bazel` exceeds 10 GB and starts evicting | M | L | Phase 1 has no remote cache; if eviction degrades CI, the deferred remote-cache decision becomes urgent. Trigger: CI cold-cache duration > 30min sustained. |
| Reviewer fatigue on Y's ~1500-line PR | H | M | Single big-bang Y is the user's deliberate choice (rejected three-way split) for revert simplicity. Pre-Y reviewer walkthrough on a single foreign_cc lib so review focuses on architecture, not pattern repetition. |
| Team Bazel skill atrophies between PRs | M | M | Each PR's commit message includes a short "Bazel concept of the week" note. Y+7 includes a post-mortem learning. |
| `dasch-specs` Phase 3 cleanup of the three `2026-04-16-sipi-nix-unified-build/` plans is missed | L | L | Y+7 explicitly updates the status of those plans to `superseded` with a pointer to this plan. |
| Sipi's release-please flow breaks because `version.txt` is no longer baked via Nix derivation | L | H | release-please config unchanged (it is build-tool-agnostic at `release-type: simple`). `tools/workspace_status.sh` emits `STABLE_SIPI_VERSION` from `version.txt`; `expand_template` substitutes it into the existing `SipiVersion.h.in` template, replacing the CMake `configure_file()` flow. See plan §"Versioning and release-please" for full mechanism. Y+6 verification: bump `version.txt`, run `bazel build //src:sipi`, run `./bazel-bin/src/sipi --version`, expect new value; confirm no untracked changes in `git diff`. |
| Custom `kakadu_archive` `repository_rule` fails in CI due to `gh` PATH or `DASCHBOT_PAT` misconfiguration | L | H | Y validates via dispatch-only CI workflow before Y is merged. `gh` is in `flake.nix` `devShells`; CI runs Bazel inside `nix develop` so `gh` resolves. `GH_TOKEN=${{ secrets.DASCHBOT_PAT }}` is set on the Bazel job env (same as today's FOD). Pre-Y dry run on a CI runner confirms `repository_rule` resolves before Y opens. |

## Dependencies & Prerequisites

- **No external dependencies.** All decisions are within the team's control.
- **Pre-Y prerequisites (must merge before Y opens):**
  - **[DEV-6339](https://linear.app/dasch/issue/DEV-6339) — `shttps::Server` → `SipiMetrics` inversion.** Standalone sipi PR on the existing CMake build that rehomes the four `SipiMetrics::instance()` call sites in `shttps/Server.cpp` behind a `shttps::ConnectionMetrics` observer interface owned by shttps; SIPI installs a concrete adapter at startup. ~150 LoC, 0.5–1 day. Required so Y can enable `layering_check` on `//shttps:shttps` as a verification gate (see *Architectural-boundary enforcement at the build-graph layer*). The full implementation plan lives on the Linear issue.
- **Pre-Y validation work:**
  - Spike Kakadu's Linux ARM64 Makefile patching as a `make()` rule on a throwaway branch before Y opens. Time-box: 1 day. If it fails completely, escalate before committing to Y.
  - Spike `rules_oci` 2.3.x against Bazel 9.0.2 — minimal hello-world `cc_binary` wrapped in an `oci_image` and pushed to a throwaway registry tag. Time-box: 0.5 day. If broken, escalate before committing to Y.
- **Knowledge prerequisites:** the implementing developer needs working knowledge of `bazel`, `MODULE.bazel`, `rules_foreign_cc`, `rules_oci`, `rules_rust`, and `toolchains_llvm`. The user has prior Bazel experience from a removed dsp-api Bazel attempt; that experience is the floor, not the ceiling. AI assistance is permitted (and expected) during writing; team review must be non-AI.
- **No infrastructure dependencies:** no new GitHub repos, no new GHA secrets (`DASCHBOT_PAT` already exists), no new external services.
- **Linear tracking:** the migration is tracked on Linear under umbrella issue [DEV-6341](https://linear.app/dasch/issue/DEV-6341). Each of the 8 phases (Y..Y+7) has a self-contained sub-issue ([DEV-6342](https://linear.app/dasch/issue/DEV-6342) through [DEV-6349](https://linear.app/dasch/issue/DEV-6349)) executable from Linear alone. Pre-Y prerequisites: [DEV-6339](https://linear.app/dasch/issue/DEV-6339) (shttps→SipiMetrics rehome, blocks Y); [INFRA-1226](https://linear.app/dasch/issue/INFRA-1226) (compose-level HEALTHCHECK, blocks Y+4). Linear blocking relationships chain Y → Y+1 → ... → Y+7. **Progress is tracked on Linear; this plan stays authoritative for design.**

## Future Considerations

These are explicit follow-ups, **out of scope for this plan**, ordered by likely sequencing:

- **Cross-compilation** via `toolchains_llvm` + Linux sysroot. Enables `bazel build //src:sipi --platforms=//platforms:linux_x86_64` from macOS hosts. Configure-script `AC_TRY_RUN`-style cross-friction on `openssl` / `curl` / `libmagic` / `jbigkit` is the known risk. Expected effort: 1 week per resistant lib. Deferred until after the Y+8 layout-flip series is stable; not Y-numbered.

- **BCR migration:** Move `tiff`, `png`, `zlib`, `fmt`, `zstd`, `webp`, `sqlite3` from `rules_foreign_cc` to `bazel_dep` in `MODULE.bazel` once the build is stable. Drift the version pins on a deliberate schedule — release-please-style or similar. Expected effort: 1 PR per lib batch.

- **Kakadu overlay refactor:** Replace the `make()` wrapper with a clean `cc_library` glob over Kakadu source for fine-grained file-level caching. Eliminates the Makefile path entirely; Bazel hashes individual `.cpp` files. Expected effort: ~3 days.

- **Y+8 — module-co-located layout flip + `package_group` boundary enforcement.** Move sipi from the historical split layout (`src/<mod>/` + `include/<mod>/` + `test/unit/<mod>/`) to a module-co-located shape where a module's source, headers, and unit tests sit in the same directory (Abseil / Bloomberg BDE / Chromium pattern; see ADR-0003 in `sipi/docs/adr/` for the design rationale). Decompose `//src:sipi_lib` into per-module `cc_library` targets with `package_group()`-restricted visibility, enable `features = ["layering_check", "parse_headers"]` repo-wide, and delete `scripts/shttps-context-check.sh` (the build graph subsumes it). Sequenced as five mechanical PRs:
  - **Y+8a** — `src/metadata/` + `include/metadata/` → co-located in `src/metadata/`. `SipiIccDetail.h` becomes a private internal header; `test/unit/sipiicc/` collapses into `src/metadata/sipiicc_test.cpp`. First per-module `cc_library`.
  - **Y+8b** — `src/formats/` + `include/formats/` → co-located. Each format codec (TIFF / JPEG / PNG / J2K) becomes its own `cc_library`. **First per-format unit tests** — today's coverage is approval-only, which Y+8b changes.
  - **Y+8c** — `src/iiifparser/` + `include/iiifparser/` → co-located. `decode_dims/` test merges into module test.
  - **Y+8d** — `src/handlers/` (already half-co-located) + the root files in `src/` and `include/` → final co-located shape. `include/` directory deleted.
  - **Y+8e** — Codify CONTEXT-MAP.md as `package_group()` + visibility entries; enable `--features=layering_check` and `--features=parse_headers` globally on first-party `//src` and `//shttps` (never on `//ext/<lib>`); delete `scripts/shttps-context-check.sh` (the build graph subsumes it). The `shttps/Server.cpp` → `SipiMetrics` violation is already gone — fixed pre-Y under [DEV-6339](https://linear.app/dasch/issue/DEV-6339); Y+8e just makes it impossible to reintroduce.
  - **Include-style decision (settled pre-Y+8a):** flat-style — siblings as `#include "Foo.h"`, cross-module as `#include "metadata/Foo.h"`. Matches `shttps/` and `src/handlers/` today; minimises diff churn. Abseil-style fully-prefixed (`#include "sipi/metadata/Foo.h"`) is rejected as verbose without semantic benefit on a single-binary project.
  - **Hard prerequisite:** Y+6 must be merged. The layout flip is build-tool-agnostic in principle but trivially cleaner under Bazel.
  - Expected effort: ~1 PR per week, five-week tail. Each PR is mechanical and revertable; CI green throughout.

- **dsp-repository's Rust IIIF service:** Greenfield Bazel from commit 1. Separate plan, separate workstream. **Gate: do not start until Sipi reaches Y+5 minimum** (Bazel-driven Rust tests proven via `rules_rust` + Crate Universe). The dsp-repository pilot reuses Y's `MODULE.bazel` patterns (toolchains_llvm, Crate Universe) so the second pilot exercises mostly novel surface area only on the IIIF protocol code.

- **`dasch-monorepo` creation:** Phase 3 destination once both pilots succeed. Single `MODULE.bazel` at root. Sipi and dsp-repository move in as `sipi/` and `dsp-repository/` subtrees. Their per-repo `MODULE.bazel` content merges; `bazel_dep` declarations consolidate. dsp-api / dsp-tools / dsp-app / dsp-ingest absorption follows on a per-repo gate (each repo's internal structure has to be clean enough — same gate that killed Bazel in dsp-api the first time).

- **Remote cache selection:** BuildBuddy SaaS vs self-hosted bazel-remote vs GHA-cache-via-proxy. Decision deferred until post-Y+4 CI cost data is available. Trigger condition for urgency: CI cold-cache duration > 30 min sustained, or GHA cache eviction observed.

- **Docker Scout → OSS replacement stack (Y+9 / Y+10 / Y+11):** swap Docker Scout for an open-source alternative providing equivalent capabilities plus net-new ones:
  - **Y+9** — replace Scout `compare` + `cves` (per-PR SARIF) with `aquasecurity/trivy-action` SHA-pinned to `57a97c7e7821a5776cebc9bb87c984fa69cba8f1` (post-incident hardening; tag-based references no longer safe). PR-comment "compare to production" implemented as bash + `jq`-diff between two Trivy SARIFs, baseline digest from `crane digest daschswiss/sipi:latest`. SARIF upload to GitHub Security via `codeql-action/upload-sarif` unchanged.
  - **Y+10** — replace Scout `sbom` with Syft via `anchore/sbom-action` (SHA-pinned). SPDX 2.3 format preserved; artifact upload to GitHub Actions unchanged. Syft is best-in-class for distroless / binary cataloging.
  - **Y+11** — net-new capability: `rules_oci`'s `cosign_sign` (per-arch image signing, keyless OIDC) + `cosign_attest --type spdx` (SBOM bound to image digest). Adds Sigstore signing + Rekor transparency that Docker Scout never provided. Per-arch on tag push. **Caveat:** `rules_oci`'s cosign rules are explicitly "developer preview" as of April 2026 — API may shift. If stability becomes a blocker, Y+11 falls back to invoking `cosign` from CI shell (`bazel run` swapped for `cosign sign $IMAGE_DIGEST` after `oci_push`); same outcome, less build-graph integration. Decide based on rules_oci stability at Y+11 time.
  - **Hard prerequisite: Y+4 must be merged and stable.** Scout integration cannot be removed until Bazel-built images are landing in production. Three sequential PRs, ~150 LoC CI + ~30 LoC `BUILD.bazel`. Not bundled into Y+4 because that PR is already load-bearing.
  - Full design + citations in `03-research-findings-scout-replacement.md`.

## Documentation Plan

Y+7 ships these documentation changes:

- **New:** `docs/src/development/bazel.md` — Bazel concepts for Sipi: `MODULE.bazel`, `BUILD.bazel`, query, common commands, `--config=` flags.
- **Rewritten:** `docs/src/development/building.md` — Bazel-as-build, Nix-as-environment. Inner-loop is `bazel build //src:sipi`. `just bazel-*` recipes documented. Local Linux builds via OrbStack escape hatch.
- **Reduced:** `docs/src/development/nix.md` — dev-shell setup only. All build content removed.
- **Updated:** `docs/src/development/developing.md` — test running via `bazel test`. Inner loop. Approval test workflow.
- **Updated:** `docs/src/development/ci.md` — Bazel CI flow. `bazel-contrib/setup-bazel`. Cache poisoning prevention.
- **Updated:** `docs/src/development/kakadu.md` — Kakadu fetched by a custom `kakadu_archive` `repository_rule` shelling out to `gh release download` (auth UX 1:1 with the deleted FOD). Local-dev: `gh auth login` once. CI: `GH_TOKEN=${{ secrets.DASCHBOT_PAT }}` on the Bazel job. The `just kakadu-fetch` step gone (Bazel's `repository_cache` replaces it).
- **Updated:** `sipi/CLAUDE.md` — build reproducibility invariant reworded for Bazel. The "ICC determinism invariant" callout (PR #587) is preserved verbatim; only the build-tool invariant is reworded.
- **Updated:** `sipi/REVIEW.md` — sections referencing Nix-build concerns updated.
- **Prerequisite repo:** `ops-deploy/roles/dsp-deploy/templates/docker-compose-iiif.yml.j2` — `healthcheck:` stanza added under [INFRA-1226](https://linear.app/dasch/issue/INFRA-1226). Lands and deploys to staging + prod before Y+4 merges; not part of Y+4's diff.
- **Status update:** `dasch-specs/specs/2026-04-16-sipi-nix-unified-build/01-feat-nix-unified-build-plan.md`, `02-feat-justfile-ci-unification-plan.md`, `03-feat-docker-nix-migration-plan.md` marked `superseded` with a pointer to this plan.

## References & Research

### Internal References

- `sipi/flake.nix:32-78` — Kakadu fixed-output derivation; reference for the `http_archive` auth pattern.
- `sipi/flake.nix:184-289` — `mkDockerImage` helper; reference for `oci_image` config attributes.
- `sipi/flake.nix:340-371` — fuzz `overrideAttrs` block; reference for `--config=fuzz` `.bazelrc` translation.
- `sipi/package.nix:1-246` — entire derivation; deleted in Y+6.
- `sipi/cmake/dependencies.cmake:1-135` — pinned versions; ports to `MODULE.bazel` `http_archive` calls.
- `sipi/ext/kakadu/CMakeLists.txt:30-54` — per-arch sed mutations; ports to patch files.
- `sipi/ext/shttp/CMakeLists.txt:1-26` — fake `ExternalProject_Add`; deleted, replaced by `//shttps:shttps`.
- `sipi/CLAUDE.md` — build reproducibility invariant; reworded in Y+7.
- `sipi/CONTEXT-MAP.md` — bounded contexts (Sipi → shttps); preserved at BUILD-file layer.
- `dasch-specs/specs/2026-04-16-sipi-nix-unified-build/` — the in-flight Nix unified build (Phase 1, 1.5, 2 already merged); status updated to `superseded` in Y+7.
- `sipi/.github/workflows/ci.yml:103-114` — current Codecov coverage flow (amd64-only, lcov direct).
- `sipi/.github/workflows/ci.yml:120-146` — current Docker Scout compare + CVE SARIF + GitHub Security upload flow (PRs only).
- `sipi/.github/workflows/publish.yml:115-154` — current Docker Scout record-production + SBOM + Sentry debug-files upload flow (tag push).
- `sipi/.github/workflows/publish.yml:175-190` — current Sentry release notification flow.
- `sipi/.github/release-please/{config,manifest}.json` — release-please config (build-tool-agnostic; unchanged).

### Companion documents

- **`02-research-findings-bazel-implementation.md`** — best-practice research on `rules_foreign_cc`, `rules_oci`, `toolchains_llvm`, and `rules_rust` + Crate Universe. Authoritative on every implementation specific this plan summarizes (sysroot, fuzz second toolchain, `crane index append`, compose-level HEALTHCHECK, `STABLE_*` keys, `--build-id=sha1`, hardening neutralization, etc.). Also documents prompt-injection attempts the research agents encountered and ignored.
- **`03-research-findings-scout-replacement.md`** — best-practice research on the optional post-launch Docker Scout → OSS replacement (Trivy + Syft + cosign). Documents the Trivy supply-chain incident's resolution, the SHA-pinning requirement, and concrete CI snippets. Corrects an earlier Trivy-status claim in `02-research-findings`.

### External References

- `bazelbuild/rules_foreign_cc` — `cmake()`, `configure_make()`, `make()` rules.
- `bazel-contrib/toolchains_llvm` — hermetic LLVM toolchain registration.
- `bazel-contrib/rules_oci` — OCI image building (replaces `dockerTools`).
- `bazel-contrib/rules_distroless` — `passwd()` and `group()` macros (fakeNss replacement).
- `bazelbuild/rules_rust` — Rust support; `crate.from_cargo` for Crate Universe (lazy resolution).
- `bazel-contrib/setup-bazel` — GHA action with cache management.
- `aspect-build/aspect_bazel_lib` — `expand_template` with `stamp_substitutions` for reproducibility.
- `@tar.bzl` (in `aspect_bazel_lib`) — deterministic tar layer assembly for `oci_image`.
- `google/go-containerregistry`'s `crane index append` — multi-arch manifest assembly across runners.

### Institutional Learnings

To be linked once `dasch-specs/learnings/best-practices/` has a relevant entry. Y+7 produces one explicitly.
