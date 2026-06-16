---
status: accepted
---

# Hermetic LLVM toolchain: swap `toolchains_llvm` → the BCR `llvm` module

SIPI's Bazel C++ toolchain provider moves from `toolchains_llvm` 1.7.0 (LLVM 19, host-coupled macOS SDK + manually-pinned Chromium Linux sysroots) to the BCR `llvm` module (`hermeticbuild/hermetic-llvm`) pinned at **0.8.8**, which carries **LLVM 22.1.7**. The new module is a fully hermetic, constraint-based toolchain: it bundles a per-target glibc + libc++ + compiler-rt and fetches the macOS SDK from Apple's CDN, so the build no longer depends on a host Xcode CLT or on the `commondatastorage.googleapis.com/chrome-linux-sysroot` tarballs.

This is a **config rewrite, not a version bump**. The old API was `llvm.toolchain(llvm_version, stdlib = {...}, sysroot = {...})` attributes; the new API is platform-constraint-based (`toolchain.exec(...)` / `toolchain.target(...)` extension tags + a single `register_toolchains("@llvm_toolchains//:all")`). The C++ standard is unchanged — `--cxxopt=-std=c++23` / `--host_cxxopt` in `.bazelrc` already carry it, and libc++ is the new toolchain's default stdlib (the old `stdlib = {linux: libc++}` map disappears).

We do this for hermeticity and to unlock darwin→linux cross-compilation (a Mac building the Linux Docker image locally — Phase 3, out of scope here). The swap removes the `toolchains_llvm` macOS coupling (`-nostdinc++`, `-isystem .../MacOSX.sdk/...c++/v1`, `DEVELOPER_DIR`/`SDKROOT` in `.bazelrc`) and the Chromium-sysroot repo rules + their bump procedure from `MODULE.bazel`.

The central acceptance criterion is **equivalence**: the existing approval goldens must pass **unchanged** (the swap introduces no codec/ICC byte drift), since `test/approval/BUILD.bazel` checks one shared golden set identically on every platform. Validated on darwin-aarch64: goldens pass unchanged under LLVM 22; `just bazel-coverage`'s pyramid runs 39/40 (1 skipped), matching the pre-swap baseline.

## The 19→22 reality

LLVM **19.1.7 has no prebuilt** in hermetic-llvm 0.8.x — the prebuilt registry (`llvm_versions.json`) covers `21.1.8` + `22.1.0`–`22.1.7`, default `22.1.7`. Pinning 19.1.7 would force `--@llvm//toolchain:source=bootstrapped`, a multi-hour from-source clang build that kills the CI inner loop and the Cloud Run remote cache. So the provider swap **necessarily carries a 19→22 compiler bump**; the two cannot be separated cheaply. We land both in one PR and lean on the equivalence gate (goldens unchanged); if goldens had drifted we would have bisected provider-vs-compiler, but they did not.

## Considered Options

- **Keep `toolchains_llvm`; add only a Linux sysroot for cross-compile** — rejected. Leaves the macOS Nix/Xcode-CLT coupling and the Chromium-sysroot maintenance in place. The `llvm` module removes both, which is the stated goal.
- **Pin LLVM 19.1.7 on the new module (provider swap with no compiler bump)** — rejected: no prebuilt, forces a bootstrapped from-source build (see above).
- **Split provider swap and compiler bump into two PRs** — not feasible: 19.x has no prebuilt under 0.8.x, so the provider swap cannot stay on 19. One PR, equivalence-gated.
- **Run the swap on the same branch/PR as Phase 1 (the microbench suite)** — rejected. Phase 2 gets its own branch + PR so the rollback unit is "this PR, reverted wholesale" (the two modules wire sysroots structurally differently — there is no clean runtime-flag coexistence). `toolchains_llvm` stays the default on `main` until the new path is green on all three platforms.

## Findings (integration surprises worth recording)

These were discovered during integration and shaped the final config. They are the parts most likely to break again on a version bump.

1. **macOS link-wrapper resolves `clang++` via an execroot-relative path (darwin).** 0.8.8's macOS "complete" toolchain routes every link action through a `link-wrapper` binary (ThinLTO + dsym handling) that `execv`s the compiler named by `LLVM_CLANGXX` — set to an execroot-relative path. That resolves for normal Bazel link actions (cwd == execroot) but **not** inside `rules_foreign_cc`, which relocates each build and runs cmake/make from a different cwd. CMake's compiler-detection links a C++ test exe, so every C++ foreign_cc dep (exiv2, sentry, libtiffxx) broke with "failed to execute external/.../clang++: No such file or directory". C-only deps (which never link a test program) and Kakadu (its own `clang++`-from-PATH wrapper) were unaffected. **Fix:** re-export `LLVM_CLANGXX` as an absolute path (`$$EXT_BUILD_ROOT$$/$(execpath //bazel:clang++)`) via the foreign_cc rule `env` (foreign_cc merges rule env *after* the toolchain env). Helpers `darwin_link_wrapper_env()` / `darwin_link_wrapper_build_data()` in `bazel/foreign_cc_helpers.bzl`; `//bazel:clang++` alias; wired into exiv2/sentry/tiff.

2. **macOS archiver is `llvm-libtool-darwin`, Apple-style (darwin).** The toolchain's default macOS archiver expects `-static -o`; foreign_cc's cmake/autotools deps invoke it with GNU-`ar` conventions (`ar qc …`) and it rejects them. **Fix:** route those deps' `AR`/`CMAKE_AR`/`CMAKE_RANLIB` at the bundle's GNU `llvm-ar`/`llvm-ranlib` (`//bazel:llvm-ar`, `//bazel:llvm-ranlib`) via a reworked `bazel/ar_wrapper.sh` (autotools) and `darwin_cmake_cache_entries()` (cmake). Darwin-only — on Linux the toolchain's default archiver is already GNU `llvm-ar`.

3. **foreign_cc build tools must be Nix-preinstalled, not bootstrapped.** The from-source bootstraps (`BootstrapGNUMake`, `BootstrapPkgConfig`) fail under the hermetic archiver. We register `@rules_foreign_cc//toolchains:preinstalled_{make,pkgconfig,cmake,ninja}_toolchain` (Nix-provided), consistent with the plan's stance that foreign_cc *build tools* stay Nix-provided. Honest scope: the hermetic *compiler* does not give hermetic *autotools* — `--action_env=PATH`/`ACLOCAL_PATH`/`NIX_LDFLAGS` remain for libmagic's `autoreconf`, openssl's `perl Configure`, and Kakadu's Nix sub-make.

4. **`linux-aarch64` is a native exec host in CI, not only a cross target.** CI runs the test matrix natively on `ubuntu-24.04-arm`. Declaring `linux-aarch64` only as a `toolchain.target(...)` (the cross-compile assumption) makes Bazel find no exec-compatible hermetic toolchain on the native arm64 runner and fall back to the autodetected Nix host CC toolchain (`ld.gold`), which compiles `-stdlib=libc++` but never links the libc++ runtime → a flood of `undefined reference to std::__1::…`. **Fix:** declare `toolchain.exec(arch = "aarch64", os = "linux")`.

5. **macOS SDK CDN is fragile.** The primary `swcdn.apple.com/.../CLTools_macOSNMOS_SDK.pkg` URL returns 403 (Apple rotates these); the build succeeded only because the module ships a `web.archive.org` mirror fallback. Caching the SDK pkg through the bazel-remote Cloud Run proxy (or a GCS object) is a CI-resilience follow-up.

6. **foreign_cc cannot link an executable under the hermetic toolchain on Linux.** The toolchain models libc++/libc++abi/libunwind **and glibc** as built-from-source Bazel `libraries_to_link` — materialized (compiled from `llvm-project` source) only as link inputs of a first-party target that depends on the toolchain's `static_runtime_lib`. The copies a foreign_cc action sees in the toolchain's library-search dirs are **empty placeholder archives** (8 bytes, 0 symbols). foreign_cc also captures the toolchain's link *flags* (incl. `-nostdlib++ --unwindlib=none -rtlib=compiler-rt`) but not those library inputs. So first-party C++ links fine (verified: a `cc_binary` using `<filesystem>` compiles libc++ from source and links), but **any executable a foreign_cc dep links — a CMake feature probe or a build-time tool — fails**: libc++-symbol probes get `undefined symbol std::__1::…`, and libc-using links fall back to the host `/lib/.../libm.so.6` whose `@GLIBC_PRIVATE` deps mismatch the bundled glibc 2.28. Deps that only *archive* (the six C deps, sentry, and exiv2 after the fix) are unaffected. This is the Linux analog of finding #1 (darwin's link-wrapper broke the same C++ foreign_cc links via a different mechanism). Two deps tripped it and are fixed per-dep rather than by staging the runtimes (which would mean depending on the version-mangled `@@llvm++llvm+llvm-project//…:*.static` internal labels):
   - **exiv2** — `cmake/FindFilesystem.cmake` runs a `check_cxx_source_runs` std::filesystem probe. Pre-seed `CXX_FILESYSTEM_NO_LINK_NEEDED=TRUE` (correct for libc++, which ships filesystem in the main library) to skip the probe.
   - **tiff** — builds `tiff_mkg3states`, a C codegen executable. Restrict the foreign_cc build to the library targets (`targets = ["tiff", "tiffxx"]`); the pre-generated `tif_fax3sm.c` is already compiled, so the tool is unneeded.
   A general fix (staging the runtime `.static` libs into foreign_cc actions + linking them, reaching CMake's `try_compile`) was rejected for this PR: it couples the build to internal version-mangled toolchain labels, and only two deps need the per-dep escape today.

## Carved-out instrumentation gates

Three instrumentation-based CI gates **cannot run** under hermetic-llvm 0.8.8's minimal prebuilt. We **proceed with the swap and carve them out** — the core gate (build/test/approval/e2e/smoke/Docker on all three platforms) passes the equivalence test, which is what protects production codegen. The three are disabled at the auto-trigger level (`workflow_dispatch` kept) with a comment pointing here:

| Gate (`.github/workflows/…`) | Was | Why it breaks | Re-arm condition |
|---|---|---|---|
| `coverage.yml` | post-merge on `main` | `llvm_coverage_map_format` does not activate (no `-fcoverage-mapping` even when forced); gated on the **unmerged rules_cc PR #385** per the toolchain's own TODO. `bazel coverage` yields an empty lcov (LF=0). | rules_cc #385 merges and the feature activates; re-point `llvm-cov`/`llvm-profdata` to the bundle's `:bin/*` (already done) and re-run a non-empty lcov on linux-amd64. |
| `sanitizer.yml` | PR merge gate | The minimal prebuilt ships **no ASan/UBSan runtime**; the only build mechanism (`--@llvm//config:asan=true`) instruments the **whole toolchain globally** (every compile incl. foreign_cc deps — jansson's CMake compiler-detection link then breaks), incompatible with SIPI's `--per_file_copt` scoping. Building the runtime archives standalone also fails (`<cassert>` not found outside the runtime config). | A prebuilt set that ships the sanitizer runtimes, or a per-target (not global) instrument path that respects `--per_file_copt`. |
| `fuzz.yml` | nightly | No libFuzzer runtime; same global-instrument problem in the fuzz subset. | A prebuilt set that ships the libFuzzer runtime. |

**Diagnostic note (Ivan, 2026-06-16): "if all deps were BCR, would it work — is foreign_cc the problem?"** foreign_cc is the *proximate* cause of the configure-time link break (BCR `cc_library` deps have no compiler-detection probe), so an all-BCR graph would dodge that specific failure mode on Linux. But SIPI **cannot** be all-BCR — **Kakadu is proprietary/license-gated and never on BCR** — and the darwin sanitizer-runtime naming gap (`libclang_rt.asan_osx_dynamic.dylib`) is independent of foreign_cc. "All BCR" helps but is not a complete fix.

## Pinning policy (v0.8.x, compatibility-level 0, ~weekly releases)

The `llvm` module is at compatibility level 0 and releases roughly weekly; codegen and the sanitizer/link surface can shift between releases with no cross-version guarantee. Therefore:

1. **Pin an exact version** (`0.8.8`), never a range. The repo-name of the prebuilt bundle even embeds the version + exec triple (`llvm-toolchain-minimal-22.1.7-<os>-<arch>`), so a bump touches `MODULE.bazel` *and* the `//bazel` aliases.
2. **Bump only on a real trigger** — an LLVM version we need, or a fix we require — never speculatively.
3. **Every bump re-runs the full equivalence gate**: approval goldens unchanged + 3-platform build + (once re-armed) coverage/sanitizer/fuzz, both Docker arches. Codegen can shift; treat each bump as a fresh proof.
4. **Keep the revert unit intact for one release cycle** — Phase 2 is one PR, reverted wholesale; `toolchains_llvm` stays the default on `main` until the new path is green on all three platforms.

## Consequences

- `MODULE.bazel`: `bazel_dep(name = "llvm", version = "0.8.8")` replaces `toolchains_llvm`; `llvm_source.version("22.1.7")`; `toolchain.exec(macos-aarch64 / linux-x86_64 / linux-aarch64)` + `toolchain.target(macos-aarch64 / linux-x86_64 / linux-aarch64)`; `register_toolchains("@llvm_toolchains//:all")`; the prebuilt bundles use_repo'd for the `//bazel` tool aliases. MVS bumped `platforms` 1.0.0→1.1.0 and `rules_cc` 0.2.18→0.2.19.
- The second `llvm_toolchain_fuzz` toolchain is **folded away** — a single registered toolchain serves every platform including the `//tools/fuzz:*` platforms (the extra `fuzz_enabled` constraint comes from a `constraint_setting` this toolchain never references, so Bazel ignores it during resolution by subset inclusion). Both toolchains were libc++ on Linux anyway; the second existed only for first-registered-wins routing a single toolchain makes unnecessary.
- `.bazelrc`: macOS SDK hacks deleted; `--repo_env=PATH` + the `-mmacosx-version-min=13.3` floor kept; the `_FORTIFY_SOURCE` undef-then-define and Linux hardening flags unchanged.
- `//src:sipi_debug_split` + coverage tools repointed to the bundle's `:bin/llvm-{objcopy,readelf,cov,profdata}` via `//bazel` aliases (exec-arch `select`). Coverage tools MUST be the LLVM-22 bundle binaries — a host LLVM-19 `profdata` cannot read a v22 `.profraw`.
- **`bazel/glibc23_compat.c` is RETAINED** (deletion deferred to a follow-up). The shim forwards `__isoc23_*`/`fcntl64` to canonical glibc entries present in any modern glibc (incl. the bundled ~2.28), and Kakadu still compiles via the Nix clang against host glibc headers, so the shim still resolves. Deleting it + rerouting Kakadu's foreign_cc compile through the hermetic clang is a hermeticity *cleanup*, not required for a green core build.
- **Cross-compilation (darwin→linux) is supported by the module but explicitly out of this PR's definition of done** — proving Kakadu under cross + `rules_oci` producing a loadable arm64 image from darwin is a separate multi-day effort (Phase 3).
- Follow-ups filed: coverage under hermetic-llvm (rules_cc #385); sanitizer runtime provisioning; libFuzzer runtime provisioning; `glibc23_compat.c` deletion + Kakadu reroute; SDK-pkg caching through the Cloud Run proxy.

> `bazel mod tidy` strips the `use_repo(llvm_toolchain_minimal, …)` / `use_repo(llvm_source, …)` lines — it cannot see the bundles referenced from the string labels in `//bazel`'s `alias(actual = "@llvm-toolchain-minimal-…")` targets and judges the repos unused. They are load-bearing; re-add them after any future `bazel mod tidy`.
