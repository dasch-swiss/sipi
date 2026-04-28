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

# Map host arch (any OS) to the matching Linux arch for `nix-docker-build`.
# `dockerTools.streamLayeredImage` is exposed only under
# `packages.{x86_64,aarch64}-linux.*` (gated by `pkgs.lib.optionalAttrs
# isLinux` in flake.nix). On macOS, native-linux-builder dispatches the
# build to a Linux builder transparently. arch() returns "aarch64" /
# "x86_64" regardless of OS, so the same mapping works on darwin and
# Linux hosts.
_linux_arch := if arch() == "aarch64" { "aarch64-linux" } else { "x86_64-linux" }

# List all recipes
default:
    @just --list

#####################################
# Smoke tests (run against Docker image)
#####################################

# Build Docker image (via Nix) and run smoke tests against it.
test-smoke: nix-docker-build
    cd test/e2e-rust && cargo test --features docker --test docker_smoke -- --test-threads=1

# Run smoke tests against an already-loaded Docker image.
test-smoke-ci:
    cd test/e2e-rust && cargo test --features docker --test docker_smoke -- --test-threads=1

#####################################
# Docker (push / manifest only — image *building* is in the Nix section)
#
# Build invariant: every image-build path goes through `nix build`.
# The recipes below do NOT build images; they only consume images that
# `nix-docker-build*` already loaded into the local Docker daemon.
#####################################

# Push the already-loaded amd64 Docker image to Docker Hub.
docker-push-amd64:
    docker push {{docker_image}}-amd64

# Push the already-loaded arm64 Docker image to Docker Hub.
docker-push-arm64:
    docker push {{docker_image}}-arm64

# Publish multi-arch Docker manifest combining amd64 and arm64 images.
docker-publish-manifest:
    docker manifest create {{docker_image}} --amend {{docker_image}}-amd64 --amend {{docker_image}}-arm64
    docker manifest annotate --arch amd64 --os linux {{docker_image}} {{docker_image}}-amd64
    docker manifest annotate --arch arm64 --os linux {{docker_image}} {{docker_image}}-arm64
    docker manifest inspect {{docker_image}}
    docker manifest push {{docker_image}}

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
_nix_eval  := "nix eval --extra-experimental-features configurable-impure-env --option impure-env GH_TOKEN=${GH_TOKEN:-} --raw"

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

# Uses `.#docker` (buildLayeredImage → static tarball) rather than
# `.#docker-stream` (streamLayeredImage → Linux Python wrapper script).
# The CI per-arch recipes use the streaming variant for speed (no temp
# tarball on disk on the Linux runner), but a streaming script can only
# execute on the OS / arch it was built for: a Linux Python interpreter
# embedded in the shebang won't run on a macOS host. The static tarball
# is portable — Docker Desktop on macOS loads it transparently into its
# Linux VM.
# Build host-arch Docker image via Nix dockerTools and load into the local Docker daemon.
nix-docker-build:
    docker load < $({{_nix_build}} .#packages.{{_linux_arch}}.docker --print-out-paths)
    # Re-tag with :latest and the build_tag for downstream consumers
    # (smoke tests, publish flows). The image's own tag (from
    # `dockerTools`) reflects version.txt; the tags below mirror the
    # Dockerfile-era contract.
    docker tag $({{_nix_eval}} .#packages.{{_linux_arch}}.docker.imageName):$({{_nix_eval}} .#packages.{{_linux_arch}}.docker.imageTag) {{docker_image}}
    docker tag $({{_nix_eval}} .#packages.{{_linux_arch}}.docker.imageName):$({{_nix_eval}} .#packages.{{_linux_arch}}.docker.imageTag) {{docker_repo}}:latest

# Pins the x86_64-linux flake attribute so this recipe fails fast on an
# arm64 host instead of silently producing an arm64 image — the CI matrix
# and publish.yml rely on the image arch matching the `-amd64` tag suffix.
#
# Two separate `nix build` calls — `nix build`'s `-o` flag is single-
# value (last wins), so a single invocation can't produce two named
# symlinks. The second call is essentially free: it shares the entire
# sipi closure (already realized by the docker-stream build) and only
# needs to materialize the `debug` output's symlink.
# Build amd64 Docker image + sipi .debug symlink, load into local daemon.
nix-docker-build-amd64:
    {{_nix_build}} -o result       .#packages.x86_64-linux.docker-stream
    {{_nix_build}} -o result-debug .#packages.x86_64-linux.sipi-debug
    ./result | docker load
    docker tag $({{_nix_eval}} .#packages.x86_64-linux.docker-stream.imageName):$({{_nix_eval}} .#packages.x86_64-linux.docker-stream.imageTag) {{docker_image}}-amd64
    docker tag $({{_nix_eval}} .#packages.x86_64-linux.docker-stream.imageName):$({{_nix_eval}} .#packages.x86_64-linux.docker-stream.imageTag) {{docker_repo}}:latest

# Pins the aarch64-linux flake attribute — same reasoning as `nix-docker-build-amd64`.
# Build arm64 Docker image + sipi .debug symlink, load into local daemon.
nix-docker-build-arm64:
    {{_nix_build}} -o result       .#packages.aarch64-linux.docker-stream
    {{_nix_build}} -o result-debug .#packages.aarch64-linux.sipi-debug
    ./result | docker load
    docker tag $({{_nix_eval}} .#packages.aarch64-linux.docker-stream.imageName):$({{_nix_eval}} .#packages.aarch64-linux.docker-stream.imageTag) {{docker_image}}-arm64
    docker tag $({{_nix_eval}} .#packages.aarch64-linux.docker-stream.imageName):$({{_nix_eval}} .#packages.aarch64-linux.docker-stream.imageTag) {{docker_repo}}:latest

# Does NO `nix build` — consumes the symlink produced by `nix-docker-build-<arch>`.
# `arch` is `amd64` or `arm64` and is purely a filename suffix here (the
# arch-specific derivation was already selected at build time, preventing
# a cross-arch mix-up). Used by publish.yml after `nix-docker-build-<arch>`.
# Rename result-debug/.../*.debug to sipi-<arch>.debug for Sentry upload.
nix-docker-extract-debug arch:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -L result-debug ] && [ ! -d result-debug ]; then
        echo "ERROR: result-debug/ not found. Run 'just nix-docker-build-{{arch}}' first." >&2
        exit 1
    fi
    debug_file=$(find result-debug/lib/debug/.build-id -name '*.debug' | head -1)
    if [ -z "$debug_file" ]; then
        echo "ERROR: no .debug file found under result-debug/lib/debug/.build-id" >&2
        exit 1
    fi
    cp "$debug_file" "sipi-{{arch}}.debug"
    echo "Debug symbols copied to: sipi-{{arch}}.debug"

# Coverage XML for Codecov (at result-coverage/coverage.xml).
nix-coverage:
    {{_nix_build}} .#dev^coverage
    @echo "Coverage at: result-coverage/coverage.xml"

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
# Inner-loop dev path — uses cargo from the dev shell. CI uses
# `nix-test-e2e` (below), which consumes pre-built test binaries from
# the .#e2e-tests derivation and needs no cargo on PATH.
rust-test-e2e:
    #!/usr/bin/env bash
    set -euo pipefail
    SIPI_BIN="${SIPI_BIN:-{{justfile_directory()}}/result/bin/sipi}"
    if [ ! -x "$SIPI_BIN" ]; then
        echo "sipi binary not found at $SIPI_BIN. Run 'just nix-build' first." >&2
        exit 1
    fi
    cd test/e2e-rust && SIPI_BIN="$SIPI_BIN" cargo test -- --test-threads=1

# Run nix-built Rust e2e test binaries against the built sipi.
# Equivalent coverage to `rust-test-e2e`, but the test binaries come
# from the .#e2e-tests Nix derivation (Cachix-cacheable, no cargo needed).
# `INSTA_WORKSPACE_ROOT` overrides insta's default `cargo metadata`-based
# snapshot lookup, which fails when the binary was compiled in a Nix
# sandbox whose source path no longer exists at runtime.
nix-test-e2e:
    #!/usr/bin/env bash
    set -euo pipefail
    SIPI_BIN="${SIPI_BIN:-{{justfile_directory()}}/result/bin/sipi}"
    if [ ! -x "$SIPI_BIN" ]; then
        echo "sipi binary not found at $SIPI_BIN. Run 'just nix-build' first." >&2
        exit 1
    fi
    {{_nix_build}} -o result-e2e .#e2e-tests
    SIPI_BIN="$SIPI_BIN" \
    SIPI_REPO_ROOT="{{justfile_directory()}}" \
    INSTA_WORKSPACE_ROOT="{{justfile_directory()}}/test/e2e-rust" \
        {{justfile_directory()}}/result-e2e/bin/run-e2e.sh

# Run nix-built Docker smoke test against the loaded daschswiss/sipi:latest image.
# Replaces `test-smoke-ci` for CI use; that recipe stays for the inner-loop dev path.
nix-test-smoke:
    #!/usr/bin/env bash
    set -euo pipefail
    if ! docker image inspect daschswiss/sipi:latest >/dev/null 2>&1; then
        echo "ERROR: daschswiss/sipi:latest not loaded. Run 'just nix-docker-build-<arch>' first." >&2
        exit 1
    fi
    {{_nix_build}} -o result-smoke .#smoke-test
    SIPI_REPO_ROOT="{{justfile_directory()}}" \
    INSTA_WORKSPACE_ROOT="{{justfile_directory()}}/test/e2e-rust" \
        {{justfile_directory()}}/result-smoke/bin/docker_smoke --test-threads=1

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

# Enforce shttps → sipi one-way dependency boundary.
# See sipi/shttps/CONTEXT.md and docs/adr/0001-shttps-as-strangler-fig-target.md.
shttps-context-check:
    @scripts/shttps-context-check.sh

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
