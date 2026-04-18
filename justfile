# Sipi — IIIF-compatible media server
# Run `just` to list all available recipes.

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
# Nix (run inside `nix develop`)
#####################################

# Build SIPI (debug + coverage, inside Nix shell)
nix-build:
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON --log-context
    cmake --build ./build --parallel {{nproc}}

# Run unit tests (inside Nix shell)
nix-test:
    cd build && ctest --output-on-failure

# Run Rust e2e tests (requires built sipi in build/ — run `just nix-build` first)
rust-test-e2e:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -x "{{justfile_directory()}}/build/sipi" ]; then
        echo "Missing build/sipi. Run 'just nix-build' (inside 'nix develop') first." >&2
        exit 1
    fi
    cd test/e2e-rust && SIPI_BIN={{justfile_directory()}}/build/sipi cargo test -- --test-threads=1

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

# Run Hurl HTTP contract tests (requires built sipi in build/)
hurl-test:
    #!/usr/bin/env bash
    set -euo pipefail
    cd test/_test_data && {{justfile_directory()}}/build/sipi --config config/sipi.e2e-test-config.lua &
    SIPI_PID=$!
    # Don't propagate sipi's SIGTERM exit code (143) as the recipe exit code.
    # `|| true` on each command — without it, `set -e` makes `wait` (which
    # returns 143 after we SIGTERMed sipi) override the hurl success code.
    trap 'kill $SIPI_PID 2>/dev/null || true; wait $SIPI_PID 2>/dev/null || true' EXIT
    READY=0
    for i in $(seq 1 30); do
        curl -sf http://127.0.0.1:1024/unit/lena512.jp2/info.json > /dev/null 2>&1 && READY=1 && break
        sleep 0.5
    done
    if [ "$READY" -ne 1 ]; then echo "ERROR: sipi failed to start within 15s"; exit 1; fi
    cd {{justfile_directory()}}/test/hurl && hurl --test --insecure --variable host=http://127.0.0.1:1024 *.hurl

# Generate coverage XML report via gcovr (inside Nix shell)
nix-coverage:
    cd build && gcovr -j {{nproc}} --delete --root ../ --print-summary --xml-pretty --xml coverage.xml . \
        --gcov-executable "llvm-cov gcov" \
        --gcov-ignore-parse-errors=negative_hits.warn_once_per_file \
        --gcov-ignore-errors=no_working_dir_found \
        --exclude '../test/' \
        --exclude '../fuzz/' \
        --exclude '../ext/' \
        --exclude '../include/'

# Generate coverage HTML report via lcov (inside Nix shell)
nix-coverage-html:
    cd build && lcov --capture --directory . --output-file coverage.info \
        && lcov --remove coverage.info '/usr/*' --output-file coverage.info \
        && lcov --remove coverage.info '*/test/*' --output-file coverage.info \
        && genhtml coverage.info --output-directory coverage

# Run all tests then generate coverage report (inside Nix shell)
nix-coverage-full: nix-build nix-test rust-test-e2e hurl-test nix-coverage

# Build SIPI with ASan+UBSan (inside Nix shell, Clang only)
nix-build-sanitized:
    cmake -B build-sanitized -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON --log-context
    cmake --build ./build-sanitized --parallel {{nproc}}

# Build with sanitizers and run unit tests (inside Nix shell)
nix-test-sanitized: nix-build-sanitized
    cd build-sanitized && ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 ctest --output-on-failure

# Run SIPI server (inside Nix shell)
nix-run:
    {{justfile_directory()}}/build/sipi --config={{justfile_directory()}}/config/sipi.config.lua

# Run SIPI with Valgrind (inside Nix shell)
nix-valgrind:
    valgrind --leak-check=yes --track-origins=yes ./build/sipi --config={{justfile_directory()}}/config/sipi.config.lua

# Build SIPI with RelWithDebInfo (inside Nix shell)
build:
    cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo --log-context
    cd build && cmake --build . --parallel {{nproc}}

#####################################
# Nix full builds (no shell required)
#####################################

# Build release binary via nix build
# Run `just kakadu-fetch` once before first build — see docs/src/development/kakadu.md.
nix-build-release:
    nix build .#default
    @echo "Binary at: $(readlink result)/bin/sipi"

# Build static amd64 binary via Nix
nix-build-static-amd64:
    nix build .#static-amd64

# Build static arm64 binary via Nix
nix-build-static-arm64:
    nix build .#static-arm64

# Build Docker image via Nix
nix-docker-build:
    $(nix build .#docker-stream --print-out-paths) | docker load

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

# Clean build artifacts
clean:
    rm -rf build/
    rm -rf cmake-build-relwithdebinfo-inside-docker/
    rm -rf site/
