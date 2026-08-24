---
title: "SIPI: eliminate rules_foreign_cc — BCR drop-ins + native cc_library for the rest (incl. Kakadu)"
type: build
date: 2026-06-17
author: "Ivan Subotic"
status: reviewed   # rewritten 2026-06-17: scope expanded from 4-codec BCR migration to full foreign_cc elimination; spec-reviewed (2 cycles incl. consistency pass)
linear: DEV-6563  # Wave 2 of the BCR migration umbrella DEV-6516
repositories:
  - sipi
adrs: [0002, 0014]   # 0002 ICC determinism (golden gate); 0014 hermetic LLVM toolchain (foreign_cc context)
future_adrs: [0015]  # proposed: "Native cc_library over rules_foreign_cc for all C/C++ deps"
---

# SIPI: eliminate `rules_foreign_cc` — BCR drop-ins + native `cc_library` for the rest (incl. Kakadu)

## Overview

This plan removes **`rules_foreign_cc` entirely** from the SIPI build. Every C/C++ dependency becomes either a Bazel Central Registry (BCR) `bazel_dep` (where stock is a true drop-in) or a **hand-written native `cc_library`** over the existing `http_archive`/release fetch (where stock would compromise capability, or where no BCR module exists). When the last foreign_cc consumer is gone, `bazel_dep(rules_foreign_cc)`, the four `preinstalled_*` make/cmake/ninja/pkgconfig toolchains, `bazel/foreign_cc_helpers.bzl`, `bazel/ar_wrapper.sh`, `bazel/all_content.BUILD.bazel`, and the darwin `llvm-ar`/`llvm-ranlib`/`clang++` alias shims are all **deleted**.

This is a rewrite of the original 4-codec BCR plan. It supersedes that narrower scope after the Phase-1 decision (below) and a directive from the maintainer:

> "I don't mind using BCR if it is a drop-in replacement. As soon as we need to compromise, we should make our own." — and — "I don't want to cripple SIPI just to 'simplify' things. We need a solution that solves all foreign_cc usage."

SIPI is a load bearing tool used inside an **archive**: it must ingest any spec-allowed input. The narrow "use stock BCR everywhere" approach was rejected precisely because stock BCR `libtiff` silently disables four TIFF ingest codecs SIPI supports today (verified — see below).

## ✅ SESSION HANDOFF — x86 JPEG encode crash RESOLVED (2026-06-17)

**Where it stands.** Phases 4–9 are implemented and pushed as **PR #675** (branch `feature/dev-6563-foreign-cc-elimination`). `foreign_cc = 0` is achieved. The last linux-amd64 red was **not** a golden-divergence problem (the prior handoff's diagnosis was wrong — see below). It was a **hard JPEG-encode crash on x86**, now fixed by switching to baseline JPEG (commit `5bdb545`). Local gates green on darwin-arm64: build, unit 17/17, approval, e2e 23/23. **Cross-arch CI on `5bdb545` is GREEN ✅ — all 3 platforms (darwin-arm64, linux-amd64, linux-arm64) + docs + docker-scout pass; PR #675 is `MERGEABLE`.** The arm-generated goldens passed the **byte-exact** approval test on linux-amd64, proving baseline `jchuff` output is bit-identical across architectures (no tolerance comparator needed).

**Branch commits (final, in order):** `8ec8316` P5 (jbigkit/lcms2/jansson) · `f14d6fa` P6 (tiff+codecs, jpeg/webp BCR) · `060058e` P7 (sentry) · `e96b554` P8 (exiv2, png BCR) · `8fe5cfc` P9 (delete foreign_cc + ADR-0015; the exiv2 Linux `strerror_r` fix, doc cleanup, and review fixes are folded in) · **`5bdb545` `fix(jpeg)`: deterministic cross-arch JPEG (integer decode + baseline encode)**. _(History was cleaned per the commit conventions before merge: the JPEG debugging journey — an `optimize_coding` attempt, then `JDCT_ISLOW` decode, then progressive→baseline — collapsed into the single `fix(jpeg)` commit, and the three standalone fixup commits folded into P9. Earlier SHAs `bff9084`/`cebcd5b`/`76169ef`/`59ba71a`/`6b58d7e`/`48d563e` no longer exist.)_

**Linux bugs surfaced by CI (all FIXED):**
1. **exiv2 `strerror_r` (FIXED, `59ba71a`).** `EXV_STRERROR_R_CHAR_P` must be defined on Linux (glibc GNU `strerror_r` returns `char*`), undefined on macOS (XSI `int`). Split via `select()` in `bazel/exiv2.BUILD.bazel`.
2. **JPEG decode determinism (FIXED, folded into `5bdb545`).** Decode forced `JDCT_FLOAT` (libjpeg documents float IDCT as machine-dependent). Switched both decode sites to integer `JDCT_ISLOW` (deterministic per libjpeg-turbo's single-MD5 integer tests; faster; matches the encoder default). Correct determinism hygiene, but **orthogonal to the encode crash** below.
3. **JPEG *encode* crashed on x86 — the real blocker (FIXED, `5bdb545`).** See the diagnosis correction.

**⚠️ DIAGNOSIS CORRECTION (this is why the prior handoff was wrong).** The prior handoff claimed the remaining red was "JPEG goldens are architecture-divergent" and recommended a "hybrid tolerance comparator." The actual linux-amd64 CI (`gh api .../jobs/<id>/logs`) showed every failure was the *same exception* — `SipiIOJpeg.cpp:1111: JPEG write failed: Missing Huffman code table entry` — thrown during JPEG **encode**, hitting plain functional unit tests that never touch a golden (`JpegWrite.WriteToFileProducesValidJpeg`, `SipiImage.ConvertTiffWith*ToJPG`, `ConvertPng16BitToJpg`, `CIELab16_Conversion`, `CMYK_Conversion`) plus 1 approval golden and the Rust e2e. A tolerance comparator would have fixed **none** of these (they fail by exception, not byte mismatch) and would have masked a real production defect. an `optimize_coding=TRUE` attempt did **not** fix it; the handoff's "got x86 past the encode" was premature.

**Root cause (verified against libjpeg-turbo 3.1.3 source).** With `optimize_coding`, libjpeg runs a statistics-gather pass then an encode pass over the *same buffered coefficients*; the error at `jcphuff.c:322` fires when the encode pass emits a Huffman symbol the gather pass never counted — the two passes disagree. arm uses `jcphuff-neon.c`, x86 uses `jcphuff-sse2.asm`; arm passes, x86 fails → the bug is libjpeg-turbo's **x86 SSE2 progressive-Huffman SIMD**. libjpeg-turbo's own ChangeLog documents this exact symptom as a known SSE2-progressive failure class; the 2.0-era fix doesn't cover SIPI's scan script in 3.1.3.

**Fix (`5bdb545`): switch JPEG output from progressive to baseline sequential.** Dropped `jpeg_simple_progression()` (+ the dead pre-`jpeg_set_defaults` `progressive_mode=TRUE`); kept `optimize_coding`. Baseline uses the canonical `jchuff` encoder, bit-exact across SIMD implementations. Same quantized DCT coefficients in a different scan order → different compressed bytes, **identical decoded pixels**: `sipi compare` reports "Files identical!" on all 5 prior progressive goldens vs the new baseline output. Re-baselined the 5 JPEG-encode goldens (`JpegFullToJpegDownscaled`, `J2kRegionToJpeg`, `CmykTiffToJpeg`, `CielabTiffToJpeg`, `JpegRotatedToJpeg`); the 3 JPEG-*decode* goldens (`.tif`/`.png`) are untouched (the switch only changes the encode path). Decision: baseline is correct for an archive's Access Files (universally valid, immaterial for IIIF region/tile responses) — maintainer-approved 2026-06-17. Baseline being bit-exact across SIMD is also expected to make the goldens byte-exact on all 3 arches (no tolerance comparator needed); CI on `5bdb545` confirms.

**Files:** `src/formats/SipiIOJpeg.cpp` (baseline switch; `JDCT_ISLOW` decode; `optimize_coding`), `bazel/exiv2.BUILD.bazel` (`EXV_STRERROR_R_CHAR_P` select), `test/approval/approval_tests/*.jpg` (5 re-baselined goldens) + `test/approval/CHANGELOG.approval.md`.

## The decision rule (governs every dep)

**Use a BCR `bazel_dep` only when stock is a true drop-in — no capability/codec loss.** The moment stock would force a compromise, **vendor our own native `cc_library`** (copy the BCR overlay as a base and customize, or hand-write where no BCR module exists). A benign golden re-baseline (different-but-equivalent output bytes) is **not** a compromise; a lost codec or capability **is**.

Applying the rule:

| Dep | Today (build rule) | On BCR? | Disposition | Why |
|-----|--------------------|---------|-------------|-----|
| zlib, zstd, bzip2, xz, sqlite3, libexpat, libmagic, lua, curl, openssl, prometheus-cpp, protobuf | `bazel_dep` ✓ | ✓ | **done** | already migrated (Wave 1 + openssl + others) |
| **png** | foreign_cc cmake | ✓ `libpng@1.6.54` | **`bazel_dep`** (drop-in) | clean codec seam; verify goldens (version bump 1.6.41→1.6.54) |
| **webp** | foreign_cc cmake | ✓ `libwebp@1.6.0` | **`bazel_dep`** (drop-in) | SIPI emits no WebP directly — only WebP-in-TIFF (covered by vendored libtiff). Version bump 1.2.0→1.6.0 |
| **jpeg** | foreign_cc configure_make (IJG 9f) | ✓ `libjpeg_turbo@3.1.3.bcr.5` | **`bazel_dep`** (drop-in) | API drop-in (only ≤v8 symbols, verified); golden re-baseline is benign, not a capability loss |
| **tiff** | foreign_cc cmake | ✓ `libtiff@4.7.1` (codec-crippled) | **native `cc_library`** (copy BCR overlay + customize) | stock BCR disables JPEG/LZMA/ZSTD/WebP-in-TIFF — a compromise for an archive |
| **jansson** | foreign_cc cmake | ✗ | **native `cc_library`** | no BCR module; small C + one generated config header |
| **lcms2** | foreign_cc configure_make | ✗ | **native `cc_library`** | no BCR module; core lib is `src/*.c`, no config header, no jpeg/tiff dep |
| **sentry** | foreign_cc cmake | ✗ | **native `cc_library`** | no BCR module; inproc+curl backend = file-list + defines |
| **exiv2** | foreign_cc cmake | ✗ | **native `cc_library`** | no BCR module; largest — generated `exv_conf.h` + bundled XMP SDK |
| **jbigkit** | foreign_cc make | ✗ | **native `cc_library`** + wire into libtiff | enable JBIG-in-TIFF (archive completeness). Trivial native rule (`jbig.c`+`jbig_ar.c`); GPL, fine under SIPI's **AGPL-3.0** (already linked today) |
| **Kakadu** | foreign_cc `make()` (proprietary) | ✗ (never) | **native `cc_library`** | wraps `rules_foreign_cc` `make()` today → blocks deleting foreign_cc *and* cross-compile. Largest/riskiest → sequenced **first among the dep migrations** (Phase 3) to de-risk the whole plan |
| ffmpeg_static ×2 | `http_archive` (prebuilt binaries) | — | keep | not foreign_cc; prebuilt, not built from source |

## Capability payoff (primary motivations — all in scope)

Beyond deleting foreign_cc, this plan restores the capabilities the hermetic-LLVM swap (ADR-0014) carved out. **Three are must-restore** — they worked before the swap: cross-compile (Phase 10), ASan/UBSan (12), fuzz (13). **Coverage (Phase 11) is a stretch goal** — it never worked correctly to begin with and is upstream-gated. End state the maintainer asked for: foreign_cc = 0 **and** cross + asan + fuzz green; coverage if rules_cc #385 lands.

1. **Cross-compilation (macOS → Linux)** — build the Linux OCI image (`//src:image`) locally on a Mac. foreign_cc is the wall: it host-binds `./configure`/`make` and can't target another platform. The toolchain is already cross-capable — it declares all exec×target pairs (`exec=darwin-aarch64 → target=linux-x86_64/aarch64`, `MODULE.bazel:411-435`) and is zero-sysroot (bundled per-target glibc stubs + libc++ + compiler-rt). The OCI target is correctly gated to target OS = linux (`@platforms//os:linux`, `src/BUILD.bazel:825`, **stays**), and `just bazel-docker-build-amd64` already passes `--platforms=//bazel/platforms:linux_amd64` — so once every dep compiles through the relocatable hermetic clang toolchain, that same invocation cross-builds from a Mac. No gate to lift.
2. **ASan/UBSan** (Phase 12) + 3. **Fuzz** (Phase 13) — disabled by `3d982e4`; both worked before the swap. foreign_cc elimination directly unblocks them (it removes the global-instrument-breaks-foreign_cc failure mode — see "Capability restoration"), leaving compiler-rt runtime provisioning.
4. **Coverage** (Phase 11) — **stretch goal**: never worked correctly to begin with (empty lcov), gated on upstream rules_cc #385, independent of foreign_cc. Not a deal-breaker. Per-gate re-arm analysis below.

## Problem Statement

1. **`rules_foreign_cc` is the residual build pain** (verified against ADR-0014 + the current tree). It carries two compounding costs:
   - **Hermetic-toolchain friction + host-binding.** Under hermetic LLVM 22, foreign_cc cannot link executables on Linux (libc++/glibc are built-from-source Bazel inputs absent from the foreign_cc action). Today this is patched per-dep: `ext/tiff` skips the `mkg3states` tool (`targets=["tiff","tiffxx"]`), `ext/exiv2` pre-seeds `CXX_FILESYSTEM_NO_LINK_NEEDED`. On macOS, foreign_cc needs a `darwin_link_wrapper_*` shim (absolute `clang++` for CMake's compiler-detection link) and a `darwin_*` archiver override (`llvm-ar`/`llvm-ranlib` instead of the Apple-style `llvm-libtool-darwin`). And foreign_cc runs the lib's own `./configure`/`make`, which build for the *execution* host — blocking cross-compilation. Every new foreign_cc C/C++ dep risks a fresh escape hatch.
   - **Performance + maintenance.** Each lib is one opaque Bazel action running `./configure` + a non-parallel `make` (no `-j`), with whole-archive cache keys — no per-TU caching, no sanitizer/coverage instrumentation flow-through, no remote-cache participation at the compile level. And the hand-written `ext/<lib>/BUILD.bazel` recipes plus the `$$VAR` escaping / `$EXT_BUILD_DEPS` staging / `set_file_prefix_map` boilerplate are a standing maintenance liability.
2. **Stock BCR `libtiff@4.7.1` cripples four ingest codecs** (verified against the live overlay, 2026-06-17). Its `tiff_impl` `cc_library` wires only `@zlib` + `@libdeflate`, and `tiffconf.h` sets none of `JPEG_SUPPORT`/`LZMA_SUPPORT`/`ZSTD_SUPPORT`/`WEBP_SUPPORT` — so `tif_jpeg.c`/`tif_lzma.c`/`tif_zstd.c`/`tif_webp.c` compile but stub out. (The module-level `bazel_dep(libwebp)`+`bazel_dep(zstd)` are declared but **unused** by the overlay's `cc_library`.) For an archive, dropping JPEG/LZMA/ZSTD/WebP-in-TIFF decode is an unacceptable ingestion regression.
3. **Five deps have no BCR module** (verified — `exiv2`, `lcms2`, `jansson`, `sentry-native`, `jbigkit` all 404 in BCR) and **Kakadu never will** (proprietary). The only way to "solve all foreign_cc usage" without crippling SIPI is to own native `cc_library` recipes for these.

## Proposed Solution

Three mechanical patterns, applied per the decision rule:

1. **BCR drop-in (`png`, `jpeg`, `webp`):** replace the `http_archive` + `ext/<lib>/BUILD.bazel` with a single `bazel_dep`; rewrite consumers to the module's exported target; verify/re-baseline goldens. (Precedent: openssl, prometheus-cpp, the Wave-1 deps.)
2. **Native `cc_library`, copy-and-customize a BCR overlay (`tiff`):** keep the `http_archive` (bumped to 4.7.1 to match the overlay source), vendor the BCR libtiff overlay `BUILD.bazel` into `ext/tiff/`, and customize it: add `@libjpeg_turbo` + `@xz//:lzma` + `@zstd` + `@libwebp` to `tiff_impl.deps` and `JPEG_SUPPORT`/`LZMA_SUPPORT`/`ZSTD_SUPPORT`/`WEBP_SUPPORT` to the `tiffconf.h` `cmake_configure_file` defines. Upstream the same codec-enable patch to BCR in parallel so we can later switch to `bazel_dep` override-free.
3. **Native `cc_library`, hand-written (`jansson`, `lcms2`, `sentry`, `exiv2`, `Kakadu`):** keep the existing fetch; replace the foreign_cc rule with explicit `cc_library` srcs/hdrs/deps, using the **`cmake_configure_file` BCR module** (`@cmake_configure_file//:cmake_configure_file.bzl`) to reproduce any `#cmakedefine`/`@VAR@` config header — the exact mechanism the libtiff overlay uses. No `$EXT_BUILD_DEPS` staging, no probe hacks; the source compiles with the same hermetic LLVM/libc++ toolchain as first-party code.

**End state.** png/jpeg/webp have no `ext/` dir. `tiff`, `jansson`, `lcms2`, `sentry`, `exiv2`, `jbigkit`, `kakadu` are `http_archive`/release-fetch + a native `cc_library` we own (jbigkit wired into libtiff to enable JBIG-in-TIFF). `rules_foreign_cc` + its toolchains + `bazel/foreign_cc_helpers.bzl` + `bazel/ar_wrapper.sh` + `bazel/all_content.BUILD.bazel` + the `//bazel:llvm-ar`/`llvm-ranlib`/`clang++` aliases are all deleted. foreign_cc footprint: **0**.

**The honest trade-off (maintainer has accepted this).** foreign_cc delegates the real build to each lib's own `./configure`/CMake, so it auto-adapts on version bumps. A native `cc_library` means *we* statically replicate that step: the source-file list and the generated config header, per platform. Every version bump means re-diffing the upstream `*.cmake.in` and re-deriving `defines` — silent drift if upstream adds a token. This is the "AI-supported, not team-sustainable" burden the original spec flagged and rejected for these libs; we are deliberately overriding that rejection to (a) stop crippling codecs, (b) delete the foreign_cc machinery permanently, and (c) unlock cross-compilation. The cost is proportional to config-header complexity and bump frequency; these libs are stable and bumped rarely.

## Technical Approach

### The `cmake_configure_file` pattern (the spine of every native rule)

`bazel_dep(name = "cmake_configure_file", version = "0.1.7")`, loaded as `@cmake_configure_file//:cmake_configure_file.bzl`. It reproduces CMake config-header generation natively:
- `#cmakedefine VAR` → `#define VAR <value>` or `/* #undef VAR */`
- `#cmakedefine01 VAR` → `#define VAR 1|0`
- `@VAR@` / `${VAR}` → literal substitution (package strings, version macros, `TIFF_INT8_T=int8_t`-style typedefs)
- attrs: `src`, `out`, `defines = ["VAR=VALUE", "HAVE_FOO=1"]`, `undefines = [...]`; split platform-divergent tokens with `select()`.

It is tested green on Bazel 7/8/9 across Debian/Ubuntu/macOS/Apple-Silicon (all three SIPI platforms) and is what the BCR libtiff overlay uses. Caveat (carried in its own docs): "incomplete, may not match CMake in all cases" — so read each `.cmake.in` and supply every token explicitly.

`rules_cc` is already pinned at `0.2.19` (so `cc_library` must be explicitly loaded: `load("@rules_cc//cc:cc_library.bzl", "cc_library")` — same as the overlay). Local references for exact source lists / templates: **`/Users/subotic/_github.com/bazelbuild/bazel-central-registry`** (BCR modules + overlays) and **`/Users/subotic/_github.com/bazelbuild/bazel`**.

### Capability restoration (cross-compile + the three instrumentation gates)

Four capabilities the hermetic-LLVM swap carved out — **three must-restore** (cross, asan, fuzz; they worked before) **plus coverage as a stretch goal**. The per-gate truth, grounded in ADR-0014's "Carved-out instrumentation gates" table + Ivan's 2026-06-16 diagnostic note:

1. **Cross-compilation (macOS → Linux) — Phase 10.** The toolchain is already cross-capable: `MODULE.bazel:411-435` declares all exec×target pairs; zero-sysroot (bundled per-target glibc + libc++ + compiler-rt, `MODULE.bazel:157-159,373-377`); `just bazel-docker-build-{amd64,arm64}` already passes `--platforms=//bazel/platforms:linux_{amd64,arm64}` (`justfile:328-332`). The OCI target is correctly gated to target OS = linux (`@platforms//os:linux`, `src/BUILD.bazel:825`) — **not** host CPU, so there is **no gate to lift**. foreign_cc's host-bound `./configure`/`make` is the only thing making a `--platforms=linux_*` build fail. Remaining work after foreign_cc=0: per-dep host-leak verification (the classic is exiv2's `<features.h>` — native rules must pull toolchain headers only; the `cmake_configure_file` config headers help by removing host probing) and Kakadu's per-**target**-arch SIMD `select()` (AVX2 vs NEON) — handled in Phase 3. `rules_oci` is host-agnostic (assembles layers from `cc_binary` outputs), so once the binary cross-links, the image assembles on any host.

2. **Coverage (`coverage.yml`) — Phase 11, STRETCH.** ADR-0014 re-arm condition: the unmerged **rules_cc #385** must land (`llvm_coverage_map_format` doesn't activate; `bazel coverage` yields empty lcov, LF=0) — **independent of foreign_cc**. The `llvm-cov`/`llvm-profdata` bundle repoint is already done. **Stretch, not a deal-breaker:** coverage never worked correctly to begin with, and completion is gated on an upstream merge we don't control. The plan is done without it.

3. **ASan/UBSan (`sanitizer.yml`) — Phase 12.** Two ADR-0014 blockers: (a) the minimal prebuilt ships **no ASan/UBSan runtime**, and (b) the only build mechanism (`--@llvm//config:asan=true`) instruments the toolchain **globally** — which breaks foreign_cc deps' CMake compiler-detection link (jansson) and conflicts with SIPI's `--per_file_copt` scoping. **Eliminating foreign_cc removes blocker (b)** (Ivan's ADR note: an all-native graph dodges that failure mode — and after this plan there are no foreign_cc probes left to break under global instrument). Blocker (a) remains: provision the compiler-rt asan/ubsan runtimes (a runtime-carrying hermetic-llvm bundle, or built compiler-rt) + the darwin `libclang_rt.asan_osx_dynamic.dylib` naming gap (independent of foreign_cc).

4. **Fuzz (`fuzz.yml`) — Phase 13.** Same shape as asan: no libFuzzer runtime in the minimal prebuilt + the global-instrument problem (removed by foreign_cc elimination). Remaining work: provision the libFuzzer runtime; re-arm the harness on linux-x86_64 (CI) + darwin (local).

Net: foreign_cc elimination is a genuine enabler for **asan + fuzz** (removes the global-instrument-breaks-foreign_cc blocker), a non-factor for **coverage** (upstream #385), and the precondition for **cross-compile**. The shared remaining instrumentation work is **runtime provisioning** in the toolchain layer (asan/ubsan + libFuzzer compiler-rt runtimes), which Phases 12–13 tackle together.

### Consumer map (drives the File Manifest — verified, `file:line`)

Only **four production packages** consume the ext libs; everything else is inter-ext cross-deps. No test/fuzz/tool/server BUILD references any `//ext/*` label.

Direct production consumers:
- `//src:sipi_lib` (`src/BUILD.bazel:315-336`): lcms2 `:315`, exiv2 `:316`, tiff `:318`, webp `:319`, jbigkit `:320`, png `:321`, jansson `:325`, sentry `:327`, jpeg `:332`, `@kakadu//:kdu` `:336`.
- `//src/metadata:metadata` (`src/metadata/BUILD.bazel:84-85`): exiv2, lcms2.
- `//src/observability:observability` (`src/observability/BUILD.bazel:54`): sentry.
- `//src/shttps:shttps_headers` (`src/shttps/BUILD.bazel:67`): jansson.

(exiv2/lcms2/sentry/jansson are listed in two packages each — both citations are live and must move together.)

Inter-ext cross-deps (move when the producer is rewritten):
- `//ext/tiff:tiff` → jpeg `:73`, png `:74`, webp `:75`, `@xz//:lzma` `:76`, `@zstd` `:77`. (In the native rewrite, tiff deps become `@libjpeg_turbo`, `@xz//:lzma`, `@zstd`, `@libwebp`, `@zlib`, `@libdeflate` — **png is dropped**, libtiff has no libpng codec.)
- `//ext/exiv2:exiv2` → `@libexpat` `:55`, png `:56`, `@zlib` `:57`. (Native rewrite: `@libexpat` + `@zlib` only — **png dropped**, exiv2 PNG support uses zlib.)
- `//ext/lcms2:lcms2` → jpeg `:36`, tiff `:37`. (Native rewrite: **both dropped** — they only feed lcms2's CLI tools, not the core lib.)
- `//ext/png:png` → `@zlib` `:84`. `//ext/sentry:sentry` → `@curl`, `@openssl//:crypto`, `@openssl//:ssl` `:50-52`.

`MODULE.bazel` `http_archive` blocks (the url/sha/strip_prefix source of truth): exiv2 `541-547`, jansson `560-566`, jbigkit `568-574`, jpeg `576-582`, lcms2 `584-590`, png `592-602`, sentry `604-610`, tiff `612-618`, webp `620-626`. All use `build_file = "//bazel:all_content.BUILD.bazel"`. `bazel_dep(rules_foreign_cc)` at `:38`; `register_toolchains(preinstalled_*)` at `:54-59`. **`use_repo_rule http_archive` at `:243` must survive** (the FFmpeg-static pull at `268-310` uses it).

> **BCR target labels (confirmed against the local BCR checkout, 2026-06-17):** `@libpng//:png` (+ a `:libpng` alias), `@libjpeg_turbo//:jpeg` (the classic ≤v8 API target SIPI uses; `:turbojpeg` is the separate TurboJPEG API we don't), `@libwebp//:libwebp`, and `libdeflate@1.25` (present). Re-confirm with `bazel query '@<dep>//...'` at impl if a version is bumped.

### libjpeg-turbo vs IJG libjpeg 9f — equivalence (the jpeg golden gate)

- **API/ABI: safe (verified).** `src/formats/SipiIOJpeg.cpp` calls only classic ≤v8 API (`jpeg_set_defaults`, `jpeg_set_quality`, `jpeg_simple_progression`, `jpeg_save_markers`, `jpeg_write_marker`, `jpeg_read_scanlines`, …). No libjpeg-9-only symbols. Drop-in at the symbol level; no compile break expected.
- **Decode: ~±1 LSB drift, not bit-identical.** Turbo's SIMD IDCT is within ±1 of the IJG reference on most pixels. SIPI's decode→process→encode can shift output pixels even for inputs it merely reads.
- **Encode: bytes differ outright.** Different Huffman/SIMD → different compressed bytes for the same pixels.

So the jpeg re-baseline needs a **tolerance gate**, not byte-exact: regenerate goldens, then assert decoded old-golden-vs-new-output **max per-channel |Δ| ≤ a documented ~1–2 LSB bound** (a larger delta is a regression, not a re-baseline). Encode-byte changes are expected and localized with `cmp -l`. The **Phase-0-corrected** `sipi compare` (true absolute per-channel delta) is the metric; ImageMagick `compare -metric AE` is the external cross-check. Record the bound + result in `test/approval/CHANGELOG.approval.md`. (See ADR-0002 for the determinism gate; `Icc::iccBytes()` chokepoint + `SOURCE_DATE_EPOCH=946684800`.)

### Golden re-baseline scope (which deps change output bytes)

- **jpeg** (libjpeg_turbo): certain encode-byte diff + decode drift → tolerance-gated re-baseline.
- **tiff** (native, with codecs enabled): re-baseline on linux-x86_64 (canonical host, DEV-6545) with reviewer sign-off; localize with `cmp -l`.
- **png** (1.6.41→1.6.54), **webp** (1.2.0→1.6.0): version bumps — **verify goldens explicitly, do not assume zero-delta** (libpng compression-default or libwebp codec changes can shift bytes).
- **lcms2** (2.16, unchanged version, same source): ICC bytes funnel through `Icc::iccBytes()` — expect no delta, but verify.
- **exiv2 / sentry / jansson / jbigkit / kakadu**: no pixel-output surface from the build change (kakadu source is unchanged — same v8.7 release; only its build rule changes → expect byte-identical J2K, verify with a decode-parity check).

### Per-library native-rule specifics (from research; effort + risk)

- **jbigkit 2.1 — native `cc_library`, effort S; wired into libtiff to enable JBIG-in-TIFF.** Trivial: `libjbig/jbig.c` + `libjbig/jbig_ar.c` → `libjbig.a`; hdrs `jbig.h`/`jbig_ar.h` (the T.85-only `jbig85.c`/`.h` go unused). No config header, no deps. Wire into the vendored libtiff (Phase 6): `JBIG_SUPPORT=1` in the `tiffconf.h` defines + jbigkit in `tiff_impl.deps` (the overlay already lists `tif_jbig.c` in srcs). The direct `//ext/jbigkit:jbigkit` link at `src/BUILD.bazel:320` becomes transitive via tiff (drop the redundant direct entry). **Licensing:** jbigkit's library is GPL; SIPI is **AGPL-3.0** (GPL-compatible) and already links it today, so this is clean — verify jbigkit is GPLv2-or-later/GPLv3 (not GPLv2-only) for strict AGPLv3 compatibility. JBIG is a rare bi-level (fax/scan) codec; this is a deliberate **capability add** for archive completeness, not a regression fix.
- **lcms2 2.16 — native, effort S.** Core `liblcms2` is a fixed `src/*.c` list (`cmscnvrt, cmserr, cmsgamma, … cmsalpha`), **no generated config header**, **no jpeg/tiff dep** (those only fed the CLI tools — the current recipe links them needlessly). Public headers `include/lcms2.h` + `lcms2_plugin.h` are checked in upstream. Risk: none material.
- **jansson 2.13.1 — native, effort S.** One generated header `jansson_config.h` from the **CMake** template `cmake/jansson_config.h.cmake` (not the autotools `.h.in`). Tokens: `JSON_INLINE=inline`, `JSON_INT_T=long long`, `JSON_STRTOINT=strtoll`, `JSON_INTEGER_FORMAT="lld"`, `JSON_HAVE_LOCALECONV/ATOMIC_BUILTINS/SYNC_BUILTINS=1`, `HAVE_STDINT_H/INTTYPES_H/SYS_TYPES_H=1` — all platform-invariant under clang, no `select()`. Srcs `src/*.c`, no deps. Risk: pick the CMake template.
- **sentry-native 0.9.0 (inproc + curl) — native, effort M–L.** No generated config header. Selection is file-list + defines: core `src/sentry_*.c` + `path/sentry_path.c` + `transports/sentry_disk_transport.c` + `unwinder/sentry_unwinder.c`; inproc backend `backends/sentry_backend_inproc.c` (`-DSENTRY_BACKEND_INPROC`); curl transport `transports/sentry_transport_curl.c` (`-DSENTRY_TRANSPORT_CURL`). Platform `select()`: path `sentry_path_unix.c` (both); modulefinder `sentry_modulefinder_linux.c` (Linux) vs `_apple.c` (macOS); unwinder `sentry_unwinder_libbacktrace.c` (default incl. macOS + glibc-Linux) vs `_libunwind.c` (musl only). Deps `@curl` + `@openssl` (the `OPENSSL_ROOT_DIR` staging block disappears). **Risk:** the unwinder/modulefinder `select()` must match CMake exactly — a wrong pick is a *silent crash-capture failure*, not a build error. Gate with a crash-capture smoke test. Confirm `backtrace()` resolves under hermetic-llvm Linux.
- **exiv2 0.28.5 — native, effort L (the hardest).** Two generated headers: `exv_conf.h` (from `cmake/config.h.cmake` via `cmake_configure_file`) and `exiv2lib_export.h` (CMake `generate_export_header` — for a static build the `EXIV2API` macros expand to empty, so **check in a ~20-line static shim** rather than reproduce it). `exv_conf.h` tokens: `EXV_ENABLE_FILESYSTEM`, `EXV_ENABLE_WEBREADY`, `EXV_ENABLE_NLS=off`, `EXV_ENABLE_BMFF`, `EXV_ENABLE_INIH=off`, `EXV_HAVE_LIBZ=on`, `EXV_HAVE_BROTLI=off`, `EXV_HAVE_XMP_TOOLKIT=on` (XMP folded into `libexiv2.a` in 0.28), `EXV_USE_CURL=off`, version macros, plus platform `select()` tokens (`EXV_HAVE_ICONV` macOS-on/Linux-off, `EXV_HAVE_STRERROR_R`, `EXV_STRERROR_R_CHAR_P`, `ICONV_ACCEPTS_CONST_INPUT`). Srcs: ~40+ `src/*.cpp` **plus the bundled `xmpsdk/src/*.cpp`**. Deps `@libexpat` + `@zlib`. `std::filesystem` is in libc++'s main lib (no `-lc++fs`; the foreign_cc `FindFilesystem` probe hack vanishes). **Risk:** breadth of the source list + XMP SDK; `exv_conf.h` token drift on the next bump; export-macro correctness (wrong static/shared macro → hidden-visibility link errors). Also the canonical cross-compile host-leak risk (`<features.h>`). **Verify EXIF/IPTC/XMP/ICC round-trip** after the swap.
- **tiff (native, copy BCR overlay) — effort M.** Bump `http_archive(tiff)` to 4.7.1 (match the overlay source); vendor the overlay's `BUILD.bazel` into `ext/tiff/`, and customize: add `@libjpeg_turbo`/`@xz//:lzma`/`@zstd`/`@libwebp`/`//ext/jbigkit:jbigkit` to `tiff_impl.deps`; add `JPEG_SUPPORT=1`/`LZMA_SUPPORT=1`/`ZSTD_SUPPORT=1`/`WEBP_SUPPORT=1`/`JBIG_SUPPORT=1` to the `tiffconf.h` `cmake_configure_file` defines. Deps also include `@zlib` + `@libdeflate`. **Drop png from tiff deps** (not a libtiff codec). `tif_dir.h` stays private (the dead `#include` in `SipiIOTiff.cpp:21` is removed — see cleanup). The `mkg3states` escape hatch + the darwin link-wrapper for `libtiffxx` both disappear (no foreign_cc). **Re-baseline TIFF goldens; e2e codec-coverage proof** (WebP-in-TIFF + ZSTD + Deflate/LZW + CCITTFAX4 write; JPEG-in-TIFF + LZMA + JBIG decode on read).
- **Kakadu v8.7 — native, effort XL (largest — Phase 3, first dep migration).** Today `bazel/kakadu.BUILD.bazel` wraps a `rules_foreign_cc` `make()` whose `postfix_script` runs `make -j1 -f Makefile-<arch>-gcc … all_but_jni` to produce `libkdu.a` + `libkdu_aux.a` (static only — no exe link, which is why it survives the hermetic toolchain). The fetch (`bazel/kakadu_extension.bzl` + `bazel/gh_release.bzl`, the `gh release download` repository_rule) is **not** foreign_cc and **stays**. Convert only the build: enumerate `coresys/` + `apps/` (the `kdu_aux` support) sources, set the per-**target**-arch SIMD `copts`/defines (AVX2 x86 / NEON arm, `CORESYS_COMMON`) via `select()` on `//platforms:is_*`, into a native `cc_library` exposing `@kakadu//:kdu`. This **removes the two `Makefile-*-clang.patch` files** (the native rule doesn't use Kakadu's makefiles) — derive the exact src/flag set from those makefiles + `coresys/make/`. **Feasibility is given, not in question** — Kakadu builds today from its own makefiles (`Makefile-{Linux-x86-64,Linux-arm-64}-gcc` + `coresys/make/`), which are the authoritative source/flag spec; the work is *translating* that into `cc_library` srcs + per-target `copts`/`local_defines`, not discovering whether it can build. **No fallback** (maintainer: not an option). The risk is therefore effort + SIMD-dispatch correctness + getting the exact src/flag set right — all derivable from the working makefiles. **Decode-parity gate:** the J2K source is unchanged (same v8.7 release) → output must be byte-identical; prove with a J2K decode/encode parity run + the existing approval goldens.

### Hermetic-toolchain interaction (a clean win)

Native `cc_library` deps are first-class Bazel targets compiled by the same hermetic LLVM 22 / libc++ toolchain as SIPI itself — **no foreign_cc configure-time link probe**, so they entirely sidestep the libc++/exe-link failures ADR-0014 patched (the exiv2 `FindFilesystem` pre-seed, the tiff `mkg3states` skip, the darwin link-wrapper, the `llvm-ar`/`llvm-ranlib` archiver overrides). Each migrated lib must build green under hermetic LLVM 22 on all three platforms (darwin-aarch64, linux-x86_64, linux-aarch64); the expectation is they "just work" because they never invoke the failing foreign_cc paths. macOS xcrun/SDK-probe hazards also vanish (those are foreign_cc + CMake behaviors).

## Implementation Phases

Each phase is an independently-revertable PR. A foundational `getPixel`/`setPixel` correctness fix (Phase 2) precedes the dependency work. **Kakadu goes first among the dependency migrations** (Phase 3, maintainer direction) — it is the largest conversion and gates both the `rules_foreign_cc` deletion *and* the cross-compile payoff, so landing it native up front retires the dominant effort. It builds from its own makefiles today, so this is a translation into Bazel, not a feasibility bet — and there is no fallback. Then the BCR drop-ins and easy native rules, the harder native rules (tiff codecs, sentry crash-capture, exiv2 metadata), the `rules_foreign_cc` deletion milestone (Phase 9), and finally the four capability gates: cross-compile (10), coverage (11), asan (12), fuzz (13). Per-lib/per-codec PRs keep any golden change unambiguously attributable.

#### Phase 0: Fix the `sipi compare` pixel-delta defect — DONE

Prerequisite for the Phase-4 jpeg tolerance gate. Landed as `f7e397a`.

- [x] Replace the unsigned per-channel subtraction with an **absolute** difference so `maxdiff` is the true max |Δ| and `avg` the mean |Δ|. _(Extracted into `SipiImage::maxPixelDelta`; also fixed the `img1 -= img2` mutation that corrupted avg/max.)_
- [x] Regression unit test (both-direction deltas; 8/16-bit; identical; incomparable→nullopt). _(`test/unit/sipiimage/pixel_delta_regression_test.cpp`.)_
- [x] Commit `fix(cli): sipi compare per-channel delta uses absolute difference`.

> Tracked as [DEV-6643](https://linear.app/dasch/issue/DEV-6643). A shipped-tool correctness bug independent of this migration.

#### Phase 1: tiff codec-gap decision — RESOLVED

The discovery (verified): stock BCR `libtiff@4.7.1` ships JPEG/LZMA/ZSTD/WebP-in-TIFF **disabled**; `tif_dir.h` is private (and SIPI's only use is a dead `#include`). The decision, per the maintainer's archive constraint ("we get many formats and types of TIFFs; if it is part of the spec and allowed, we will have to deal with it") and the drop-in-or-vendor rule:

- [x] **Asset-corpus question** — answered by the maintainer: SIPI is an archive and must ingest any spec-allowed TIFF, so JPEG/LZMA/ZSTD/WebP-compressed TIFF inputs are in scope. Accept-limitation is **not** viable.
- [x] **`_TIFFFieldArray` need** — confirmed dead: the only uses (`SipiIOTiff.cpp:346,2154`) are commented out; the `#include "tif_dir.h"` at `:21` has no live consumer. Drop the include + commented lines; no vendoring/patching of `tif_dir.h` needed.
- [x] **Decision** — **vendor a native libtiff `cc_library` with all four codecs re-enabled** (path 1, copy-and-customize the BCR overlay). Recorded here; mechanics in the tiff phase.

#### Phase 2: `getPixel`/`setPixel` pixel-index transposition fix

> Tracked as [DEV-6647](https://linear.app/dasch/issue/DEV-6647). Surfaced during the Phase-0 `sipi compare` fix. Sequenced **before** the dependency migrations: a correct `SipiImage` pixel-accessor foundation precedes the codec work whose verification leans on pixel comparison. (Phase 0's `maxPixelDelta` already reads the raw row-major buffer, so `sipi compare` is correct; this fixes the accessors themselves.)

`getPixel`/`setPixel` index `nc * (x * nx + y) + c` while the rest of `SipiImage` (format handlers, `operator-=`, `operator==`, `compare()`, `maxPixelDelta`) is row-major `nc * (y * nx + x) + c` — transposes that diverge for non-square images.

- [x] Change `getPixel`/`setPixel` to row-major `nc * (y * nx + x) + c`.
- [x] Audit call sites for logic that compensated for the transposition (self-cancelling pairs). _(No production callers — the only first-party caller is the unit test; no compensating logic to undo. Stale transposition comments in `PixelDelta` doc, `maxPixelDelta`, and the delta-regression test were corrected.)_
- [x] Non-square round-trip regression test. _(`test/unit/sipiimage/pixel_accessor_regression_test.cpp` — setPixel↔row-major-store via the `maxPixelDelta` oracle (verified red on the pre-fix code) + get/set round-trip on non-square 8/16-bit.)_

#### Phase 3: `Kakadu` → native `cc_library` (first dep migration — the gating item)

> Sequenced first among the dependency migrations by maintainer direction. Kakadu gates both `rules_foreign_cc` deletion and the cross-compile payoff; landing it native up front retires the dominant effort. **No fallback** — and since Kakadu builds from its makefiles today, native Bazel is achievable (translation, not discovery). As the **hardest** native `cc_library` conversion, Kakadu *is* the proof of the native approach — the remaining libs (tiff/lcms2/jansson/sentry/exiv2/jbigkit) are strictly easier and follow the same shape, so no later phase needs to "prove the pattern."

- [x] **Translate the working makefile build into a `cc_library`** (no fallback). The per-arch `Makefile-{Linux-x86-64,Linux-arm-64}-gcc` + `coresys/make/` are the authoritative source/flag spec — Kakadu builds from them today, so feasibility is given; this is translation. Spike first: get a single-platform `libkdu.a` linking + a J2K decode, then fan out to all three platforms. _(Done: darwin-aarch64 spike (build + `sipi` link + approval) then fanned out — linux-x86-64 + linux-arm-64 cross-compile from darwin. PR #674.)_
- [x] Keep the `gh_release_archive` fetch (`bazel/kakadu_extension.bzl` + `bazel/gh_release.bzl`); replace the foreign_cc `make()` in `bazel/kakadu.BUILD.bazel` (today `targets=[]` + a `postfix_script` running `make -j1 -f Makefile-<arch>-gcc … all_but_jni` → `libkdu.a` + `libkdu_aux.a`, static-only) with a `cc_library`. Enumerate `coresys/` + `apps/` (`kdu_aux`) sources + per-**target**-arch SIMD `copts`/defines (AVX2 x86 / NEON arm, `CORESYS_COMMON`) via `select()` on the target platform, derived from `Makefile-{Linux-x86-64,Linux-arm-64}-gcc` + `coresys/make/`. Expose `@kakadu//:kdu` (+ `kdu_aux`). **Remove the two `Makefile-*-clang.patch` files** (the native rule doesn't use the makefiles). _(Done. libkdu.a = coresys GENERIC+SIMD + prebuilt HT archive; libkdu_aux.a = managed ALL_OBJS; x86 SIMD ISAs are per-flag `alwayslink` sub-libs; patches deleted.)_
- [x] **Decode-parity gate**: same v8.7 source → byte-identical J2K output expected; prove with a J2K decode/encode parity run against `SipiIOJ2k.cpp` + the approval goldens. ~~**J2K microbench before/after** (the SIMD path is perf-critical).~~ 3-platform CI green. _(Parity proven: approval goldens pass byte-identical. Microbench gate waived by maintainer 2026-06-17 — identical source + `-O3`/SIMD flags. 3-platform CI green on PR #674: darwin-arm64 + linux-amd64 + linux-arm64 all pass.)_

#### Phase 4: BCR drop-ins (png/jpeg/webp) + shared native deps

> **Execution note:** to avoid the throwaway transitional dep-wiring (last bullet), the phases were reordered **natives-first** — each png/jpeg/webp drop-in folded into the commit that retired its last foreign_cc consumer (jpeg+webp with tiff; png with exiv2). No foreign_cc recipe ever consumed a BCR `cc_library`. See ADR-0015 / the PR.

- [x] Add `bazel_dep(name = "cmake_configure_file", version = "0.1.7")` and `bazel_dep(name = "libdeflate", version = "1.25")`. Confirm `rules_cc` 0.2.19 is present. _(cmake_configure_file in `8ec8316`; libdeflate in `f14d6fa`; rules_cc 0.2.19 present.)_
- [x] **png → `bazel_dep(libpng, 1.6.54)` (`@libpng//:png`)**: delete the `http_archive` + `ext/png/`; rewrite consumers. **verify PNG goldens** (1.6.41→1.6.54 bump). _(Done in `e96b554` — folded into Phase 8 since exiv2 was png's last foreign_cc consumer; goldens **byte-identical**, no re-baseline needed.)_
- [x] **webp → `bazel_dep(libwebp, 1.6.0)` (`@libwebp//:libwebp`)**: delete `http_archive` + `ext/webp/`; rewrite consumers. _(Done in `f14d6fa` — folded into Phase 6; SIPI emits no direct WebP so the direct sipi_lib link is dropped, transitive via @tiff; WebP-in-TIFF covered by the codec test.)_
- [x] **jpeg → `bazel_dep(libjpeg_turbo, 3.1.3.bcr.5)` (`@libjpeg_turbo//:jpeg`)**: API-compat pre-check; delete `http_archive` + `ext/jpeg/`; rewrite consumers. **Tolerance-gated re-baseline**. _(Done in `f14d6fa` — folded into Phase 6 (no-duplicate-libjpeg link constraint). 8 goldens re-baselined: decode drift max |Δ|=6/avg 0.30; lossy re-encode max 21–44/avg ≤3.4; maintainer sign-off; `CHANGELOG.approval.md`. EXIF/IPTC/ICC round-trip green.)_
- [x] ~~**Transitional dep-wiring**~~ — **avoided** by the natives-first reordering above; no foreign_cc recipe ever consumed a BCR dep.

#### Phase 5: Native `cc_library` rules — jbigkit, lcms2, jansson

- [x] **jbigkit → native `cc_library`**: replace `ext/jbigkit/BUILD.bazel`'s foreign_cc `make()` with a `cc_library` (`libjbig/jbig.c` + `jbig_ar.c`; hdrs `jbig.h`/`jbig_ar.h`; no deps). Keep the `http_archive`. Wired into libtiff in Phase 6; the direct `src/BUILD.bazel:320` link becomes transitive via tiff (drop the redundant direct entry then). GPL, fine under SIPI's AGPL-3.0. _(Done: `bazel/jbigkit.BUILD.bazel`, `@jbigkit//:jbig`; commit `8ec8316`.)_
- [x] **lcms2 → native `cc_library`**: `src/*.c`, no config header, **no jpeg/tiff deps** (CLI-only). Verify ICC round-trip (`Icc::iccBytes()` goldens unchanged). Rewrite `src/BUILD.bazel:315` + `src/metadata/BUILD.bazel:85`. _(Done: `bazel/lcms2.BUILD.bazel`, `HAVE_GMTIME_R=1`; approval goldens byte-identical; commit `8ec8316`.)_
- [x] **jansson → native `cc_library`**: `cmake_configure_file` on `cmake/jansson_config.h.cmake`; `src/*.c`; no deps. Rewrite `src/BUILD.bazel:325` + `src/shttps/BUILD.bazel:67`. _(Done: also needs `cmake/jansson_private_config.h.cmake` (HAVE_CONFIG_H); `bazel/jansson.BUILD.bazel`; commit `8ec8316`.)_

#### Phase 6: `tiff` → native `cc_library` (copy BCR overlay + enable codecs)

- [x] Bump `http_archive(tiff)` to 4.7.1; vendor the BCR `libtiff/4.7.1/overlay/BUILD.bazel` into `bazel/tiff.BUILD.bazel`. _(`f14d6fa`. Lives in `bazel/` not `ext/`, matching the kakadu precedent.)_
- [x] Customize: add `@libjpeg_turbo` + `@xz//:lzma` + `@zstd` + `@libwebp//:libwebp` + `@jbigkit//:jbig` to `tiff_impl.deps`; add the five `*_SUPPORT` defines. **Drop png**; drop the redundant direct jbigkit link. _(`f14d6fa`. Token placement per 4.7.1 templates: JPEG/JBIG in tiffconf.h, LZMA/ZSTD/WEBP in tif_config.h.)_
- [x] Remove the dead `#include "tif_dir.h"` from `src/formats/SipiIOTiff.cpp`. _(`f14d6fa`; the `_TIFFFieldArray` uses were already commented out pre-work.)_
- [x] Rewrite consumer `src/BUILD.bazel` → `@tiff//:tiff`; delete the old foreign_cc recipe (removes the `mkg3states` hatch + darwin link-wrapper). _(`f14d6fa`.)_
- [x] **Re-baseline TIFF goldens** — none needed: native libtiff 4.7.1 output is byte-identical (TiffRegionRoundTrip/CmykTiffDownscaled/TiffToPng pass unchanged); only the JPEG-decode-affected TIFF goldens shifted, attributed to libjpeg_turbo (Phase 4). _(`f14d6fa`.)_
- [x] **e2e codec-coverage proof** — new `//test/unit/tiff_codecs`: asserts all 8 codecs `TIFFIsCODECConfigured` (CCITTFAX4/LZW/Deflate/JPEG/LZMA/ZSTD/WEBP/JBIG) + RGB-lossless round-trip for ZSTD/LZMA/LZW/Deflate. _(`f14d6fa`.)_
- [ ] Open the codec-enable patch upstream to `bazel-central-registry` (so the vendored overlay can later collapse to a `bazel_dep` override-free). _(Follow-up — external contribution, not required for foreign_cc=0.)_

#### Phase 7: `sentry` → native `cc_library`

- [x] File-list + defines per the inproc+curl selection; `select()` for modulefinder (linux/apple); libbacktrace unwinder; deps `@curl` + `@openssl`. _(`060058e`. Also needed: vendored `mpack.c`/`stb_sprintf.c`, `sentry_screenshot_none.c`, and defines `SENTRY_HANDLER_STACK_SIZE=64`/`SIZEOF_LONG=8`. OPENSSL_ROOT_DIR staging gone.)_
- [x] **Crash-capture smoke test** — new `//test/unit/sentry_smoke`: inits the inproc backend, captures a live stack via the libbacktrace unwinder (`add_stacktrace`, NULL ips), and asserts the event reaches a transport. `backtrace()` resolves. _(`060058e`. Full SIGSEGV capture remains validated by the deployed integration.)_

#### Phase 8: `exiv2` → native `cc_library`

- [x] `cmake_configure_file` on `cmake/config.h.cmake` → `exv_conf.h` (tokens matched to the foreign_cc-generated darwin header; `EXV_HAVE_ICONV` macOS-only via `select()`); static `exiv2lib_export.h` shim via genrule; srcs `src/*.cpp` + `xmpsdk/src/*.cpp` (`.incl_cpp` in `textual_hdrs`); `-std=gnu++17`; deps `@libexpat` + `@zlib`. **png dropped** — verified exiv2 links zlib not libpng (`EXIV2_ENABLE_PNG` → `ZLIB::ZLIB`; no `<png.h>`/`png_*`). Rewrite consumers. _(`e96b554`. http.cpp excluded (WEBREADY off); impl/public split.)_
- [x] **Verify EXIF/IPTC/XMP/ICC round-trip** preserved. _(unit 17/17 + approval byte-identical + MetadataGolden.* (ImgExifGps/GrayWithIcc/ImageOrientation) green; `e96b554`.)_

#### Phase 9: Delete `rules_foreign_cc` + documentation — **foreign_cc = 0 milestone**

- [x] Remove `bazel_dep(rules_foreign_cc)` + `register_toolchains(preinstalled_*)` + the header/dead-dep comments. **Keep** `use_repo_rule http_archive` and the hermetic-llvm `use_repo` lines. _(`8a63c9c`.)_
- [x] Delete `bazel/foreign_cc_helpers.bzl`, `ar_wrapper.sh`, `all_content.BUILD.bazel`, the `llvm-ar`/`llvm-ranlib`/`clang++` aliases + `ar_wrapper.sh` export. Keep the objcopy/readelf/cov/profdata + glibc23_compat aliases. Remove the dead `patches/` files. _(`8a63c9c`. Deleted the **whole** `patches/` dir — all 5 patches were unreferenced foreign_cc-era dead code and the BUILD exported two already-deleted files.)_
- [x] `grep -c rules_foreign_cc MODULE.bazel` → 0; `grep -rc foreign_cc bazel/` → 0. _(`8a63c9c`. Also scrubbed every other repo-wide reference — CI workflows, flake.nix, .bazelrc, docs, BUILD comments — at the maintainer's "clean all" request; remaining mentions are ADR-0015 pointers + append-only ADR/changelog history.)_
- [x] Update `CLAUDE.md`, `docs/src/development/{bazel,building,developing,nix}.md`, ADR-0014 Consequences; write **ADR-0015**. _(`8a63c9c`.)_
- [ ] **Build-cost capture**: per-library critical-path contribution from the Bazel JSON trace profile. _(Follow-up — measurement task, not required for foreign_cc=0; deferred.)_

#### Phase 10: Cross-compilation works (macOS → Linux)

- [ ] Verify each native dep cross-compiles linux-amd64 + linux-arm64 from a darwin-aarch64 host with **no host-include leaks** (the classic: exiv2 `<features.h>`); fix any rule pulling host `/usr/include` (toolchain headers only).
- [ ] Confirm Kakadu's per-**target**-arch SIMD `select()` (from Phase 3) keys on the *target* platform under a darwin exec host.
- [ ] **`bazel build --platforms=//bazel/platforms:linux_amd64 //src:image` green on a Mac** (and `:linux_arm64`). No `target_compatible_with` change — the `@platforms//os:linux` gate (`src/BUILD.bazel:825`) is correct and stays. Add (or extend) a CI job that cross-builds the OCI image from darwin so it can't silently regress.

#### Phase 11: Coverage re-armed (`coverage.yml`) — **STRETCH GOAL**

> **Stretch, not a deal-breaker.** Coverage never worked correctly to begin with (empty lcov, LF=0), and is gated on upstream **rules_cc #385**, which we don't control. The other three capabilities (cross, asan, fuzz) worked before the swap and are full deliverables; coverage is explicitly optional and slips independently. **The plan completes without this phase.**

- [ ] Track **rules_cc #385**. If/when it merges and `llvm_coverage_map_format` activates, restore the `push: branches:[main]` trigger in `coverage.yml` (the `llvm-cov`/`profdata` bundle repoint is already done) and verify a **non-empty lcov** on linux-amd64 (native deps now instrumented; foreign_cc archives were opaque).
- [ ] Until then: leave `coverage.yml` on `workflow_dispatch`, re-arm condition tracked in ADR-0014. This does **not** gate foreign_cc=0 or the cross/asan/fuzz outcomes.

#### Phase 12: ASan/UBSan re-armed (`sanitizer.yml`)

- [ ] Confirm foreign_cc=0 (Phase 9) removed blocker (b): `--@llvm//config:asan=true` (global instrument) no longer breaks any compile (no foreign_cc CMake probes left).
- [ ] Provision the compiler-rt **ASan/UBSan runtimes** (blocker (a)): a runtime-carrying hermetic-llvm bundle, or built/fetched compiler-rt; resolve the darwin `libclang_rt.asan_osx_dynamic.dylib` naming gap (independent of foreign_cc).
- [ ] Restore the `pull_request:` merge gate in `sanitizer.yml`; verify `--config=asan --config=ubsan //src/cli:sipi` builds and the suite runs green on linux-x86_64 (the deps now instrumented too).

#### Phase 13: Fuzz re-armed (`fuzz.yml`)

- [ ] Provision the **libFuzzer runtime** (same runtime-carrying bundle as Phase 12; same global-instrument unblock from foreign_cc=0).
- [ ] Restore the nightly `schedule:` in `fuzz.yml`; verify the `//fuzz/handlers:*` harness builds + runs on linux-x86_64 (CI) and darwin-aarch64 (local).

## Alternative Approaches Considered

- **Use stock BCR everywhere (incl. libtiff).** Rejected — stock BCR libtiff drops JPEG/LZMA/ZSTD/WebP-in-TIFF; an ingestion regression for an archive. This is the choice the decision rule explicitly forbids.
- **Keep `foreign_cc` for the non-BCR libs (the original plan's "stop at png/jpeg/webp").** Rejected by the maintainer: it leaves the foreign_cc machinery (hermetic hatches, serial builds, the darwin shims) in place and does not unlock cross-compilation — "we need a solution that solves all foreign_cc usage."
- **Replace codecs with Rust crates now.** Rejected for the build-pain problem: `tiff`/JPEG2000 crates can't encode tiled/pyramid/JPEG-in-TIFF or do HTJ2K; `lcms2`/`exiv2` have only C bindings; a single-lib FFI swap adds a marshalling boundary for little gain. Rust adoption belongs to the strangler-fig (DEV-5970), module-by-module.
- **Big-bang single PR.** Rejected: golden-drift attribution and review tractability require per-lib PRs; libjpeg_turbo's certain bit-diff and Kakadu's SIMD risk must each be isolated.
- **Kakadu: keep the foreign_cc `make()`, or use a non-foreign_cc make-runner.** Both rejected (maintainer). Keeping `make()` blocks deleting `rules_foreign_cc`; a make-runner is a lateral move that wouldn't unlock cross-compile. Kakadu goes **native `cc_library`** — it builds from its makefiles today, so a Bazel `cc_library` is achievable; there is no fallback.

## File Manifest

- `MODULE.bazel` — add `bazel_dep` cmake_configure_file 0.1.7, libdeflate 1.25, libpng 1.6.54, libjpeg_turbo 3.1.3.bcr.5, libwebp 1.6.0; bump `http_archive(tiff)` → 4.7.1; delete `http_archive` blocks for png/jpeg/webp (jbigkit's `http_archive` **stays** — now a native `cc_library`); delete `bazel_dep(rules_foreign_cc)` + `register_toolchains(preinstalled_*)` (Phase 9). Keep `use_repo_rule http_archive` + hermetic-llvm `use_repo`.
- `ext/png/`, `ext/jpeg/`, `ext/webp/` — **deleted**. `ext/tiff/`, `ext/lcms2/`, `ext/jansson/`, `ext/sentry/`, `ext/exiv2/`, `ext/jbigkit/` — foreign_cc recipe **replaced** by native `cc_library` (+ checked-in config headers / export shims); jbigkit wired into the native libtiff (`JBIG_SUPPORT`).
- `bazel/kakadu.BUILD.bazel` — foreign_cc `make()` → native `cc_library` (Phase 3, first dep migration). `bazel/kakadu_extension.bzl` + `bazel/gh_release.bzl` — unchanged (fetch stays). `patches/kakadu-Makefile-*-clang.patch` — deleted (native build doesn't use the makefiles); `patches/kakadu-makefile-{apps,coresys,managed}.patch` — deleted (already vestigial).
- `bazel/foreign_cc_helpers.bzl`, `bazel/ar_wrapper.sh`, `bazel/all_content.BUILD.bazel` — **deleted** (Phase 9). `bazel/BUILD.bazel` — `:30-51` aliases + `ar_wrapper.sh` export deleted; `:63-117` kept.
- `src/BUILD.bazel` — consumer rewrites `:315-336`. `src/metadata/BUILD.bazel:84-85`, `src/observability/BUILD.bazel:54`, `src/shttps/BUILD.bazel:67` — consumer rewrites.
- `src/formats/SipiIOTiff.cpp:21` — remove the dead `#include "tif_dir.h"` + commented `_TIFFFieldArray` lines.
- `test/approval/**` goldens + `CHANGELOG.approval.md` — re-baselined for jpeg (tolerance gate) and tiff; verified-unchanged for png/webp/lcms2/kakadu.
- `CLAUDE.md`, `docs/src/development/{bazel,building}.md`, `docs/adr/0014-*.md`, new `docs/adr/0015-*.md`.
- `//src:image` `@platforms//os:linux` gate (`src/BUILD.bazel:825`) — **unchanged** (correct; stays). Cross-build invoked via `bazel build --platforms=//bazel/platforms:linux_amd64 //src:image` (Phase 10).
- `.github/workflows/{coverage,sanitizer,fuzz}.yml` — `on:` auto-triggers restored (Phases 11–13); `MODULE.bazel`/toolchain — compiler-rt asan/ubsan/libFuzzer runtime provisioning (Phases 12–13).

> Indicative, not exhaustive — exact src lists / config-header tokens per native rule are derived at implementation from the local BCR/upstream checkouts.

## Acceptance Criteria

### Functional
- [x] (Phase 0) `sipi compare` reports the true absolute per-channel delta (`sipi.cpp:884` fixed) with a regression test.
- [x] (Phase 1) tiff codec-gap decision recorded: vendor native libtiff with all four codecs re-enabled.
- [x] png/jpeg/webp build via BCR `bazel_dep`; `ext/{png,jpeg,webp}` deleted. _(Phases 4/6/8.)_
- [x] tiff/lcms2/jansson/sentry/exiv2/jbigkit build via native `cc_library` (jbigkit wired into libtiff for JBIG-in-TIFF); all foreign_cc recipes gone. _(Phases 5–8; `ext/` fully removed.)_
- [x] Kakadu builds via native `cc_library` (no fallback); `@kakadu//:kdu` unchanged at the FFI surface. _(All 3 platforms; FFI surface + J2K output byte-identical. PR #674.)_
- [x] `rules_foreign_cc` + `preinstalled_*` toolchains + `bazel/foreign_cc_helpers.bzl` + `ar_wrapper.sh` + `all_content.BUILD.bazel` + the darwin alias shims **deleted**; `grep` confirms zero foreign_cc references. _(`8a63c9c`; grep 0 in MODULE.bazel + bazel/.)_
- [x] 3-platform CI green (darwin-arm64, linux-amd64, linux-arm64) under hermetic LLVM 22. _(PR #675 fully green on `5bdb545`: all 3 platforms + docs + docker-scout; `MERGEABLE`.)_
- [x] TIFF codec-coverage proof: WebP-in-TIFF + ZSTD + Deflate/LZW + CCITTFAX4 write; JPEG-in-TIFF + LZMA + JBIG read. _(`//test/unit/tiff_codecs`: all 8 `TIFFIsCODECConfigured` + ZSTD/LZMA/LZW/Deflate round-trip.)_
- [x] EXIF/IPTC/ICC/XMP round-trip preserved across the libjpeg_turbo swap and the exiv2 native rewrite. _(unit + MetadataGolden.* + approval green.)_
- [x] Sentry crash-capture verified (inproc backend) post-rewrite. _(`//test/unit/sentry_smoke`.)_
- [ ] **Cross-compile (Phase 10):** `bazel build --platforms=//bazel/platforms:linux_amd64 //src:image` (and `:linux_arm64`) builds from a darwin-aarch64 host; covered by a CI job. _(Deferred — Phase 10, follow-up.)_
- [ ] **ASan/UBSan re-armed (Phase 12):** `sanitizer.yml` PR gate restored and green on linux-x86_64, deps instrumented. _(Deferred — Phase 12; needs compiler-rt runtime provisioning.)_
- [ ] **Fuzz re-armed (Phase 13):** `fuzz.yml` harness builds + runs on linux-x86_64 (CI) + darwin-aarch64 (local). _(Deferred — Phase 13; needs libFuzzer runtime provisioning.)_

### Non-Functional
- [x] JPEG goldens re-baselined — **no tolerance gate needed** (the prior "architecture-divergent goldens" diagnosis was wrong; see SESSION HANDOFF). The real blocker was a hard x86 encode crash (libjpeg-turbo SSE2 progressive Huffman), fixed by switching to baseline JPEG (`5bdb545`). Baseline `jchuff` is bit-exact across SIMD, so goldens stay **byte-exact** on all 3 arches — **confirmed**: the 5 re-baselined JPEG-encode goldens (pixel-identical to the prior progressive output per `sipi compare`) pass the byte-exact approval gate on linux-amd64 in CI.
- [x] tiff goldens re-baselined with reviewer sign-off; png/webp/lcms2/kakadu goldens verified unchanged (not assumed). _(tiff needed no re-baseline — byte-identical; png/webp/lcms2/kakadu verified byte-identical.)_
- [x] foreign_cc footprint: **0** (was 9 ext libs + Kakadu).
- [ ] Per-library build-cost recorded before/after via critical-path contribution in the Bazel JSON trace profile. _(Deferred — measurement follow-up.)_

### Quality Gates
- [ ] No transient upstream-mirror failure in 5 consecutive CI runs (BCR CDN replaces png/jpeg/webp source URLs). _(CI-gated on the PR.)_
- [x] No regression in the format-handler robustness corpus (DEV-6249/DEV-6414 class). _(Full e2e suite green on all 3 platforms in CI on `5bdb545`; unit + approval green.)_
- [x] J2K decode/encode parity after the Kakadu rewrite (byte-identical, approval goldens pass on darwin-aarch64). _(Microbench-regression gate waived by maintainer 2026-06-17.)_

### Stretch (not required for completion)
- [ ] **Coverage re-armed (Phase 11)** — contingent on upstream rules_cc #385; `coverage.yml` push trigger restored + non-empty lcov on linux-amd64. Coverage never worked correctly to begin with, so this does **not** gate the plan.

## Success Metrics

- `rules_foreign_cc` dependency: present → **removed**. foreign_cc-built libs: 9 (+Kakadu) → **0**.
- Per-dep BUILD maintenance: 9 hand-written foreign_cc recipes → 3 deleted (png/jpeg/webp now one-line `bazel_dep` each, bumps outsourced to BCR) + 7 native `cc_library` we own (tiff/lcms2/jansson/sentry/exiv2/jbigkit/kakadu).
- **New codec capability:** JBIG-in-TIFF decode, enabled by wiring jbigkit into the native libtiff (SIPI couldn't decode it before).
- All ADR-0014 hermetic-toolchain escape hatches deleted (the tiff `mkg3states` skip, the exiv2 `FindFilesystem` pre-seed, the darwin link-wrapper + archiver overrides).
- Capabilities recovered: `--platforms=//bazel/platforms:linux_amd64 //src:image` builds from a Mac; `sanitizer.yml` + `fuzz.yml` auto-triggers re-armed and green (both worked before the swap). **Stretch:** `coverage.yml` re-armed if rules_cc #385 lands (never worked correctly before — not a gate).
- Measurable build-cost reduction on each migrated lib's critical-path contribution (per-TU parallelism + no `./configure`), read per-library from the Bazel trace profile. Plus: native compiles now flow through sanitizer/coverage instrumentation and the remote cache (foreign_cc actions did not).

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Native config-header drift on future version bumps (the trade-off we accepted) | Medium (per bump) | M | Use `cmake_configure_file` (re-diff the `.cmake.in` on bump); document the token set per lib in the BUILD; these libs bump rarely |
| Kakadu native rewrite effort / SIMD-dispatch correctness | Low–Med | M | Feasibility is given (builds from its makefiles today → translate the per-arch Makefiles into `cc_library` srcs/copts); **sequenced first (Phase 3)**; decode-parity gate (byte-identical) + J2K microbench. **No fallback** — native is required |
| Cross-compile fails on a dep host-leak (e.g. exiv2 `<features.h>`) | M | M | Per-dep cross verification in Phase 10; toolchain-only include paths; `cmake_configure_file` headers avoid host probing |
| Coverage re-arm blocked on upstream rules_cc #385 | Medium | L | Out of our control; Phase 11 delivers the re-arm wiring + tracking, green gate contingent on the merge (under-promised) |
| ASan/UBSan/libFuzzer runtime absent in the minimal hermetic-llvm prebuilt | Medium | M | Phases 12–13 provision compiler-rt runtimes (runtime-carrying bundle or built compiler-rt); foreign_cc=0 already removes the global-instrument blocker; darwin asan-runtime naming gap handled separately |
| Sentry unwinder/modulefinder `select()` mismatch → silent crash-capture gap | Medium | M | Crash-capture smoke test; confirm `backtrace()` under hermetic-llvm Linux; match CMake's platform logic exactly |
| exiv2 source-list / export-macro / XMP-SDK errors; cross-compile `<features.h>` host-leak | Medium | M | Start from the foreign_cc recipe as spec; static export-header shim; EXIF/IPTC/XMP round-trip test; toolchain-only include paths |
| `libjpeg_turbo` vs IJG 9f: encode bytes differ + ~1 LSB decode drift | Certain/Likely | M | Tolerance-gated re-baseline (not byte-exact) via Phase-0 `sipi compare`; decode-parity spot-check; isolate in its PR |
| png 1.6.41→1.6.54 / webp 1.2.0→1.6.0 output delta | M | M | Verify goldens explicitly (do not assume zero-delta) |
| Stock BCR libtiff codec crippling (verified) | Confirmed | H | Resolved by vendoring native libtiff with codecs enabled |
| `bazel mod tidy` strips load-bearing `use_repo` lines after foreign_cc deletion | M | M | Manual verify post-tidy; documented in ADR-0014 |
| BCR drop-in pulls transitive-dep bumps (MVS) | M | L | Resolve with the smallest `single_version_override`, per the protobuf/rules_go precedent |
| Hermetic-toolchain interaction on a migrated lib | L | L | Native `cc_library` avoids the foreign_cc link probe; CI on 3 platforms confirms |

## Resource Requirements

Single IC. Effort ≈ **L–XL (~5–7 weeks)** — the scope is foreign_cc=0 **plus** the four capability gates. Indicative: Phase 2 `getPixel`/`setPixel` fix ~half day; **Phase 3 Kakadu ~1–2+ weeks (dominant effort, first dep migration)**; Phase 4 drop-ins + jpeg re-baseline ~3–4 days; Phase 5 jbigkit/lcms2/jansson ~2 days; Phase 6 tiff (overlay + codecs + re-baseline + e2e) ~2–3 days; Phase 7 sentry ~2–3 days; Phase 8 exiv2 ~3–5 days; Phase 9 cleanup + docs + ADR ~1–2 days; Phase 10 cross-compile ~2–4 days; Phases 12–13 asan/fuzz runtime provisioning ~3–5 days (shared); Phase 11 coverage ~1 day **once rules_cc #385 lands (upstream-gated — may slip past the rest)**. New infra: a runtime-carrying LLVM bundle (or built compiler-rt) for asan/fuzz; otherwise BCR CDN + the existing Cloud Run remote cache + the `cmake_configure_file`/`libdeflate` BCR modules. Local BCR + bazel checkouts available for exact source lists.

## Documentation Plan

- `CLAUDE.md` — External Libraries list + replace "built from source via `rules_foreign_cc` under `ext/<lib>`" with the native-`cc_library` description; foreign_cc-footprint note → 0.
- `docs/src/development/bazel.md` / `building.md` — deps are now `bazel_dep` (png/jpeg/webp) or native `cc_library` (the rest); the `cmake_configure_file` pattern; Kakadu's native build; the cross-compile capability.
- `docs/adr/0014-*` Consequences — note the `mkg3states` / `FindFilesystem` / darwin-link-wrapper hatches removed by the foreign_cc elimination.
- **`docs/adr/0015-native-cc_library-over-foreign_cc.md`** (new) — record the decision rule (drop-in-or-vendor), the `cmake_configure_file` pattern, the config-header maintenance trade-off accepted, and the cross-compilation unlock.

## References & Research

### Local checkouts (for exact source lists / config templates)
- `/Users/subotic/_github.com/bazelbuild/bazel-central-registry` — BCR modules + overlays (libtiff/4.7.1/overlay is the native template; libpng 1.6.54, libjpeg_turbo 3.1.3.bcr.5, libwebp 1.6.0, cmake_configure_file 0.1.7, rules_cc 0.2.19 all present).
- `/Users/subotic/_github.com/bazelbuild/bazel` — Bazel sources.

### Linear
- [DEV-6516](https://linear.app/dasch/issue/DEV-6516) — umbrella: migrate C/C++ deps off http_archive/foreign_cc.
- [DEV-6563](https://linear.app/dasch/issue/DEV-6563) — this plan (scope now: full foreign_cc elimination).
- [DEV-6352](https://linear.app/dasch/issue/DEV-6352) — prometheus-cpp → BCR (the proven `single_version_override` precedent).
- [DEV-6545](https://linear.app/dasch/issue/DEV-6545) — golden re-baseline procedure (linux-x86_64 canonical host).
- [DEV-5970](https://linear.app/dasch/issue/DEV-5970) — Rust strangler-fig (out of scope; the "Rust crates" answer).
- [DEV-6037](https://linear.app/dasch/issue/DEV-6037) / [DEV-6614](https://linear.app/dasch/issue/DEV-6614) — Kakadu → pyramidal-TIFF serving (the eventual "get rid of Kakadu" lever; this plan only de-foreign_cc's its build).
- [DEV-6249](https://linear.app/dasch/issue/DEV-6249) / [DEV-6414](https://linear.app/dasch/issue/DEV-6414) — format-handler robustness corpus (regression guard).
- [DEV-6643](https://linear.app/dasch/issue/DEV-6643) — Phase 0 `sipi compare` fix (done). [DEV-6647](https://linear.app/dasch/issue/DEV-6647) — Phase 2 getPixel/setPixel transposition.

### Internal
- [ADR-0014](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0014-toolchain-provider-swap.md) — hermetic LLVM swap; the foreign_cc link/archiver findings (#1–#4) this plan eliminates; the exec×target toolchain matrix that enables cross-compile.
- [ADR-0002](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0002-icc-profile-determinism-test-only.md) — ICC determinism / `SOURCE_DATE_EPOCH` golden gate.
- `bazel/foreign_cc_helpers.bzl` (6 darwin helper fns — all die), `ext/*/BUILD.bazel` (the 9 recipes), `bazel/kakadu.BUILD.bazel` (the foreign_cc `make()`).
- `src/formats/{SipiIOTiff,SipiIOJpeg,SipiIOPng,SipiIOJ2k}.cpp` — the codec consumers; `SipiIOJ2k.cpp` is the Kakadu FFI surface.

### External research (2026-06-17)
- `cmake_configure_file` BCR module (the recommended native config-header mechanism; the libtiff overlay uses it): https://registry.bazel.build/modules/cmake_configure_file — https://github.com/wep21/cmake_configure_file
- BCR libtiff overlay (native `cc_library` template): https://github.com/bazelbuild/bazel-central-registry/tree/main/modules/libtiff/4.7.1/overlay
- exiv2 config/export headers (`cmake/config.h.cmake`, `generate_export_header`): https://github.com/Exiv2/exiv2 (not on BCR). sentry-native backend/transport selection: https://github.com/getsentry/sentry-native (`src/CMakeLists.txt`; not on BCR). lcms2 core source list: https://github.com/mm2/Little-CMS. jansson CMake config: https://github.com/akheron/jansson (`cmake/jansson_config.h.cmake`). jbigkit layout: upstream `libjbig/Makefile`.
