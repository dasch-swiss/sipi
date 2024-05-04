# syntax=docker/dockerfile:1.3

# Nix builder
FROM nixos/nix:2.22.0 AS builder

# Copy our source and setup our working dir.
COPY . /tmp/src
WORKDIR /tmp/src

# Enable the Nix experimental features.
RUN echo "experimental-features = nix-command flakes" >> /etc/nix/nix.conf

# Install and use cachix for caching .
RUN nix-env -iA cachix -f https://cachix.org/api/v1/install
RUN --mount=type=secret,id=CACHIX_AUTH_TOKEN cachix authtoken $(cat /run/secrets/CACHIX_AUTH_TOKEN) \
    && cachix use dasch-swiss \
    && nix develop --profile dev-profile -c true \
    && cachix push dasch-swiss dev-profile

# Build SIPI and run unit tests.
RUN nix build

# Copy the Nix store closure into a directory. The Nix store closure is the
# entire set of Nix store values that we need for our build.
RUN mkdir /tmp/nix-store-closure
RUN cp -R $(nix-store -qR result/) /tmp/nix-store-closure

# Final image is based on scratch. We copy a bunch of Nix dependencies
# but they're fully self-contained so we don't need Nix anymore.
FROM ubuntu:24.04

ARG TARGETPLATFORM
ARG BUILDPLATFORM

LABEL maintainer="support@dasch.swiss"

# Silence debconf messages
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Zurich

# Install some basic tools
RUN sed -i 's/# \(.*multiverse$\)/\1/g' /etc/apt/sources.list  \
  && apt-get clean \
  && apt-get -qq update  \
  && apt-get -y install \
    tzdata \
    wget \
    byobu curl git htop man vim wget unzip \
    ca-certificates \
    gnupg2 \
    software-properties-common \
  && apt-get clean

# Install pid1 for reaping zombies.
RUN if [ "$TARGETPLATFORM" = "linux/amd64" ]; then \
        curl -L https://github.com/fpco/pid1-rs/releases/download/v0.1.2/pid1-x86_64-unknown-linux-musl -o /usr/sbin/pid1 && chmod +x /usr/sbin/pid1; \
    elif [ "$TARGETPLATFORM" = "linux/arm64" ]; then \
        curl -L https://github.com/fpco/pid1-rs/releases/download/v0.1.2/pid1-aarch64-unknown-linux-musl -o /usr/sbin/pid1 && chmod +x /usr/sbin/pid1; \
    else \
        echo "No supported target architecture selected"; \
    fi

WORKDIR /sipi

RUN mkdir -p /sipi/images/knora \
    && mkdir -p /sipi/cache

# Copy /nix/store
COPY --from=builder /tmp/nix-store-closure /nix/store

# Copy Sipi binary and other files from the build stage
COPY --from=builder /tmp/sipi/result/bin/sipi /sipi/sipi
COPY --from=builder /tmp/sipi/config/sipi.config.lua /sipi/config/sipi.config.lua
COPY --from=builder /tmp/sipi/config/sipi.init.lua /sipi/config/sipi.init.lua
COPY --from=builder /tmp/sipi/server/test.html /sipi/server/test.html
COPY --from=builder /tmp/sipi/scripts/test_functions.lua /sipi/scripts/test_functions.lua
COPY --from=builder /tmp/sipi/scripts/send_response.lua /sipi/scripts/send_response.lua

EXPOSE 1024

ENTRYPOINT [ "/usr/sbin/pid1", "--verbose", "--", "/sipi/sipi" ]

CMD [ "--config=/sipi/config/sipi.config.lua" ]
