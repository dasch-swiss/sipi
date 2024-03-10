# Determine this makefile's path.
# Be sure to place this BEFORE `include` directives, if any.
# THIS_FILE := $(lastword $(MAKEFILE_LIST))
THIS_FILE := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

include vars.mk

# Version of the base Docker image
SIPI_BASE := daschswiss/sipi-base:2.20.0
UBUNTU_BASE := ubuntu:22.04

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
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg BUILD_TAG=$(BUILD_TAG) \
		-t $(DOCKER_IMAGE) -t $(DOCKER_REPO):latest \
		--load \
		.

.PHONY: docker-test-build-aarch64
docker-test-build-aarch64: ## locally (unit) test and publish aarch64 Sipi Docker image with -aarch64 tag
	docker buildx build \
		--progress auto \
		--platform linux/arm64 \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg BUILD_TAG=$(BUILD_TAG) \
		-t $(DOCKER_IMAGE)-aarch64 -t $(DOCKER_REPO):latest \
		--load \
		.

.PHONY: docker-push-aarch64
docker-push-aarch64: ## push previously build aarch64 image to Docker hub
	docker push $(DOCKER_IMAGE)-aarch64

.PHONY: docker-test-build-amd64
docker-test-build-amd64: ## locally (unit) test and publish x86 Sipi Docker image with -amd64 tag
	docker buildx build \
		--progress auto \
		--platform linux/amd64 \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg BUILD_TAG=$(BUILD_TAG) \
		-t $(DOCKER_IMAGE)-amd64 -t $(DOCKER_REPO):latest \
		--load \
		.

.PHONY: docker-push-amd64
docker-push-amd64: ## push previously build x86 image to Docker hub
	docker push $(DOCKER_IMAGE)-amd64

.PHONY: docker-publish-manifest
docker-publish-manifest: ## publish Docker manifest combining aarch64 and x86 published images
	docker manifest create $(DOCKER_IMAGE) --amend $(DOCKER_IMAGE)-amd64 --amend $(DOCKER_IMAGE)-aarch64
	docker manifest annotate --arch amd64 --os linux $(DOCKER_IMAGE) $(DOCKER_IMAGE)-amd64
	docker manifest annotate --arch arm64 --os linux $(DOCKER_IMAGE) $(DOCKER_IMAGE)-aarch64
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
# test targets for usinf with Docker
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

.PHONY: test-integration
test-integration: docker-build ## run tests against locally published Sipi Docker image
	pytest -s test/integration

.PHONY: test-integration-ci
test-integration-ci: ## run tests against (already) locally published Sipi Docker image
	pytest -s test/integration

#####################################
# Use inside NIX develop shell
#####################################

.PHONY: build
build: ## build SIPI (inside NIX develop shell)
	cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cd build && make -j 1

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
