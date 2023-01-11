# syntax=docker/dockerfile:1.3

# Expose (global) variables (ARGs before FROM can only be used on FROM lines and not afterwards)
ARG BUILD_TYPE=production
ARG SIPI_BASE=daschswiss/sipi-base:2.12.0
ARG UBUNTU_BASE=ubuntu:22.04

# STAGE 1: Build debug
FROM $SIPI_BASE as builder-debug

WORKDIR /sipi

# Add everything to image.
COPY . .

# Build SIPI.
RUN mkdir -p /sipi/build-linux && \
    cd /sipi/build-linux && \
    cmake -DMAKE_DEBUG:BOOL=ON .. && \
    make

# STAGE 1: Build production
FROM $SIPI_BASE as builder-production

WORKDIR /sipi

# Add everything to image.
COPY . .

# Build SIPI.
RUN mkdir -p /sipi/build-linux && \
    cd /sipi/build-linux && \
    cmake -DMAKE_DEBUG:BOOL=OFF .. && \
    make

# STAGE 2: Setup debug
FROM $UBUNTU_BASE as debug

LABEL maintainer="400790+subotic@users.noreply.github.com"

# Silence debconf messages
ARG DEBIAN_FRONTEND=noninteractive

# needs to be separate because of gnupg2 which is needed for the keyserver stuff
RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list && \
    apt-get update && apt-get -y install \
    ca-certificates \
    gnupg2 \
    byobu curl git htop man vim wget unzip

# Install build dependencies.
RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list && \
    apt-get update && apt-get -y install \
    libllvm14 llvm-14-runtime libidn14-dev \
    openssl \
    locales \
    uuid \
    ffmpeg \
    at \
    bc \
    imagemagick \
    valgrind

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
COPY --from=builder-debug /sipi/build-linux/sipi /sipi/sipi
COPY --from=builder-debug /sipi/config/sipi.config.lua /sipi/config/sipi.config.lua
COPY --from=builder-debug /sipi/config/sipi.init.lua /sipi/config/sipi.init.lua
COPY --from=builder-debug /sipi/server/test.html /sipi/server/test.html

ENTRYPOINT [ "/sipi/sipi" ]

CMD ["--config=/sipi/config/sipi.config.lua"]

# STAGE 2: Setup production
FROM $UBUNTU_BASE as production

LABEL maintainer="400790+subotic@users.noreply.github.com"

# Silence debconf messages
ARG DEBIAN_FRONTEND=noninteractive

# needs to be separate because of gnupg2 which is needed for the keyserver stuff
RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list && \
    apt-get update && apt-get -y install \
    ca-certificates \
    gnupg2 \
    wget \
    byobu curl git htop man vim wget unzip

# Install build dependencies.
RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list && \
    apt-get update && apt-get -y install \
    libllvm14 llvm-14-runtime libidn14-dev \
    openssl \
    locales \
    uuid \
    ffmpeg \
    at \
    bc \
    imagemagick

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
COPY --from=builder-production /sipi/build-linux/sipi /sipi/sipi
COPY --from=builder-production /sipi/config/sipi.config.lua /sipi/config/sipi.config.lua
COPY --from=builder-production /sipi/config/sipi.init.lua /sipi/config/sipi.init.lua
COPY --from=builder-production /sipi/server/test.html /sipi/server/test.html

ENTRYPOINT [ "/sipi/sipi" ]

CMD ["--config=/sipi/config/sipi.config.lua"]

#
# Stage 3: The final build type specific image
FROM $BUILD_TYPE
