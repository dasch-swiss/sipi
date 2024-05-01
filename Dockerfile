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
FROM scratch

WORKDIR /app

# Copy /nix/store
COPY --from=builder /tmp/nix-store-closure /nix/store
COPY --from=builder /tmp/src/build /app
CMD ["/app/build/sipi"]
