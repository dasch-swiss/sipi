# List all recipies
default:
    just --list --unsorted

# Serve the doxygen documentation. Needs to be first built through cmake.
doxygen-serve:
    cd ./doxygen/generated/html && python3 -m http.server

# Start Docker container for running the binary
docker-run:
    docker run -it --rm -v /Users/subotic/_github.com/dasch-swiss/sipi:/app -v /Volumes/PhotoArchive:/scratch -w /app daschswiss/remote-sipi-env:1.0 /bin/bash


# build and publish Sipi Docker image locally
docker-build:
	docker buildx build \
		--progress auto \
		--build-arg SIPI_BASE=daschswiss/sipi-base:2.23.0 \
		--build-arg UBUNTU_BASE=ubuntu:22.04 \
		--build-arg BUILD_TAG=development \
		-t daschswiss/sipi:development -t daschswiss/sipi:latest \
		--load \
		.
# run smoke tests against locally published Sipi Docker image
test-smoke:
	pytest -s test/smoke

#
# The following commands need to be run inside a Nix development shell
#

# Open the Nix development shell with the clang environment: `just clang`
clang:
	nix develop .#clang

# Open the Nix development shell with the gcc environment: `just gcc`
gcc:
    nix develop

# Build the `sipi` binary
build:
	cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON
	cmake --build ./build --parallel

# Run the tests
test: build
    cd build && ctest --output-on-failure

# Print the test coverage (using gcov executable, thus assuming GCC)
coverage: test
    cd build && gcovr -j 4 --delete --root ../ --print-summary --xml-pretty --xml coverage.xml . --gcov-executable gcov

# Print the test coverage
coverage1: test
    cd build
    lcov --capture --directory . --output-file coverage.info
    lcov --remove coverage.info '/usr/*' --output-file coverage.info
    lcov --remove coverage.info '*/test/*' --output-file coverage.info
    genhtml coverage.info --output-directory coverage
    open coverage/index.html

# Run the `sipi` binary
run:
	${PWD}/build/sipi --config=${PWD}/config/sipi.config.lua

# start SIPI with Valgrind (inside NIX develop shell, e.g., 'nix clang')
valgrind:
	valgrind --leak-check=yes --track-origins=yes ./build/sipi --config=/sipi/config/sipi.config.lua

# query version of the project
version:
    cmake -P query_version.cmake
