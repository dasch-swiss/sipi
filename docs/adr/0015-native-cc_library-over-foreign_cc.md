---
status: accepted
---

# Native `cc_library` over `rules_foreign_cc` for all C/C++ deps

SIPI removes `rules_foreign_cc` from the build entirely. Every C/C++ dependency
is now either a Bazel Central Registry (BCR) `bazel_dep` or a hand-written native
`cc_library` over an `http_archive`/release fetch, compiled by the same hermetic
LLVM toolchain (ADR-0014) as first-party code. With the last foreign_cc consumer
gone, `bazel_dep(rules_foreign_cc)`, the four `preinstalled_*` make/cmake/ninja/
pkgconfig toolchains, `bazel/foreign_cc_helpers.bzl`, `bazel/ar_wrapper.sh`,
`bazel/all_content.BUILD.bazel`, and the darwin `llvm-ar`/`llvm-ranlib`/`clang++`
shims are all deleted. foreign_cc footprint: **0**.

## The decision rule (governs every dep)

**Use a BCR `bazel_dep` only when stock is a true drop-in — no capability/codec
loss. The moment stock would force a compromise, vendor a native `cc_library`.**
A benign golden re-baseline (different-but-equivalent output bytes) is *not* a
compromise; a lost codec or capability *is*. SIPI is a load-bearing tool inside
an **archive** — it must ingest any spec-allowed input — so "use stock BCR
everywhere" was rejected: stock BCR `libtiff` ships JPEG/LZMA/ZSTD/WebP-in-TIFF
**disabled**, an unacceptable ingestion regression.

Dispositions: png/jpeg/webp → BCR `bazel_dep` (true drop-ins). tiff →
native `cc_library` copied from the BCR overlay with the four codecs re-enabled
(+ JBIG via jbigkit, a capability add). jansson/lcms2/sentry/exiv2/jbigkit →
native (no BCR module). Kakadu → native (proprietary, never on BCR).

## The `cmake_configure_file` pattern

Native rules that need a generated config header use the
[`cmake_configure_file`](https://github.com/wep21/cmake_configure_file) BCR
module (`@cmake_configure_file//:cmake_configure_file.bzl`) to reproduce CMake's
`configure_file()` natively — `#cmakedefine`/`#cmakedefine01`/`@VAR@`
substitution — the same mechanism the BCR libtiff overlay uses. Every `@VAR@`
token referenced in a template must be supplied in `defines`; platform-divergent
tokens are split with `select()`. Used by jansson, libtiff, and exiv2.

## The trade-off accepted

foreign_cc delegated the real build to each lib's own `./configure`/CMake, so it
auto-adapted on version bumps. A native `cc_library` means *we* statically
replicate that step: the source-file list and the generated config header, per
platform. Every version bump means re-diffing the upstream `*.cmake.in` and
re-deriving `defines` — silent drift if upstream adds a token. The maintainer
accepted this cost deliberately, to (a) stop crippling codecs, (b) delete the
foreign_cc machinery permanently, and (c) unlock cross-compilation. The cost is
proportional to config-header complexity and bump frequency; these libs are
stable and bumped rarely. Each native rule documents its token set in its
`bazel/<lib>.BUILD.bazel` docstring.

## Why this is a clean win under the hermetic toolchain

Native `cc_library` deps are first-class Bazel targets compiled by the hermetic
LLVM 22 / libc++ toolchain — no foreign_cc configure-time link probe — so they
entirely sidestep the libc++/exe-link failures ADR-0014 patched per-dep (the
exiv2 `FindFilesystem` pre-seed, the tiff `mkg3states` skip, the darwin
link-wrapper, the `llvm-ar`/`llvm-ranlib` archiver overrides), all now deleted.
C++ third-party TUs that target C++17 are pinned to `-std=gnu++17` via target
`copts` (overriding the global `-std=c++23`) — `gnu++17` keeps glibc POSIX
symbols visible on Linux that strict `-std=c++23` hides (e.g. `strerror_r`,
`iconv`). Same pattern as `bazel/kakadu.BUILD.bazel`.

## Capability unlocks

- **Cross-compilation (macOS → Linux).** foreign_cc was the wall — it host-binds
  `./configure`/`make` and cannot target another platform. With every dep
  compiled by the relocatable hermetic toolchain, `bazel build
  --platforms=//bazel/platforms:linux_amd64 //src:image` can build the Linux OCI
  image from a Mac (the cross-compile work).
- **ASan/UBSan + fuzz.** foreign_cc's global-instrument-breaks-CMake-probes
  failure mode is gone (no foreign_cc probes left to break under global
  instrument); the native deps also now flow through sanitizer/coverage
  instrumentation and the remote cache, which foreign_cc's opaque whole-archive
  actions did not. The remaining work is compiler-rt runtime provisioning
  (asan/ubsan/libFuzzer), tracked against ADR-0014's instrumentation gates.
- **New codec capability.** JBIG-in-TIFF decode, enabled by wiring jbigkit into
  the native libtiff (`JBIG_SUPPORT`) — SIPI could not decode it before.

## Consequences

- `MODULE.bazel`: png/jpeg/webp/libdeflate/cmake_configure_file added as BCR
  `bazel_dep`s; `http_archive` retained for the native libs with
  `build_file = "//bazel:<lib>.BUILD.bazel"`; tiff bumped to 4.7.1 (the BCR
  overlay's validated tarball); `bazel_dep(rules_foreign_cc)` +
  `register_toolchains(preinstalled_*)` deleted. `use_repo_rule http_archive` and
  the hermetic-llvm `use_repo` lines are load-bearing and stay (`bazel mod tidy`
  strips them — re-add).
- `bazel/`: `foreign_cc_helpers.bzl`, `ar_wrapper.sh`, `all_content.BUILD.bazel`,
  the `llvm-ar`/`llvm-ranlib`/`clang++` aliases, and the dead `patches/` dir
  deleted; `bazel/<lib>.BUILD.bazel` added for each native dep.
- The dev shell (`flake.nix`) no longer ships the foreign_cc host tools
  (perl/cmake/pkg-config/autotools); only bazelisk + `gh` (Kakadu fetch) + crane
  + just + opentofu + jpylyzer remain.
- Golden re-baseline: the libjpeg_turbo swap shifted 8 JPEG-affected approval
  goldens under a tolerance gate (decode drift ≤6 LSB; lossy re-encode avg ≤3.4;
  see `test/approval/CHANGELOG.approval.md`). png/webp/lcms2/kakadu/tiff
  round-trips are byte-identical (verified).
- New regression gates: `//test/unit/tiff_codecs` (all enabled codecs configured
  + lossless round-trips) and `//test/unit/sentry_smoke` (inproc backend +
  libbacktrace unwinder deliver a captured event).
