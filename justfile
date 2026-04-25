# Sipi — IIIF-compatible media server
# Run `just` to list all available recipes.
#
# Build reproducibility invariant: every build-related recipe goes through
# `nix build .#<variant>`. CI invokes only `just <recipe>`. Incremental
# inner-loop development (seconds-fast rebuilds while editing one .cpp file)
# is deliberately NOT a recipe — see CLAUDE.md / docs/src/development/developing.md
# for the documented `nix develop` + `cmake --build build` workflow.

# Auto-detect CPU cores
nproc := `nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4`

# Docker repo/tag (from vars.mk)
docker_repo := "daschswiss/sipi"
build_tag := `git describe --tag --dirty --abbrev=7 2>/dev/null || git rev-parse --verify HEAD 2>/dev/null || cat version.txt`
docker_image := docker_repo + ":" + build_tag

# GHA cache flags (auto-detected from environment)
_gha_cache_from := if env("ACTIONS_CACHE_URL", "") != "" { "--cache-from type=gha,url=$ACTIONS_CACHE_URL,token=$ACTIONS_RUNTIME_TOKEN" } else { "" }
_gha_cache_to := if env("ACTIONS_CACHE_URL", "") != "" { "--cache-to type=gha,mode=max,url=$ACTIONS_CACHE_URL,token=$ACTIONS_RUNTIME_TOKEN" } else { "" }

# List all recipes
default:
    @just --list

#####################################
# Docker
#####################################

# Build and load Sipi Docker image locally
docker-build:
    docker buildx build \
        --progress auto \
        --build-arg VERSION={{build_tag}} \
        {{_gha_cache_from}} \
        {{_gha_cache_to}} \
        -t {{docker_image}} -t {{docker_repo}}:latest \
        --load \
        . \
    || ( echo "Build failed, retrying without GHA cache..." && \
    docker buildx build \
        --progress auto \
        --build-arg VERSION={{build_tag}} \
        -t {{docker_image}} -t {{docker_repo}}:latest \
        --load \
        . )

# Build + test arm64 Docker image, extract debug symbols
docker-test-build-arm64:
    docker buildx build \
        --progress auto \
        --platform linux/arm64 \
        --build-arg VERSION={{build_tag}} \
        {{_gha_cache_from}} \
        {{_gha_cache_to}} \
        -t {{docker_image}}-arm64 -t {{docker_repo}}:latest \
        --load \
        . \
    || ( echo "Build failed, retrying without GHA cache..." && \
    docker buildx build \
        --progress auto \
        --platform linux/arm64 \
        --build-arg VERSION={{build_tag}} \
        -t {{docker_image}}-arm64 -t {{docker_repo}}:latest \
        --load \
        . )
    docker buildx build \
        --platform linux/arm64 \
        --build-arg VERSION={{build_tag}} \
        {{_gha_cache_from}} \
        {{_gha_cache_to}} \
        --target debug-symbols \
        --output type=local,dest=./debug-out \
        . \
    || ( echo "Debug symbols build failed, retrying without GHA cache..." && \
    docker buildx build \
        --platform linux/arm64 \
        --build-arg VERSION={{build_tag}} \
        --target debug-symbols \
        --output type=local,dest=./debug-out \
        . )
    mv ./debug-out/sipi.debug ./sipi-arm64.debug && rm -rf ./debug-out

# Push previously built arm64 image to Docker Hub
docker-push-arm64:
    docker push {{docker_image}}-arm64

# Build + test amd64 Docker image, extract debug symbols
docker-test-build-amd64:
    docker buildx build \
        --progress auto \
        --platform linux/amd64 \
        --build-arg VERSION={{build_tag}} \
        {{_gha_cache_from}} \
        {{_gha_cache_to}} \
        -t {{docker_image}}-amd64 -t {{docker_repo}}:latest \
        --load \
        . \
    || ( echo "Build failed, retrying without GHA cache..." && \
    docker buildx build \
        --progress auto \
        --platform linux/amd64 \
        --build-arg VERSION={{build_tag}} \
        -t {{docker_image}}-amd64 -t {{docker_repo}}:latest \
        --load \
        . )
    docker buildx build \
        --platform linux/amd64 \
        --build-arg VERSION={{build_tag}} \
        {{_gha_cache_from}} \
        {{_gha_cache_to}} \
        --target debug-symbols \
        --output type=local,dest=./debug-out \
        . \
    || ( echo "Debug symbols build failed, retrying without GHA cache..." && \
    docker buildx build \
        --platform linux/amd64 \
        --build-arg VERSION={{build_tag}} \
        --target debug-symbols \
        --output type=local,dest=./debug-out \
        . )
    mv ./debug-out/sipi.debug ./sipi-amd64.debug && rm -rf ./debug-out

# Push previously built amd64 image to Docker Hub
docker-push-amd64:
    docker push {{docker_image}}-amd64

# Publish Docker manifest combining arm64 and amd64 images
docker-publish-manifest:
    docker manifest create {{docker_image}} --amend {{docker_image}}-amd64 --amend {{docker_image}}-arm64
    docker manifest annotate --arch amd64 --os linux {{docker_image}} {{docker_image}}-amd64
    docker manifest annotate --arch arm64 --os linux {{docker_image}} {{docker_image}}-arm64
    docker manifest inspect {{docker_image}}
    docker manifest push {{docker_image}}

#####################################
# Smoke tests (run against Docker image)
#####################################

# Build Docker image and run smoke tests
test-smoke: docker-build
    cd test/e2e-rust && cargo test --features docker --test docker_smoke -- --test-threads=1

# Run smoke tests against already-built Docker image
test-smoke-ci:
    cd test/e2e-rust && cargo test --features docker --test docker_smoke -- --test-threads=1

#####################################
# Nix builds (reproducible — what CI runs)
#
# Every recipe here wraps `nix build .#<variant>`. No imperative cmake.
# For the fast inner-loop edit/rebuild cycle, drop into `nix develop` and
# call cmake by hand — see CLAUDE.md "Inner-loop development".
#
# Every recipe passes `--extra-experimental-features configurable-impure-env
# --option impure-env "GH_TOKEN=$GH_TOKEN"` so the Kakadu FOD can pull the
# archive on a cold Cachix cache. Harmless when GH_TOKEN is empty (the FOD
# falls through to cache substitution or errors with a helpful message).
#####################################

_nix_build := "nix build --extra-experimental-features configurable-impure-env --option impure-env GH_TOKEN=${GH_TOKEN:-}"

# Dev build: Debug + coverage instrumentation, tests run in the Nix sandbox.
# Canonical CI build — what `test.yml` invokes on every PR.
nix-build:
    {{_nix_build}} .#dev
    @echo "Binary at: $(readlink result)/bin/sipi"

# Default build: RelWithDebInfo + tests, unstripped debug info in $debug.
nix-build-default:
    {{_nix_build}} .#default
    @echo "Binary at: $(readlink result)/bin/sipi"

# Release build: Release + tests disabled + dontStrip (for manual distribution).
nix-build-release:
    {{_nix_build}} .#release
    @echo "Binary at: $(readlink result)/bin/sipi"

# Sanitized build: Debug + ASan + UBSan, tests run in the Nix sandbox.
# `filter-syscalls false` is required because LSan uses ptrace, which
# the default Nix sandbox blocks via seccomp. No-op on macOS.
nix-build-sanitized:
    {{_nix_build}} .#sanitized --option filter-syscalls false
    @echo "Binary at: $(readlink result)/bin/sipi"

# Fuzz harness build: produces the libFuzzer binary only (no sipi runtime files).
nix-build-fuzz:
    {{_nix_build}} .#fuzz
    @echo "Fuzzer at: $(readlink result)/bin/iiif_handler_uri_parser_fuzz"

# Static amd64 binary via Zig-in-Nix (Linux only).
nix-build-static-amd64:
    {{_nix_build}} .#static-amd64

# Static arm64 binary via Zig-in-Nix (Linux only).
nix-build-static-arm64:
    {{_nix_build}} .#static-arm64

# Release archive amd64 (tarball + sha256 + debug symbols).
nix-build-release-archive-amd64:
    {{_nix_build}} .#release-archive-amd64

# Release archive arm64 (tarball + sha256 + debug symbols).
nix-build-release-archive-arm64:
    {{_nix_build}} .#release-archive-arm64

# Docker image via Nix dockerTools (streamed into Docker daemon).
nix-docker-build:
    $({{_nix_build}} .#docker-stream --print-out-paths) | docker load

# Coverage XML for Codecov (at result-coverage/coverage.xml).
nix-coverage:
    {{_nix_build}} .#dev^coverage
    @echo "Coverage at: result-coverage/coverage.xml"

# Verify a Linux static sipi binary has no NEEDED dynamic library entries.
nix-static-linkage-verify path:
    #!/usr/bin/env bash
    set -euo pipefail
    file "{{path}}"
    if readelf -d "{{path}}" | grep -q '(NEEDED)'; then
        echo "ERROR: {{path}} has unexpected dynamic dependencies:" >&2
        readelf -d "{{path}}" | grep 'NEEDED' >&2
        exit 1
    fi
    echo "OK: {{path}} is statically linked (no NEEDED entries)."

# Audit macOS sipi runtime dylibs — fail if any point at /opt/homebrew/ or /usr/local/.
nix-macos-dylib-audit path:
    #!/usr/bin/env bash
    set -euo pipefail
    otool -L "{{path}}"
    if otool -L "{{path}}" | awk 'NR>1 {print $1}' | grep -E '^(/opt/homebrew/|/usr/local/)'; then
        echo "ERROR: {{path}} references non-system dylibs (must be portable)." >&2
        exit 1
    fi
    echo "OK: {{path}} uses only system dylibs."

#####################################
# Tests (consume $SIPI_BIN, default ./result/bin/sipi)
#####################################

# Run Rust e2e tests against the built sipi.
rust-test-e2e:
    #!/usr/bin/env bash
    set -euo pipefail
    SIPI_BIN="${SIPI_BIN:-{{justfile_directory()}}/result/bin/sipi}"
    if [ ! -x "$SIPI_BIN" ]; then
        echo "sipi binary not found at $SIPI_BIN. Run 'just nix-build' first." >&2
        exit 1
    fi
    cd test/e2e-rust && SIPI_BIN="$SIPI_BIN" cargo test -- --test-threads=1

# Run Hurl HTTP contract tests against the built sipi.
hurl-test:
    #!/usr/bin/env bash
    set -euo pipefail
    SIPI_BIN="${SIPI_BIN:-{{justfile_directory()}}/result/bin/sipi}"
    if [ ! -x "$SIPI_BIN" ]; then
        echo "sipi binary not found at $SIPI_BIN. Run 'just nix-build' first." >&2
        exit 1
    fi
    cd test/_test_data && "$SIPI_BIN" --config config/sipi.e2e-test-config.lua &
    SIPI_PID=$!
    # Don't propagate sipi's SIGTERM exit code (143) as the recipe exit code.
    trap 'kill $SIPI_PID 2>/dev/null || true; wait $SIPI_PID 2>/dev/null || true' EXIT
    READY=0
    for i in $(seq 1 30); do
        curl -sf http://127.0.0.1:1024/unit/lena512.jp2/info.json > /dev/null 2>&1 && READY=1 && break
        sleep 0.5
    done
    if [ "$READY" -ne 1 ]; then echo "ERROR: sipi failed to start within 15s"; exit 1; fi
    cd {{justfile_directory()}}/test/hurl && hurl --test --insecure --variable host=http://127.0.0.1:1024 *.hurl

# Run sipi with the localdev config.
nix-run: nix-build # dep: keep result/bin/sipi fresh across variant switches
    #!/usr/bin/env bash
    set -euo pipefail
    SIPI_BIN="${SIPI_BIN:-{{justfile_directory()}}/result/bin/sipi}"
    "$SIPI_BIN" --config={{justfile_directory()}}/config/sipi.localdev-config.lua

# Run sipi under Valgrind.
nix-valgrind: nix-build
    #!/usr/bin/env bash
    set -euo pipefail
    SIPI_BIN="${SIPI_BIN:-{{justfile_directory()}}/result/bin/sipi}"
    valgrind --leak-check=yes --track-origins=yes "$SIPI_BIN" --config={{justfile_directory()}}/config/sipi.config.lua

# Run the libFuzzer harness against a live corpus (read+write) for a bounded
# time budget. Optional read-only seed corpus can be passed as the third arg.
# Exit 0 = time budget reached with no findings; non-zero = finding (crash,
# timeout, or oom).
#
# `-timeout=1`: any single input that takes >1 s to parse is reported as a
# `timeout-*` finding instead of being absorbed into the run budget.
# `-rss_limit_mb=1024`: any allocation past 1 GiB is reported as an `oom-*`
# finding instead of OOM-killing the runner.
nix-run-fuzz corpus duration seed="":
    #!/usr/bin/env bash
    set -euo pipefail
    FUZZ_BIN="${FUZZ_BIN:-{{justfile_directory()}}/result/bin/iiif_handler_uri_parser_fuzz}"
    if [ ! -x "$FUZZ_BIN" ]; then
        echo "fuzzer binary not found at $FUZZ_BIN. Run 'just nix-build-fuzz' first." >&2
        exit 1
    fi
    if [ -n "{{seed}}" ]; then
        exec "$FUZZ_BIN" "{{corpus}}" "{{seed}}" \
            -max_total_time={{duration}} -timeout=1 -rss_limit_mb=1024 -print_final_stats=1
    else
        exec "$FUZZ_BIN" "{{corpus}}" \
            -max_total_time={{duration}} -timeout=1 -rss_limit_mb=1024 -print_final_stats=1
    fi

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
# Vendor dependencies
#####################################

# Download all dependency archives to vendor/
vendor-download:
    @scripts/vendor.sh download

# Verify SHA-256 checksums of vendored archives
vendor-verify:
    @scripts/vendor.sh verify

# Print SHA-256 checksums for all vendor archives
vendor-checksums:
    @scripts/vendor.sh checksums

# Fetch the proprietary Kakadu archive from dsp-ci-assets release into vendor/.
# Only required for the production Dockerfile and local Docker dev builds.
# Nix builds fetch Kakadu directly via flake.nix's FOD (no vendor/ step).
# Requires: gh CLI authenticated (org membership = access).
# In CI: set GH_TOKEN to a PAT with read access to dsp-ci-assets (DASCHBOT_PAT).
kakadu-fetch:
    @./scripts/fetch-kakadu.sh

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
# Utilities
#####################################

# Clean build artifacts (cmake trees from the dev-shell inner loop + Nix result symlinks)
clean:
    rm -rf build/
    rm -rf build-sanitized/
    rm -rf build-fuzz/
    rm -rf cmake-build-relwithdebinfo-inside-docker/
    rm -rf site/
    rm -f result result-* || true
