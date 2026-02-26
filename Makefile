# Determine this makefile's path.
# Be sure to place this BEFORE `include` directives, if any.
# THIS_FILE := $(lastword $(MAKEFILE_LIST))
THIS_FILE := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

include vars.mk

# Version of the base Docker image
SIPI_BASE := daschswiss/sipi-base:2.23.0
UBUNTU_BASE := ubuntu:24.04

# Docker buildx cache flags (empty by default for local builds, set in CI)
DOCKER_CACHE_FROM ?=
DOCKER_CACHE_TO ?=

.PHONY: docs-build
docs-build: ## build docs into the local 'site' folder
	@$(MAKE) -C docs docs-build

.PHONY: docs-serve
docs-serve: ## serve docs for local viewing
	@$(MAKE) -C docs docs-serve

.PHONY: docs-publish
docs-publish: ## build and publish docs to Github Pages
	@$(MAKE) -C docs docs-publish

.PHONY: docs-install-requirements
docs-install-requirements: ## install requirements for documentation
	@$(MAKE) -C docs docs-install-requirements

.PHONY: install-requirements
install-requirements: docs-install-requirements ## install requirements for documentation
	pip3 install -r requirements.txt

.PHONY: docker-build
docker-build: ## build and publish Sipi Docker image locally
	docker buildx build \
		--progress auto \
		$(DOCKER_CACHE_FROM) \
		$(DOCKER_CACHE_TO) \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		-t $(DOCKER_IMAGE) -t $(DOCKER_REPO):latest \
		--load \
		.

.PHONY: docker-test-build-arm64
docker-test-build-arm64: ## locally (unit) test and publish arm64 Sipi Docker image with -arm64 tag
	docker buildx build \
		--progress auto \
		--platform linux/arm64 \
		$(DOCKER_CACHE_FROM) \
		$(DOCKER_CACHE_TO) \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		-t $(DOCKER_IMAGE)-arm64 -t $(DOCKER_REPO):latest \
		--load \
		.
	docker buildx build \
		--platform linux/arm64 \
		$(DOCKER_CACHE_FROM) \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		--target debug-symbols \
		--output type=local,dest=./debug-out \
		.
	mv ./debug-out/sipi.debug ./sipi-arm64.debug && rm -rf ./debug-out

.PHONY: docker-push-arm64
docker-push-arm64: ## push previously built arm64 image to Docker hub
	docker push $(DOCKER_IMAGE)-arm64

.PHONY: docker-test-build-amd64
docker-test-build-amd64: ## locally (unit) test and publish x86 Sipi Docker image with -amd64 tag
	docker buildx build \
		--progress auto \
		--platform linux/amd64 \
		$(DOCKER_CACHE_FROM) \
		$(DOCKER_CACHE_TO) \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		-t $(DOCKER_IMAGE)-amd64 -t $(DOCKER_REPO):latest \
		--load \
		.
	docker buildx build \
		--platform linux/amd64 \
		$(DOCKER_CACHE_FROM) \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		--target debug-symbols \
		--output type=local,dest=./debug-out \
		.
	mv ./debug-out/sipi.debug ./sipi-amd64.debug && rm -rf ./debug-out

.PHONY: docker-push-amd64
docker-push-amd64: ## push previously build x86 image to Docker hub
	docker push $(DOCKER_IMAGE)-amd64

.PHONY: docker-publish-manifest
docker-publish-manifest: ## publish Docker manifest combining arm64 and amd64 published images
	docker manifest create $(DOCKER_IMAGE) --amend $(DOCKER_IMAGE)-amd64 --amend $(DOCKER_IMAGE)-arm64
	docker manifest annotate --arch amd64 --os linux $(DOCKER_IMAGE) $(DOCKER_IMAGE)-amd64
	docker manifest annotate --arch arm64 --os linux $(DOCKER_IMAGE) $(DOCKER_IMAGE)-arm64
	docker manifest inspect $(DOCKER_IMAGE)
	docker manifest push $(DOCKER_IMAGE)

#####################################
# Remote Sipi development environment
#####################################

.PHONY: docker-build-sipi-dev-env
docker-build-sipi-dev-env: ## build and publish Sipi development environment Docker image locally
	docker buildx build \
		--progress auto \
		--build-arg UID=$(shell id -u) \
		-t daschswiss/remote-sipi-env:1.0 \
		--load \
		-f ./Dockerfile.sipi-dev-env \
		.

#####################################
# test targets for using with Docker
#####################################

.PHONY: compile
compile: ## compile SIPI inside Docker with Debug symbols
	docker run \
		--rm \
		-it \
		-v ${PWD}:/tmp/sipi \
		$(SIPI_BASE) /bin/sh -c "mkdir -p /tmp/sipi/cmake-build-debug-inside-docker && cd /tmp/sipi/cmake-build-debug-inside-docker && cmake -DMAKE_DEBUG:BOOL=ON .. && make"

.PHONY: compile-ci
compile-ci: ## compile SIPI inside Docker with Debug symbols (no it)
	docker run \
		--rm \
		-v ${PWD}:/tmp/sipi \
		$(SIPI_BASE) /bin/sh -c "mkdir -p /tmp/sipi/cmake-build-debug-inside-docker && cd /tmp/sipi/cmake-build-debug-inside-docker && cmake -DMAKE_DEBUG:BOOL=ON .. && make"

.PHONY: test
test: ## compile and run tests inside Docker with Debug symbols
	@mkdir -p ${PWD}/images
	docker run \
    	--rm \
    	-it \
    	-v ${PWD}:/tmp/sipi \
    	$(SIPI_BASE) /bin/sh -c "mkdir -p /tmp/sipi/cmake-build-debug-inside-docker && cd /tmp/sipi/cmake-build-debug-inside-docker && cmake -DMAKE_DEBUG:BOOL=ON .. && make && ctest --verbose"

.PHONY: test-ci
test-ci: ## compile and run tests inside Docker with Debug symbols (no it)
	@mkdir -p ${CURRENT_DIR}/images
	docker run \
		--rm \
		-v ${PWD}:/tmp/sipi \
		$(SIPI_BASE) /bin/sh -c "mkdir -p /tmp/sipi/cmake-build-debug-inside-docker && cd /tmp/sipi/cmake-build-debug-inside-docker && cmake -DMAKE_DEBUG:BOOL=ON .. && make && ctest --verbose"

.PHONY: test-e2e
test-e2e: ## run end-to-end locally (needs local Nix develop shell)
	(cd test/e2e; pytest -s --sipi-exec=../../build/sipi)

.PHONY: test-smoke
test-smoke: docker-build ## run smoke tests against locally published Sipi Docker image
	pytest -s test/smoke

.PHONY: test-smoke-ci
test-smoke-ci: ## run smoke tests against (already) locally published Sipi Docker image
	pytest -s test/smoke

#####################################
# Use inside NIX develop shell
#####################################

.PHONY: build
build: ## build SIPI (inside NIX develop shell)
	cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cd build && cmake --build . --parallel $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

.PHONY: run
run: compile ## run SIPI (inside NIX develop shell)
	${PWD}/build/sipi --config=${PWD}/config/sipi.config.lua

.PHONY: nix-develop
cmdline: ## open NIX develop shell
	nix develop .#clang

.PHONY: valgrind
valgrind: ## start SIPI with Valgrind (inside NIX develop shell, e.g., 'make nix-develop')
	valgrind --leak-check=yes --track-origins=yes ./build/sipi --config=/sipi/config/sipi.config.lua

.PHONY: clean
clean: ## cleans the project directory
	@rm -rf build/
	@rm -rf cmake-build-relwithdebinfo-inside-docker/
	@rm -rf site/

.PHONY: help
help: ## this help
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST) | sort

.DEFAULT_GOAL := help
