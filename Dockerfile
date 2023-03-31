# syntax=docker/dockerfile:1.3

# Expose (global) variables (ARGs before FROM can only be used on FROM lines and not afterwards)
ARG SIPI_BASE=daschswiss/sipi-base:2.18.0
ARG UBUNTU_BASE=ubuntu:22.04

# STAGE 1: Build
FROM $SIPI_BASE as builder

WORKDIR /tmp/sipi

# Add everything to image.
COPY . .

# Build SIPI and run unit tests.
RUN mkdir -p ./build && \
    cd ./build && \
    cmake -DMAKE_DEBUG:BOOL=OFF .. && \
    make && \
    ctest

# STAGE 2: Setup
FROM $UBUNTU_BASE as final

LABEL maintainer="support@dasch.swiss"

# Silence debconf messages
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Zurich

RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list \
  && apt-get clean \
  && apt-get update \
  && apt-get install -qyyy --no-install-recommends \
    ca-certificates \
    gnupg2 \
    tzdata \
    wget \
    byobu curl git htop man vim wget unzip \
    libllvm14 llvm-14-runtime \
    openssl \
    locales \
    uuid \
    ffmpeg \
    at \
    bc \
    imagemagick \
    libmagic1 \
    file \
  && apt-get clean

# add locales
RUN locale-gen en_US.UTF-8 && \
    locale-gen sr_RS.UTF-8

ENV LC_ALL=en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US.UTF-8

WORKDIR /sipi

EXPOSE 1024

RUN mkdir -p /sipi/images/knora && \
    mkdir -p /sipi/cache

# Copy Sipi binary and other files from the build stage
COPY --from=builder /tmp/sipi/build/sipi /sipi/sipi
COPY --from=builder /tmp/sipi/config/sipi.config.lua /sipi/config/sipi.config.lua
COPY --from=builder /tmp/sipi/config/sipi.init.lua /sipi/config/sipi.init.lua
COPY --from=builder /tmp/sipi/server/test.html /sipi/server/test.html
COPY --from=builder /tmp/sipi/scripts/test_functions.lua /sipi/scripts/test_functions.lua
COPY --from=builder /tmp/sipi/scripts/send_response.lua /sipi/scripts/send_response.lua

ENTRYPOINT [ "/sipi/sipi" ]

CMD ["--config=/sipi/config/sipi.config.lua"]
