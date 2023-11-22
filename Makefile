# Determine this makefile's path.
# Be sure to place this BEFORE `include` directives, if any.
# THIS_FILE := $(lastword $(MAKEFILE_LIST))
THIS_FILE := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

include vars.mk

# Version of the base Docker image
SIPI_BASE := daschswiss/sipi-base:2.18.0
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

.PHONY: docker-build-debug
docker-build-debug: ## build and publish Sipi Docker image locally with debugging enabled
	docker buildx build \
		--progress auto \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg BUILD_TAG=$(BUILD_TAG) \
		-t $(DOCKER_IMAGE)-debug \
		--load \
		--file ./Dockerfile.debug \
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

.PHONY: docker-publish-debug
docker-publish-debug: ## publish Sipi Docker image to Docker-Hub with debugging enabled
	docker buildx build \
		--progress auto \
		--platform linux/amd64,linux/arm64 \
		--build-arg BUILD_TYPE=debug \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg BUILD_TAG=$(BUILD_TAG) \
		-t $(DOCKER_IMAGE)-debug \
		--push \
		-f ./Dockerfile.debug \
		.

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
# test targets
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
# other targets
#####################################

.PHONY: run
run: compile ## run SIPI (needs to be run inside devcontainer)
	${PWD}/build/sipi --config=${PWD}/config/sipi.config.lua

.PHONY: cmdline
cmdline: ## open shell inside Docker container
	@mkdir -p ${CURRENT_DIR}/images
	docker run -it --rm \
		-v ${PWD}:/sipi \
		--workdir "/sipi" \
		--expose "1024" \
		--expose "1025" \
		-p 1024:1024 \
		-p 1025:1025 \
		${SIPI_BASE} /bin/bash

.PHONY: shell
shell: ## open shell inside privileged Docker container (does not compile)
	@mkdir -p ${CURRENT_DIR}/images
	docker run \
		-it --rm \
		--privileged \
		--cap-add=SYS_PTRACE \
		--security-opt seccomp=unconfined \
		--security-opt apparmor=unconfined \
		-v ${PWD}:/sipi \
		--workdir "/sipi" \
		--expose "1024" \
		--expose "1025" \
		-p 1024:1024 \
		-p 1025:1025 \
		-p 1234:1234 \
		-p 1235:1235 \
		${SIPI_BASE} /bin/bash

.PHONY: valgrind
valgrind: ## start SIPI with Valgrind (needs to be started inside Docker container, e.g., 'make shell')
	valgrind --leak-check=yes --track-origins=yes ./build/sipi --config=/sipi/config/sipi.config.lua

.PHONY: clean
clean: ## cleans the project directory
	@rm -rf build/
	@rm -rf site/

.PHONY: help
help: ## this help
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST) | sort

.DEFAULT_GOAL := help
