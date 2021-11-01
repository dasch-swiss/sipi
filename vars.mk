DOCKER_REPO := daschswiss/sipi

ifeq ($(BUILD_TAG),)
	BUILD_TAG := $(shell git describe --tag --dirty --abbrev=7)
endif
ifeq ($(BUILD_TAG),)
	BUILD_TAG := $(shell git rev-parse --verify HEAD)
endif

ifeq ($(DOCKER_IMAGE),)
	DOCKER_IMAGE := $(DOCKER_REPO):$(BUILD_TAG)
endif
