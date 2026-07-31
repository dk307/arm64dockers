# =============================================================================
# hometimeline-base: FFmpeg optimised for Snapdragon 8cx Gen 3
# ARM64-only, Cortex-A78C tuned — base layer for COPY --from
# =============================================================================

ARG FFMPEG_VERSION=8.1.2
ARG SVTAV1_VERSION=3.0.2

# ---------------------------------------------------------------------------
# Stage 1: Build dependencies
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 ubuntu:26.04 AS deps

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang lld llvm libomp-dev \
    cmake make git autoconf automake libtool pkg-config \
    nasm yasm curl ca-certificates \
    libx264-dev libx265-dev libvpx-dev \
    libfdk-aac-dev libopus-dev \
    libjpeg-dev \
    libvulkan-dev libspirv-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# Stage 2: Build SVT-AV1 from source (not in Ubuntu repos)
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 deps AS svtav1

ARG SVTAV1_VERSION
ARG CFLAGS="-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c \
  -fno-math-errno -fassociative-math -fno-signed-zeros \
  -fno-trapping-math -freciprocal-math -fno-plt -flto=thin"

RUN git clone --depth 1 --branch v${SVTAV1_VERSION} \
    https://github.com/AOMediaCodec/SVT-AV1.git /src/svtav1

WORKDIR /src/svtav1

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS="${CFLAGS}" \
    -DCMAKE_CXX_FLAGS="${CFLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="-flto=thin" \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_APPS=OFF \
    -DBUILD_DEC=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build build -j"$(nproc)" \
    && cmake --install build

# ---------------------------------------------------------------------------
# Stage 3: Build FFmpeg from source
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 deps AS ffmpeg

ARG FFMPEG_VERSION
ARG CFLAGS="-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c \
  -fno-math-errno -fassociative-math -fno-signed-zeros \
  -fno-trapping-math -freciprocal-math -fno-plt -flto=thin"

COPY --from=svtav1 /usr/local/lib/ /usr/local/lib/
COPY --from=svtav1 /usr/local/include/ /usr/local/include/
COPY --from=svtav1 /usr/local/lib/pkgconfig/ /usr/local/lib/pkgconfig/

RUN git clone --depth 1 --branch n${FFMPEG_VERSION} \
    https://github.com/FFmpeg/FFmpeg.git /src/ffmpeg

WORKDIR /src/ffmpeg

ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:${PKG_CONFIG_PATH}

RUN ./configure \
    --prefix=/usr/local \
    --enable-gpl \
    --enable-version3 \
    --enable-libx264 \
    --enable-libx265 \
    --enable-libvpx \
    --enable-libsvtav1 \
    --enable-libfdk-aac \
    --enable-libopus \
    --enable-libjpeg-turbo \
    --enable-vulkan \
    --enable-openssl \
    --enable-protocol=file \
    --enable-protocol=pipe \
    --enable-protocol=tcp \
    --enable-protocol=rtsp \
    --enable-protocol=rtmp \
    --enable-protocol=hls \
    --enable-protocol=https \
    --enable-shared \
    --disable-static \
    --disable-doc \
    --disable-programs \
    --extra-cflags="${CFLAGS}" \
    --extra-cxxflags="${CFLAGS}" \
    --extra-ldflags="-flto=thin" \
    && make -j"$(nproc)" \
    && make install

# ---------------------------------------------------------------------------
# Stage 4: Runtime — minimal Ubuntu 26.04 with binaries + shared libs
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 ubuntu:26.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgcc-s1 libstdc++6 \
    libx264-163 libx265-199 libvpx7 libopus0 \
    libjpeg8 libva2 libssl3 \
    libvulkan1 libspirv-tools \
    && rm -rf /var/lib/apt/lists/*

COPY --from=ffmpeg /usr/local/bin/ffmpeg /usr/local/bin/ffmpeg
COPY --from=ffmpeg /usr/local/bin/ffprobe /usr/local/bin/ffprobe
COPY --from=ffmpeg /usr/local/lib/ /usr/local/lib/

RUN ldconfig

ENV LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH}
ENV PATH=/usr/local/bin:${PATH}

LABEL org.opencontainers.image.title="hometimeline-base" \
      org.opencontainers.image.description="FFmpeg optimised for Snapdragon 8cx Gen 3 — ARM64 Cortex-A78C" \
      org.opencontainers.image.source="https://github.com/dk307/arm64dockers" \
      org.opencontainers.image.licenses="MIT"

# No EXPOSE, no ENTRYPOINT — pure base layer for COPY --from
