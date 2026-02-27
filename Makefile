# Determine this makefile's path.
# Be sure to place this BEFORE `include` directives, if any.
# THIS_FILE := $(lastword $(MAKEFILE_LIST))
THIS_FILE := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

include vars.mk

# Version of the base Docker image
SIPI_BASE := daschswiss/sipi-base:2.23.0
UBUNTU_BASE := ubuntu:24.04

# Detect available CPU cores (cross-platform: Linux nproc, macOS sysctl, fallback 4)
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Auto-detect GHA cache when running in GitHub Actions.
# ACTIONS_CACHE_URL and ACTIONS_RUNTIME_TOKEN are set automatically by the
# GitHub Actions runner and inherited by Make as environment variables.
ifneq ($(ACTIONS_CACHE_URL),)
  DOCKER_CACHE_FROM = --cache-from type=gha,url=$(ACTIONS_CACHE_URL),token=$(ACTIONS_RUNTIME_TOKEN)
  DOCKER_CACHE_TO = --cache-to type=gha,mode=max,url=$(ACTIONS_CACHE_URL),token=$(ACTIONS_RUNTIME_TOKEN)
else
  DOCKER_CACHE_FROM =
  DOCKER_CACHE_TO =
endif

#####################################
# Docker targets (used by CI and local)
#####################################

.PHONY: docker-build
docker-build: ## build and publish Sipi Docker image locally
	docker buildx build \
		--progress auto \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		$(DOCKER_CACHE_FROM) \
		$(DOCKER_CACHE_TO) \
		-t $(DOCKER_IMAGE) -t $(DOCKER_REPO):latest \
		--load \
		.

.PHONY: docker-test-build-arm64
docker-test-build-arm64: ## build + test arm64 Docker image, extract debug symbols
	docker buildx build \
		--progress auto \
		--platform linux/arm64 \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		$(DOCKER_CACHE_FROM) \
		$(DOCKER_CACHE_TO) \
		-t $(DOCKER_IMAGE)-arm64 -t $(DOCKER_REPO):latest \
		--load \
		.
	docker buildx build \
		--platform linux/arm64 \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		$(DOCKER_CACHE_FROM) \
		$(DOCKER_CACHE_TO) \
		--target debug-symbols \
		--output type=local,dest=./debug-out \
		.
	mv ./debug-out/sipi.debug ./sipi-arm64.debug && rm -rf ./debug-out

.PHONY: docker-push-arm64
docker-push-arm64: ## push previously built arm64 image to Docker hub
	docker push $(DOCKER_IMAGE)-arm64

.PHONY: docker-test-build-amd64
docker-test-build-amd64: ## build + test amd64 Docker image, extract debug symbols
	docker buildx build \
		--progress auto \
		--platform linux/amd64 \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		$(DOCKER_CACHE_FROM) \
		$(DOCKER_CACHE_TO) \
		-t $(DOCKER_IMAGE)-amd64 -t $(DOCKER_REPO):latest \
		--load \
		.
	docker buildx build \
		--platform linux/amd64 \
		--build-arg SIPI_BASE=$(SIPI_BASE) \
		--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \
		--build-arg VERSION=$(BUILD_TAG) \
		$(DOCKER_CACHE_FROM) \
		$(DOCKER_CACHE_TO) \
		--target debug-symbols \
		--output type=local,dest=./debug-out \
		.
	mv ./debug-out/sipi.debug ./sipi-amd64.debug && rm -rf ./debug-out

.PHONY: docker-push-amd64
docker-push-amd64: ## push previously built amd64 image to Docker hub
	docker push $(DOCKER_IMAGE)-amd64

.PHONY: docker-publish-manifest
docker-publish-manifest: ## publish Docker manifest combining arm64 and amd64 images
	docker manifest create $(DOCKER_IMAGE) --amend $(DOCKER_IMAGE)-amd64 --amend $(DOCKER_IMAGE)-arm64
	docker manifest annotate --arch amd64 --os linux $(DOCKER_IMAGE) $(DOCKER_IMAGE)-amd64
	docker manifest annotate --arch arm64 --os linux $(DOCKER_IMAGE) $(DOCKER_IMAGE)-arm64
	docker manifest inspect $(DOCKER_IMAGE)
	docker manifest push $(DOCKER_IMAGE)

.PHONY: docker-build-sipi-dev-env
docker-build-sipi-dev-env: ## build Sipi remote development environment Docker image
	docker buildx build \
		--progress auto \
		--build-arg UID=$(shell id -u) \
		$(DOCKER_CACHE_FROM) \
		$(DOCKER_CACHE_TO) \
		-t daschswiss/remote-sipi-env:1.0 \
		--load \
		-f ./Dockerfile.sipi-dev-env \
		.

#####################################
# Smoke tests (run against Docker image)
#####################################

.PHONY: install-requirements
install-requirements: docs-install-requirements ## install pip requirements for smoke/e2e tests
	pip3 install -r requirements.txt

.PHONY: test-smoke
test-smoke: docker-build ## build Docker image and run smoke tests
	pytest -s test/smoke

.PHONY: test-smoke-ci
test-smoke-ci: ## run smoke tests against already-built Docker image
	pytest -s test/smoke

#####################################
# Nix targets (run inside Nix develop shell)
#####################################

.PHONY: nix-build
nix-build: ## build SIPI (debug + coverage, inside Nix shell)
	cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON --log-context
	cmake --build ./build --parallel $(NPROC)

.PHONY: nix-test
nix-test: ## run unit tests (inside Nix shell)
	cd build && ctest --output-on-failure

.PHONY: nix-test-e2e
nix-test-e2e: ## run e2e tests (inside Nix shell)
	cd test/e2e && pytest -s --sipi-exec=../../build/sipi

.PHONY: nix-coverage
nix-coverage: ## generate coverage XML report via gcovr (inside Nix shell)
	cd build && gcovr -j $(NPROC) --delete --root ../ --print-summary --xml-pretty --xml coverage.xml . --gcov-executable gcov --gcov-ignore-parse-errors=negative_hits.warn_once_per_file

.PHONY: nix-coverage-html
nix-coverage-html: ## generate coverage HTML report via lcov (inside Nix shell)
	cd build && lcov --capture --directory . --output-file coverage.info \
		&& lcov --remove coverage.info '/usr/*' --output-file coverage.info \
		&& lcov --remove coverage.info '*/test/*' --output-file coverage.info \
		&& genhtml coverage.info --output-directory coverage

.PHONY: nix-run
nix-run: ## run SIPI server (inside Nix shell)
	$(PWD)/build/sipi --config=$(PWD)/config/sipi.config.lua

.PHONY: nix-valgrind
nix-valgrind: ## run SIPI with Valgrind (inside Nix shell)
	valgrind --leak-check=yes --track-origins=yes ./build/sipi --config=$(PWD)/config/sipi.config.lua

.PHONY: nix-doxygen-serve
nix-doxygen-serve: ## serve doxygen docs (build via cmake first, inside Nix shell)
	cd ./doxygen/generated/html && python3 -m http.server

.PHONY: build
build: ## build SIPI with RelWithDebInfo (inside Nix shell)
	cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo --log-context
	cd build && cmake --build . --parallel $(NPROC)

#####################################
# Documentation
#####################################

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

#####################################
# Utilities
#####################################

.PHONY: clean
clean: ## clean build artifacts
	@rm -rf build/
	@rm -rf cmake-build-relwithdebinfo-inside-docker/
	@rm -rf site/

.PHONY: help
help: ## this help
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z0-9_-]+:.*?## / {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST) | sort

.DEFAULT_GOAL := help
