# STAGE 1: Build
FROM daschswiss/sipi-base:2.4 as builder

WORKDIR /sipi

# Add everything to image.
COPY . .

# Build SIPI.
RUN mkdir -p /sipi/build-linux && \
    cd /sipi/build-linux && \
    cmake -DMAKE_DEBUG:BOOL=OFF .. && \
    make

# STAGE 2: Setup
FROM ubuntu:20.04

MAINTAINER Ivan Subotic <400790+subotic@users.noreply.github.com>

# Silence debconf messages
ARG DEBIAN_FRONTEND=noninteractive

RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list && \
    apt-get clean && apt-get -qq update && apt-get -y install \
    ca-certificates \
    gnupg2

# Install build dependencies.
RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list && \
    echo 'deb http://apt.llvm.org/focal/ llvm-toolchain-focal-11 main' | tee -a /etc/apt/sources.list && \
    echo 'deb-src http://apt.llvm.org/focal/ llvm-toolchain-focal-11 main' | tee -a /etc/apt/sources.list && \
    apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 15CF4D18AF4F7421 && \
    apt-get clean && apt-get -qq update && apt-get -y install \
    ca-certificates \
    byobu curl git htop man vim wget unzip \
    libllvm11 llvm-11-runtime \
    openssl \
    libidn11-dev \
    locales \
    uuid \
    ffmpeg \
    at

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
COPY --from=builder /sipi/build-linux/sipi /sipi/sipi
COPY --from=builder /sipi/config/sipi.config.lua /sipi/config/sipi.config.lua
COPY --from=builder /sipi/config/sipi.init.lua /sipi/config/sipi.init.lua
COPY --from=builder /sipi/server/test.html /sipi/server/test.html

ENTRYPOINT [ "/sipi/sipi" ]

CMD ["--config=/sipi/config/sipi.config.lua"]
