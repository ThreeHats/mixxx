# Build environment for the muxic fork: Ubuntu 22.04, the same base as the rig.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

COPY debian_buildenv.sh /tmp/debian_buildenv.sh

RUN apt-get update \
    && apt-get install -y --no-install-recommends sudo ca-certificates git ninja-build \
    && bash /tmp/debian_buildenv.sh setup \
    && apt-get install -y --no-install-recommends \
        gpac \
        imagemagick \
        libgl1-mesa-dri \
        liblo-dev \
        sqlite3 \
        x11-utils \
        xdotool \
        xvfb \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update \
    && apt-get install -y --no-install-recommends clang-format python3 \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 22.04 has hidapi 0.11 and Mixxx needs 0.14. The static library
# goes into the Mixxx binary, thus the rig needs no new package.
RUN git clone --depth 1 --branch hidapi-0.14.0 https://github.com/libusb/hidapi.git /tmp/hidapi \
    && cmake -G Ninja -S /tmp/hidapi -B /tmp/hidapi/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    && cmake --build /tmp/hidapi/build --target install \
    && rm -rf /tmp/hidapi

ENV CCACHE_DIR=/ccache \
    CCACHE_MAXSIZE=10G \
    CCACHE_NOHASHDIR=1 \
    CCACHE_SLOPPINESS=pch_defines,time_macros \
    QT_QPA_PLATFORM=offscreen
