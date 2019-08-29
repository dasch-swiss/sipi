FROM dhlabbasel/sipi-base:18.04

MAINTAINER Ivan Subotic <ivan.subotic@unibas.ch>

# Add everything to image.
COPY . /sipi

# Install and clean-up SIPI.
RUN mkdir -p /sipi/build-linux && \
    cd /sipi/build-linux && \
    cmake -DMAKE_DEBUG:BOOL=OFF .. && \
    make && \
    cp /sipi/build-build/sipi /sipi/sipi && \
    mkdir -p /sipi/images/knora && \
    mkdir -p /sipi/cache && \
    rm -rf /sipi/vendor && \
    rm -rf /sipi/build-linux

EXPOSE 1024

WORKDIR /sipi

ENTRYPOINT [ "/sipi/sipi" ]

CMD ["--config=/sipi/config/sipi.config.lua"]