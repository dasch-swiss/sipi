# syntax=docker/dockerfile:1.3

# Expose (global) variables (ARGs before FROM can only be used on FROM lines and not afterwards)
ARG SIPI_BASE=daschswiss/sipi-base:2.18.0
ARG UBUNTU_BASE=ubuntu:22.04

# STAGE 1: Build
FROM $SIPI_BASE as builder

ARG UID=1000
RUN useradd -m -u ${UID} -s /bin/bash builder
USER builder

WORKDIR /tmp/sipi

# Add everything to image.
COPY --chown=builder . .

# Build SIPI.
RUN mkdir -p ./cmake-build-debug-inside-docker && \
    cd ./cmake-build-debug-inside-docker && \
    cmake -DMAKE_DEBUG:BOOL=OFF .. && \
    make

# STAGE 2: Setup
FROM $UBUNTU_BASE as final

LABEL maintainer="400790+subotic@users.noreply.github.com"

# Silence debconf messages
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Zurich

# needs to be separate because of gnupg2 which is needed for the keyserver stuff
RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list \
  && apt-get clean \
  && apt-get update \
  && apt-get -y install \
    ca-certificates \
    gnupg2 \
    tzdata \
    wget \
    byobu curl git htop man vim wget unzip \
  && apt-get clean

# Install build dependencies.
RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list \
  && apt-get clean \
  && apt-get update \
  && apt-get -y install \
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
COPY --from=builder /tmp/sipi/cmake-build-debug-inside-docker/sipi /sipi/sipi
COPY --from=builder /tmp/sipi/config/sipi.config.lua /sipi/config/sipi.config.lua
COPY --from=builder /tmp/sipi/config/sipi.init.lua /sipi/config/sipi.init.lua
COPY --from=builder /tmp/sipi/server/test.html /sipi/server/test.html
COPY --from=builder /tmp/sipi/scripts/test_functions.lua /sipi/scripts/test_functions.lua
COPY --from=builder /tmp/sipi/scripts/send_response.lua /sipi/scripts/send_response.lua

ENTRYPOINT [ "/sipi/sipi" ]

CMD ["--config=/sipi/config/sipi.config.lua"]
