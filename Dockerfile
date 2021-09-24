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
FROM daschswiss/sipi-base:2.4

MAINTAINER Ivan Subotic <400790+subotic@users.noreply.github.com>

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
