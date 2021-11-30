# Determine this makefile's path.
# Be sure to place this BEFORE `include` directives, if any.
# THIS_FILE := $(lastword $(MAKEFILE_LIST))
THIS_FILE := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

include vars.mk

# Version of the base Docker image
SIPI_BASE_VERSION := 2.4.2

.PHONY: docs-build
docs-build: ## build docs into the local 'site' folder
	mkdocs build

.PHONY: docs-serve
docs-serve: ## serve docs for local viewing
	mkdocs serve

.PHONY: docs-publish
docs-publish: ## build and publish docs to Github Pages
	mkdocs gh-deploy

.PHONY: install-requirements
install-requirements: ## install requirements for documentation
	pip3 install -r requirements.txt

.PHONY: docker-build
docker-build: ## build and publish Sipi Docker image locally
	docker buildx build --platform linux/amd64 -t $(DOCKER_IMAGE) -t $(DOCKER_REPO):latest --load .

.PHONY: docker-publish
docker-publish: ## publish Sipi Docker image to Docker-Hub
	docker buildx build --platform linux/amd64 -t $(DOCKER_IMAGE) -t $(DOCKER_REPO):latest --push .

.PHONY: compile
compile: ## compile SIPI inside Docker
	docker run -it --rm -v ${PWD}:/sipi daschswiss/sipi-base:$(SIPI_BASE_VERSION) /bin/sh -c "mkdir -p /sipi/build-linux && cd /sipi/build-linux && cmake .. && make"

.PHONY: compile-ci
compile-ci: ## compile SIPI inside Docker (no it)
	docker run --rm -v ${PWD}:/sipi daschswiss/sipi-base:$(SIPI_BASE_VERSION) /bin/sh -c "mkdir -p /sipi/build-linux && cd /sipi/build-linux && cmake .. && make"

.PHONY: test
test: ## compile and run tests inside Docker
	@mkdir -p ${PWD}/images
	docker run -it --rm -v ${PWD}:/sipi daschswiss/sipi-base:$(SIPI_BASE_VERSION) /bin/sh -c "mkdir -p /sipi/build-linux && cd /sipi/build-linux && cmake .. && make && ctest --verbose"

.PHONY: test-ci
test-ci: ## compile and run tests inside Docker (no it)
	@mkdir -p ${CURRENT_DIR}/images
	docker run --rm -v ${PWD}:/sipi daschswiss/sipi-base:$(SIPI_BASE_VERSION) /bin/sh -c "mkdir -p /sipi/build-linux && cd /sipi/build-linux && cmake .. && make && ctest --verbose"

.PHONY: test-integration
test-integration: docker-build ## run tests against locally published Sipi Docker image
	pytest -s test/integration

.PHONY: run
run: docker-build ## run SIPI inside Docker
	@mkdir -p ${CURRENT_DIR}/images
	docker run -it --rm --workdir "/sipi" --expose "1024" --expose "1025" -p 1024:1024 -p 1025:1025 daschswiss/sipi:latest

.PHONY: cmdline
cmdline: ## run SIPI inside Docker (does not compile)
	@mkdir -p ${CURRENT_DIR}/images
	docker run -it --rm -v ${PWD}:/sipi --workdir "/sipi" --expose "1024" --expose "1025" -p 1024:1024 -p 1025:1025 dhlabbasel/sipi-base:18.04 /bin/sh

.PHONY: cmdline-debug
cmdline-debug: ## run SIPI inside Docker (does not compile)
	@mkdir -p ${CURRENT_DIR}/images
	docker run -it --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --security-opt apparmor=unconfined --rm -v ${PWD}:/sipi --workdir "/sipi" --expose "1024" --expose "1025" -p 1024:1024 -p 1025:1025 dhlabbasel/sipi-base:18.04 /bin/sh

.PHONY: debug
debug: ## run SIPI inside Docker (does not compile)
	@mkdir -p ${PWD}/images
	docker run -it --privileged --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --security-opt apparmor=unconfined --rm -v ${PWD}:/sipi --workdir "/sipi" --expose "1024" --expose "1025" -p 1024:1024 -p 1025:1025 -p 1234:1234 -p 1235:1235 dhlabbasel/sipi-base:18.04 /bin/sh

.PHONY: clean
clean: ## cleans the project directory
	@rm -rf build-linux/
	@rm -rf site/

.PHONY: help
help: ## this help
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST) | sort

.DEFAULT_GOAL := help
