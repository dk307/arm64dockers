# =============================================================================
# hometimeline-base: FFmpeg optimised for Snapdragon 8cx Gen 3
# ARM64-only, Cortex-A78C tuned — base layer for COPY --from
#
# Built on Debian bookworm to match the glibc of python:3.14-slim (the
# upstream HomeTimeline runtime).  This ensures .so files copied from this
# image are ABI-compatible with the target container.
# =============================================================================

ARG FFMPEG_VERSION=8.1.2
ARG SVTAV1_VERSION=4.2.0

# ---------------------------------------------------------------------------
# Stage 1: Build dependencies
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 debian:bookworm AS deps

ARG CLANG_VERSION=21

ENV DEBIAN_FRONTEND=noninteractive

# Enable contrib + non-free for libfdk-aac-dev
RUN echo "deb http://deb.debian.org/debian bookworm main contrib non-free non-free-firmware" \
        > /etc/apt/sources.list \
    && echo "deb http://deb.debian.org/debian bookworm-updates main contrib non-free non-free-firmware" \
        >> /etc/apt/sources.list \
    && echo "deb http://security.debian.org/debian-security bookworm-security main contrib non-free non-free-firmware" \
        >> /etc/apt/sources.list \
    && rm -f /etc/apt/sources.list.d/debian.sources

# Core build deps
RUN apt-get update && apt-get install -y --no-install-recommends \
    wget gpg lsb-release gnupg \
    cmake make git autoconf automake libtool pkg-config \
    nasm yasm curl ca-certificates \
    libx264-dev libx265-dev libvpx-dev \
    libfdk-aac-dev libopus-dev \
    libavcodec-dev \
    libjpeg62-turbo-dev \
    libvulkan-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# LLVM 21 from official apt repo
RUN wget -qO /tmp/llvm-snapshot.gpg.key https://apt.llvm.org/llvm-snapshot.gpg.key \
    && gpg --dearmor < /tmp/llvm-snapshot.gpg.key > /usr/share/keyrings/llvm-snapshot.gpg \
    && rm /tmp/llvm-snapshot.gpg.key \
    && echo "deb [signed-by=/usr/share/keyrings/llvm-snapshot.gpg] http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm-${CLANG_VERSION} main" \
        > /etc/apt/sources.list.d/llvm-21.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        clang-${CLANG_VERSION} lld-${CLANG_VERSION} llvm-${CLANG_VERSION} libomp-${CLANG_VERSION}-dev \
    && rm -rf /var/lib/apt/lists/*

# Add LLVM bin to PATH and create convenience symlinks
RUN LLVM_DIR=$(ls -d /usr/lib/llvm-* 2>/dev/null | head -1) \
    && echo "LLVM install dir: $LLVM_DIR" \
    && for bin in clang clang++ lld ld.lld llvm-strip llvm-ar; do \
        target="/usr/bin/${bin}-${CLANG_VERSION}"; \
        if [ ! -f "$target" ]; then \
            alt=$(find "$LLVM_DIR/bin" -name "${bin}-${CLANG_VERSION}" -type f 2>/dev/null | head -1); \
            [ -n "$alt" ] && target="$alt"; \
        fi; \
        [ -f "$target" ] && ln -sf "$target" "/usr/bin/${bin}"; \
    done \
    && ln -sf "$LLVM_DIR/bin/clang" /usr/bin/clang 2>/dev/null || true \
    && ln -sf "$LLVM_DIR/bin/clang++" /usr/bin/clang++ 2>/dev/null || true \
    && ln -sf "$LLVM_DIR/bin/lld" /usr/bin/lld 2>/dev/null || true \
    && ln -sf "$LLVM_DIR/bin/llvm-strip" /usr/bin/llvm-strip 2>/dev/null || true \
    && ln -sf "$LLVM_DIR/bin/llvm-ar" /usr/bin/llvm-ar 2>/dev/null || true \
    && which clang && clang --version

# ---------------------------------------------------------------------------
# Stage 2: Build SVT-AV1 from source (not in Debian repos)
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 deps AS svtav1

ARG SVTAV1_VERSION
ARG CFLAGS="-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c \
  -fno-math-errno -fassociative-math -fno-signed-zeros \
  -fno-trapping-math -freciprocal-math -fno-plt -flto=thin"

RUN git clone --depth 1 --branch v${SVTAV1_VERSION} \
    https://gitlab.com/AOMediaCodec/SVT-AV1.git /src/svtav1

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

ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

RUN ./configure \
    --prefix=/usr/local \
    --cc=clang --cxx=clang++ \
    --enable-gpl \
    --enable-nonfree \
    --enable-version3 \
    --enable-libx264 \
    --enable-libx265 \
    --enable-libvpx \
    --enable-libsvtav1 \
    --enable-libfdk-aac \
    --enable-libopus \
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
    --extra-cflags="${CFLAGS}" \
    --extra-cxxflags="${CFLAGS}" \
    --extra-ldflags="-flto=thin" \
    && make -j"$(nproc)" \
    && make install

# ---------------------------------------------------------------------------
# Stage 4: Runtime — minimal Debian with binaries + shared libs
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 debian:bookworm AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgcc-s1 libstdc++6 curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# FFmpeg binaries and self-installed libs
COPY --from=ffmpeg /usr/local/bin/ffmpeg /usr/local/bin/ffmpeg
COPY --from=ffmpeg /usr/local/bin/ffprobe /usr/local/bin/ffprobe
COPY --from=ffmpeg /usr/local/lib/ /usr/local/lib/

# Codec shared libraries from build stage (FFmpeg links dynamically against these)
COPY --from=ffmpeg /usr/lib/aarch64-linux-gnu/ /usr/lib/aarch64-linux-gnu/

RUN ldconfig

ENV PATH=/usr/local/bin:${PATH}

LABEL org.opencontainers.image.title="hometimeline-base" \
      org.opencontainers.image.description="FFmpeg optimised for Snapdragon 8cx Gen 3 — ARM64 Cortex-A78C" \
      org.opencontainers.image.source="https://github.com/dk307/arm64dockers" \
      org.opencontainers.image.licenses="MIT"

# No EXPOSE, no ENTRYPOINT — pure base layer for COPY --from
