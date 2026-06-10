# Sipi — IIIF-compatible media server
# Run `just` to list all available recipes.
#
# Build reproducibility invariant: every build-related recipe goes through
# `bazel build` / `bazel test` / `bazel coverage`. CI invokes only
# `just <recipe>` — no inline bazel calls. Bazel's own incremental rebuild
# IS the inner-loop edit/rebuild cycle (`just bazel-build` after a single-
# file edit completes in seconds via the action cache).

# Auto-detect CPU cores
nproc := `nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4`

# List all recipes
default:
    @just --list

#####################################
# Bazel — what CI runs
#
# Every build/test step in CI invokes one of these recipes. `bazelisk`
# resolves the pinned Bazel version from `.bazelversion` and is on PATH
# inside the Nix dev shell (`flake.nix`).
#
# All non-trivial recipes accept `*FLAGS` so callers can pass extra Bazel
# flags positionally — e.g. `--config=asan` for sanitiser runs,
# `--disk_cache=$HOME/.cache/bazel-disk` in CI, `--runs_per_test=3` for
# flakiness gates on the high-load e2e targets.
#
# `--stamp` is on for every recipe whose output reads
# `STABLE_SIPI_VERSION` (set by `tools/workspace_status.sh`). Only the
# `:sipi_version_h` action's cache key depends on `STABLE_*` values, so
# the stamp adds at most one re-link per workspace_status change.
#####################################

# Build sipi. Bazel's fastbuild default — fast incremental rebuilds
# for the inner-loop edit/rebuild cycle. Pass `-c opt`, `-c dbg`,
# `--config=asan`, etc. as positional flags when needed.
bazel-build *FLAGS='':
    bazel build --stamp //src/cli:sipi {{FLAGS}}

# Build + run the full test pyramid (unit + approval + e2e) under
# fastbuild — no coverage instrumentation. CI's linux-arm64 and
# darwin-arm64 matrix entries use this path; only linux-amd64 (which
# uploads to Codecov) runs `bazel-coverage`. Splitting the two avoids
# paying instrumentation overhead (1.5-2x compile, slower runtime,
# flakiness risk on the high-load e2e targets) for arches that don't
# upload anyway.
bazel-test *FLAGS='':
    bazel test --stamp //src/... //test/unit/... //test/approval/... //test/e2e-rust/... {{FLAGS}}

# Build + run all tests (unit + approval + e2e) under coverage
# instrumentation; emit combined lcov at
# `bazel-out/_coverage/_coverage_report.dat` for `codecov-action@v6`
# to consume directly — no cobertura conversion.
#
# `instrumentation_filter` keeps measurement scoped to first-party
# code (`//src`, which now subsumes the shttps module at //src/shttps),
# excluding test bodies and ext/* deps. Coverage spans unit + approval +
# e2e, so the reported % includes HTTP and Lua paths the unit suite cannot
# reach.
#
# `COVERAGE_GCOV_PATH` (= llvm-profdata) and `LLVM_COV` (= llvm-cov) are
# propagated from the dev-shell PATH into each test action. Bazel's
# `tools/test/collect_cc_coverage.sh` reads these to decide the LLVM
# coverage path (`llvm-profdata merge` + `llvm-cov export --format=lcov`)
# when toolchains_llvm produces `.profraw` profiles. Required since the
# Bazel 7.2+ env-clobber bug — fix landed in Bazel 9.1.0 (PR #25879,
# closes #23247), which is pinned via `.bazelversion`.
bazel-coverage *FLAGS='':
    COVERAGE_GCOV_PATH=$(command -v llvm-profdata) \
    LLVM_COV=$(command -v llvm-cov) \
        bazel coverage \
            --combined_report=lcov \
            --instrumentation_filter='//src' \
            --features=llvm_coverage_map_format \
            --features=-gcc_coverage_map_format \
            --test_env=COVERAGE_GCOV_PATH \
            --test_env=LLVM_COV \
            --stamp \
            //src/... //test/unit/... //test/approval/... //test/e2e-rust/... {{FLAGS}}

# Run every GoogleTest unit-test target — both the legacy
# `//test/unit/<x>/` directories AND any per-module ADR-0003 co-located
# `*_test.cpp` under `//src/<mod>/`. Useful for inner-loop development;
# CI runs unit tests via `bazel-coverage`.
bazel-test-unit:
    bazel test //src/... //test/unit/...

# Run the approval-test target. SOURCE_DATE_EPOCH=946684800 and
# SIPI_WORKSPACE_ROOT="." are injected by `test/approval/BUILD.bazel`;
# any `.received.<ext>` file from a maintainer-driven re-approval
# lands under `bazel-testlogs/test/approval/approvaltests/`. Useful for
# inner-loop development; CI runs approval tests via `bazel-coverage`.
bazel-test-approval:
    bazel test //test/approval:approvaltests

# Run all Rust e2e `rust_test` targets against the Bazel-built `:sipi`
# binary. Pass extra Bazel flags positionally: e.g. `--config=asan` for
# the sanitiser run, `--test_output=streamed` for live stdout,
# `--runs_per_test=3` for a flakiness gate on `iiif_compliance` /
# `resource_limits` / `latency`. Useful for inner-loop development; CI
# runs e2e tests via `bazel-coverage`.
#
# `--stamp` is on so `cli_version_flag` reads the stamped
# `STABLE_SIPI_VERSION` rather than the `0.0.0-unstamped` fallback. Only
# the `:sipi_version_h` action's cache key depends on `STABLE_*` values,
# so the stamp adds at most one re-link per workspace_status change.
bazel-test-e2e *FLAGS='':
    bazel test --stamp --verbose_failures {{FLAGS}} //test/e2e-rust:all_e2e

# Run the Docker smoke test against the Bazel-built OCI image. The image
# tarball is produced by `//src:image_load` and consumed via the test's
# runfiles (`SIPI_IMAGE_TAR` env), so no separate `bazel run
# //src:image_load` step is needed — Bazel materialises the tarball as
# a `data` dep of `:docker_smoke`.
#
# `--config=asan` does not apply: the test client is uninstrumented and
# the sipi binary inside the container is already covered by
# `bazel-test-e2e --config=asan` above.
bazel-test-smoke *FLAGS='':
    bazel test --stamp --verbose_failures {{FLAGS}} //test/e2e-rust:docker_smoke

# Sanitized build: Debug + ASan + UBSan via Bazel
# `--config=asan --config=ubsan`. The resulting binary at
# `bazel-bin/src/cli/sipi` is what `sanitizer.yml`'s e2e step consumes
# via `SIPI_BIN`. DWARF stays inline (`--strip=never` in
# `.bazelrc`) so the symbol-name suppressions in `.lsan_suppressions.txt`
# match. `--verbose_failures` surfaces the underlying cmake/make output
# from any failing `rules_foreign_cc` ext/* dep — without it, Bazel only
# reports the higher-level "output X was not created" line, which makes
# triage of CFLAGS-propagation interactions impossible. `--stamp` runs
# `tools/workspace_status.sh` so `STABLE_SIPI_VERSION` (from `version.txt`)
# is baked into `SipiVersion.h` via `src/BUILD.bazel`'s
# `expand_template(stamp_substitutions = {…})`. Without it, the binary
# reports `sipi 0.0.0-unstamped` and the `cli_version_flag` e2e test
# fails.
bazel-build-sanitized *FLAGS='':
    bazel build --config=asan --config=ubsan --verbose_failures --stamp {{FLAGS}} //src/cli:sipi
    @echo "Binary at: $(pwd)/bazel-bin/src/cli/sipi"

# Resolve the per-host fuzz `--platforms=` label. linux-x86_64 → libstdc++
# toolchain (`@llvm_toolchain_fuzz`) for libFuzzer ABI parity with the
# prior CMake build. darwin-aarch64 → default `@llvm_toolchain` (Apple SDK
# + libc++; libFuzzer's ABI on darwin). Other host tuples are rejected
# loudly — the libFuzzer harness is supported on linux-x86_64 (CI) and
# darwin-aarch64 (local dev) only. Used as a `recipe: dep` so both
# `bazel-build-fuzz` and `bazel-run-fuzz` share the same host detection.
_fuzz-platform:
    #!/usr/bin/env bash
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64)   echo "//tools/fuzz:linux_x86_64_fuzz" ;;
        Darwin-arm64)   echo "//tools/fuzz:darwin_aarch64_fuzz" ;;
        *)
            echo "fuzz harness: unsupported host $(uname -s)-$(uname -m)." >&2
            echo "  Supported: linux-x86_64 (CI) and darwin-aarch64 (local dev)." >&2
            echo "  linux-aarch64 is out of scope; use OrbStack/colima or workflow_dispatch." >&2
            exit 1
            ;;
    esac

# Fuzz harness build: produces the libFuzzer binary only. The
# `--platforms=` is resolved by `_fuzz-platform` from the host
# (`uname`) — linux-x86_64
# routes to the libstdc++ toolchain, darwin-aarch64 to the default
# libc++ toolchain. The `target_compatible_with = ["//tools/fuzz:fuzz_enabled"]`
# gate on the binary activates under both. CI's `fuzz.yml` workflow's
# `Build fuzz target` step invokes this recipe.
bazel-build-fuzz *FLAGS='':
    #!/usr/bin/env bash
    set -euo pipefail
    PLATFORM=$(just _fuzz-platform)
    bazel build --config=fuzz --platforms="$PLATFORM" --verbose_failures {{FLAGS}} //fuzz/handlers:iiif_handler_uri_parser_fuzz
    echo "Fuzzer at: $(pwd)/bazel-bin/fuzz/handlers/iiif_handler_uri_parser_fuzz"

# Run the libFuzzer harness against a live corpus (read+write) for a bounded
# time budget. Optional read-only seed corpus can be passed as the third arg.
# Argument triple matches today's deleted `nix-run-fuzz` recipe verbatim so
# `fuzz.yml`'s `Run fuzzer` step needs only the recipe-name swap.
#
# Exit 0 = time budget reached with no findings; non-zero = finding (crash,
# timeout, or oom).
#
# `-timeout=1`: any single input that takes >1 s to parse is reported as a
# `timeout-*` finding instead of being absorbed into the run budget.
# `-rss_limit_mb=1024`: any allocation past 1 GiB is reported as an `oom-*`
# finding instead of OOM-killing the runner. Both libFuzzer flags are
# emitted by the harness binary, not by Bazel — the `bazel run -- <args>`
# form forwards everything past `--` to the binary's argv directly.
#
# `bazel run` resolves the binary's `cwd` to `$BUILD_WORKSPACE_DIRECTORY`
# (the workspace root), so `{{corpus}}` and `{{seed}}` are interpreted
# relative to the same in-tree paths today's Nix-based recipe used.
# Crash/timeout/oom artifacts (`crash-*`, `timeout-*`, `oom-*`) land in
# the workspace root; `fuzz.yml`'s `Collect crash details` glob keeps
# working unchanged.
bazel-run-fuzz corpus duration seed="":
    #!/usr/bin/env bash
    set -euo pipefail
    PLATFORM=$(just _fuzz-platform)

    # Build first (cached after the first invocation).
    bazel build --config=fuzz --platforms="$PLATFORM" --verbose_failures //fuzz/handlers:iiif_handler_uri_parser_fuzz

    args=("{{corpus}}")
    [ -n "{{seed}}" ] && args+=("{{seed}}")

    # Apple's ASan runtime (`libclang_rt.asan_osx_dynamic.dylib`) is always
    # dynamically linked, and toolchains_llvm's binary references it via
    # `@rpath`. Under `bazel run`, cwd is the workspace root, so the
    # binary's `external/toolchains_llvm…` relative rpath does not
    # resolve. Setting DYLD_LIBRARY_PATH and exec'ing the binary directly
    # avoids that and also dodges macOS SIP, which strips DYLD_* across
    # `bazel run`'s subprocess chain. cwd is the workspace root either
    # way so corpus paths and crash-file globs keep working unchanged.
    if [ "$(uname -s)" = "Darwin" ]; then
        EXECROOT=$(bazel info execution_root)
        export DYLD_LIBRARY_PATH="$EXECROOT/external/toolchains_llvm++llvm+llvm_toolchain_llvm/lib/clang/19/lib/darwin${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
    fi

    exec ./bazel-bin/fuzz/handlers/iiif_handler_uri_parser_fuzz \
        "${args[@]}" \
        -max_total_time={{duration}} -timeout=1 -rss_limit_mb=1024 -print_final_stats=1

#####################################
# Microbenchmarks (Google Benchmark)
#
# Local dev-loop tooling only — never a CI gate. The discipline:
# *no benchmark, no hot-path change* — before/after numbers from the SAME
# machine and the SAME `-c opt` configuration accompany every codec/parser
# hot-path PR. See docs/src/development/benchmarking.md for the full
# workflow and the regression decision rule.
#####################################

# Build (`-c opt`, matching production codegen — never fastbuild, never
# sanitized/instrumented) and exec the named microbenchmark binary
# directly, forwarding Google Benchmark flags. `name` is the tier:
# parse | decode | encode | process → `//src:<name>_benchmark`.
#
# Typical before/after loop:
#   just bench parse --benchmark_repetitions=20 \
#       --benchmark_out=before.json --benchmark_out_format=json
#   ... make the hot-path change ...
#   just bench parse --benchmark_repetitions=20 \
#       --benchmark_out=after.json --benchmark_out_format=json
#   just bench-compare before.json after.json
#
# SOURCE_DATE_EPOCH + SIPI_WORKSPACE_ROOT mirror what
# `test/approval/BUILD.bazel` injects for tests: the encode/process tiers
# touch the ICC machinery whose creation date is wall-clock-stamped
# (ADR-0002), and `//test:test_paths` resolves fixtures relative to the
# workspace root. They are exported here (not via the cc_binary `env`
# attr) because the recipe runs the binary directly — `env` only applies
# under `bazel run`/`bazel test`. The decode/encode tiers additionally
# read the @sipi_bench_fixtures external repo (fetched lazily on first
# build) out of the binary's runfiles tree via SIPI_BENCH_FIXTURES_DIR,
# and the encode tier writes its outputs to a throwaway TEST_TMPDIR.
bench name *FLAGS='':
    #!/usr/bin/env bash
    set -euo pipefail
    bazel build -c opt //src:{{name}}_benchmark
    shopt -s nullglob
    fixtures=(./bazel-bin/src/{{name}}_benchmark.runfiles/+*sipi_bench_fixtures)
    env_args=()
    [ ${#fixtures[@]} -gt 0 ] && env_args+=("SIPI_BENCH_FIXTURES_DIR=${fixtures[0]}")
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    env SOURCE_DATE_EPOCH=946684800 SIPI_WORKSPACE_ROOT="." TEST_TMPDIR="$TMP" \
        "${env_args[@]}" \
        ./bazel-bin/src/{{name}}_benchmark {{FLAGS}}

# Compare two `just bench` JSON outputs: per-benchmark Mann-Whitney
# U-test deltas + OVERALL_GEOMEAN via the vendored google_benchmark
# compare.py (`//tools/benchmark:compare`, hermetic python + numpy/scipy).
# Trust a delta only if it is green (p < 0.05) AND the median shift
# exceeds the baseline CV; sub-3% deltas are noise. Extra FLAGS (e.g.
# `--alpha=0.01`, `--no-utest`) are global compare.py options and must
# precede the subcommand. Paths are resolved to absolute because
# `bazel run` does not execute in the invocation directory.
bench-compare before after *FLAGS='':
    #!/usr/bin/env bash
    set -euo pipefail
    BEFORE="$(cd "$(dirname "{{before}}")" && pwd)/$(basename "{{before}}")"
    AFTER="$(cd "$(dirname "{{after}}")" && pwd)/$(basename "{{after}}")"
    bazel run //tools/benchmark:compare -- {{FLAGS}} benchmarks "$BEFORE" "$AFTER"

#####################################
# Bazel Docker (rules_oci)
#
# Each per-arch CI runner builds + loads the matching architecture's
# image into its local Docker daemon (`bazel-docker-build-${arch}`) and
# pushes per-arch digests to the registry (`bazel-docker-push-${arch}`).
# A coordinator job runs `bazel-docker-publish-manifest` to stitch the
# two pushed digests into a multi-arch manifest at
# `daschswiss/sipi:v<version>` via `crane index append`.
#
# `oci_image_index` is intentionally not used — its `images=[…]` attr
# accepts only same-build-graph labels, not external digests pushed by
# other runners (see dasch-specs research-findings §1).
#####################################

# Build the per-arch image and load it into the local Docker daemon as
# `daschswiss/sipi:latest`. Smoke-test consumes `latest`; CI re-tags
# with `:v<version>-${arch}` via `bazel-docker-push-${arch}` below.
# `--stamp` runs `tools/workspace_status.sh` so STABLE_SIPI_VERSION
# (from version.txt) is baked into image labels and tag suffixes;
# without it, tags resolve to `0.0.0-unstamped-${arch}` and `publish.yml`'s
# version-based downstream steps (Sentry release naming, Scout
# environment recording) silently break.
bazel-docker-build-amd64 *FLAGS='':
    bazel run --stamp --platforms=//bazel/platforms:linux_amd64 --verbose_failures {{FLAGS}} //src:image_load

bazel-docker-build-arm64 *FLAGS='':
    bazel run --stamp --platforms=//bazel/platforms:linux_arm64 --verbose_failures {{FLAGS}} //src:image_load

# Push the per-arch image to docker.io/daschswiss/sipi with two tags:
# `latest-${arch}` and `v<version>-${arch}`. Driven by `oci_push`'s
# `remote_tags` attribute consuming `:remote_tags_${arch}`. The
# coordinator job (`bazel-docker-publish-manifest` below) reads these
# digests off the registry to assemble the multi-arch manifest.
bazel-docker-push-amd64 *FLAGS='':
    bazel run --stamp --platforms=//bazel/platforms:linux_amd64 --verbose_failures {{FLAGS}} //src:image_push_amd64

bazel-docker-push-arm64 *FLAGS='':
    bazel run --stamp --platforms=//bazel/platforms:linux_arm64 --verbose_failures {{FLAGS}} //src:image_push_arm64

# Assemble the multi-arch manifest at `daschswiss/sipi:v<version>` from
# the two per-arch digests already pushed by `bazel-docker-push-*`.
# Also applies the `latest` tag to the manifest so downstream consumers
# can pull `daschswiss/sipi:latest` and get the right arch automatically.
#
# Pre-conditions (held by publish.yml's job graph):
#   - `daschswiss/sipi:v<version>-amd64` and `-arm64` exist in the
#     registry (pushed by the per-arch publish-docker matrix jobs)
#   - `crane` on PATH (declared in flake.nix devShells)
#   - Docker Hub credentials available (handled by docker/login-action
#     in publish.yml; `crane` reads ~/.docker/config.json)
bazel-docker-publish-manifest:
    #!/usr/bin/env bash
    set -euo pipefail
    VERSION="$(tr -d '[:space:]' < version.txt)"
    BASE="docker.io/daschswiss/sipi"
    echo "==> Assembling ${BASE}:v${VERSION} from per-arch digests"
    crane index append \
        -m "${BASE}:v${VERSION}-amd64" \
        -m "${BASE}:v${VERSION}-arm64" \
        -t "${BASE}:v${VERSION}"
    echo "==> Tagging manifest as :latest"
    crane tag "${BASE}:v${VERSION}" latest
    crane manifest "${BASE}:v${VERSION}" >/dev/null
    echo "Multi-arch manifest published: ${BASE}:v${VERSION}"

# Build `:sipi_debug_layout` and surface the .debug file at
# `sipi-${arch}.debug` for `sentry-cli debug-files upload` consumption.
# Filename matches today's `nix-docker-extract-debug` recipe so
# publish.yml's existing Sentry-upload step is untouched (only the
# upstream changes from Nix to Bazel; the contract — `sipi-${arch}.debug`
# at the workspace root — is preserved verbatim).
#
# `arch` is `amd64` or `arm64`; the recipe selects the Bazel platform
# accordingly. CI's per-arch runners pass their matching value.
bazel-docker-extract-debug arch *FLAGS='':
    #!/usr/bin/env bash
    set -euo pipefail
    case "{{arch}}" in
        amd64) PLATFORM=//bazel/platforms:linux_amd64 ;;
        arm64) PLATFORM=//bazel/platforms:linux_arm64 ;;
        *)
            echo "ERROR: unknown arch '{{arch}}' (expected: amd64, arm64)" >&2
            exit 1
            ;;
    esac

    # Build the laid-out debug-tree tarball and extract its single
    # `.debug` file. The tarball layout is `lib/debug/.build-id/<xx>/<yy>.debug`;
    # the filename is the build-id with the leading two hex chars removed
    # and `.debug` appended.
    bazel build --stamp --platforms="$PLATFORM" --verbose_failures {{FLAGS}} //src:sipi_debug_layout

    # Find the single `.debug` file inside the tar. The tarball is
    # deterministic by construction, so this glob always resolves
    # to exactly one entry.
    workdir=$(mktemp -d)
    trap 'rm -rf "$workdir"' EXIT
    tar -xf bazel-bin/src/sipi_debug_layout.tar -C "$workdir"
    debug_file=$(find "$workdir/lib/debug/.build-id" -name '*.debug' | head -1)
    if [ -z "$debug_file" ]; then
        echo "ERROR: no .debug file found inside sipi_debug_layout.tar" >&2
        exit 1
    fi
    cp "$debug_file" "sipi-{{arch}}.debug"
    echo "Debug symbols copied to: sipi-{{arch}}.debug"

# Run sipi with the localdev config.
run: bazel-build
    #!/usr/bin/env bash
    set -euo pipefail
    SIPI_BIN="${SIPI_BIN:-{{justfile_directory()}}/bazel-bin/src/cli/sipi}"
    "$SIPI_BIN" server --config={{justfile_directory()}}/config/sipi.localdev-config.lua

# Run sipi under Valgrind.
valgrind: bazel-build
    #!/usr/bin/env bash
    set -euo pipefail
    SIPI_BIN="${SIPI_BIN:-{{justfile_directory()}}/bazel-bin/src/cli/sipi}"
    valgrind --leak-check=yes --track-origins=yes "$SIPI_BIN" server --config={{justfile_directory()}}/config/sipi.config.lua

# Download CI fuzz corpus and merge into seed corpus
fuzz-corpus-update:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "Downloading latest fuzz corpus from CI..."
    rm -rf {{justfile_directory()}}/.fuzz-corpus-ci
    gh run download --name fuzz-corpus --dir {{justfile_directory()}}/.fuzz-corpus-ci || \
        { echo "No fuzz-corpus artifact found. Has the fuzz workflow run yet?"; exit 1; }
    before=$(ls fuzz/handlers/corpus/ | wc -l | tr -d ' ')
    for f in {{justfile_directory()}}/.fuzz-corpus-ci/*; do
        hash=$(shasum -a 256 "$f" | cut -d' ' -f1 | head -c 16)
        cp "$f" "fuzz/handlers/corpus/$hash"
    done
    after=$(ls fuzz/handlers/corpus/ | wc -l | tr -d ' ')
    echo "Corpus: $before → $after inputs ($((after - before)) new)"
    rm -rf {{justfile_directory()}}/.fuzz-corpus-ci

#####################################
# Architecture boundaries
#####################################

# Enforce shttps → sipi one-way dependency boundary.
shttps-context-check:
    @scripts/shttps-context-check.sh

#####################################
# Documentation
#####################################

# Build docs into the local 'site' folder
docs-build:
    cd docs && mkdocs build

# Serve docs for local viewing
docs-serve:
    cd docs && mkdocs serve

# Build and publish docs to Github Pages
docs-publish:
    cd docs && mkdocs gh-deploy

# Install requirements for documentation
docs-install-requirements:
    pip3 install -r docs/requirements.txt

#####################################
# Infra (OpenTofu) — out-of-band, NOT part of the Bazel build graph
#
# Manages the `bazel-cache-proxy` Cloud Run service. Needs `gcloud auth`
# and the dev shell (`opentofu` is in flake.nix). The TF state bucket is
# bootstrapped separately (see infra/bootstrap). Details and the one-time
# import sequence: docs/src/development/ci.md.
#####################################

# Show planned changes to the bazel-cache-proxy deployment.
tf-plan *FLAGS='':
    tofu -chdir=infra/bazel-cache plan {{FLAGS}}

# Apply the bazel-cache-proxy deployment (rolls a new Cloud Run revision).
tf-apply *FLAGS='':
    tofu -chdir=infra/bazel-cache apply {{FLAGS}}

#####################################
# Utilities
#####################################

# Clean local build artifacts (Bazel symlinks + docs site + IDE-emitted dirs).
# Bazel's own cache is at `bazel info output_base`; use `bazel clean
# --expunge` to drop it.
clean:
    rm -rf bazel-bin bazel-out bazel-testlogs bazel-* || true
    rm -rf site/
