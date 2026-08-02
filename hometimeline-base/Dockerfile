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

# Core build deps (including Vulkan, OpenCL, libshaderc, libnuma)
RUN apt-get update && apt-get install -y --no-install-recommends \
    wget gpg lsb-release gnupg \
    cmake make git autoconf automake libtool pkg-config \
    nasm yasm curl ca-certificates \
    libx264-dev libx265-dev libvpx-dev \
    libfdk-aac-dev libopus-dev \
    libavcodec-dev \
    libjpeg62-turbo-dev \
    libssl-dev \
    libvulkan-dev libshaderc-dev ocl-icd-opencl-dev libnuma-dev \
    && rm -rf /var/lib/apt/lists/*

# Vulkan headers >= 1.3.277 required by FFmpeg 8.1.2 (bookworm ships 1.3.239).
# Install from Khronos official headers and patch vulkan.pc version.
ARG VULKAN_HEADERS_VERSION=vulkan-sdk-1.4.357.0
RUN git clone --depth 1 --branch ${VULKAN_HEADERS_VERSION} \
    https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/vulkan-headers \
    && cp -r /tmp/vulkan-headers/include/vulkan/* /usr/include/vulkan/ \
    && VULKAN_VER=$(grep '#define VK_HEADER_VERSION_COMPLETE' /usr/include/vulkan/vulkan_core.h \
        | sed 's/.*VK_MAKE_API_VERSION(0, \([0-9]*\), \([0-9]*\), VK_HEADER_VERSION)/\1.\2/') \
    && sed -i "s/^Version:.*/Version: ${VULKAN_VER}/" /usr/lib/aarch64-linux-gnu/pkgconfig/vulkan.pc \
    && echo "Vulkan headers installed: ${VULKAN_VER} (from ${VULKAN_HEADERS_VERSION})" \
    && pkg-config --modversion vulkan \
    && rm -rf /tmp/vulkan-headers

# FFmpeg 8.x uses pkg-config name "spirv_library" for libshaderc, but Debian
# only ships shaderc.pc.  Create a symlink so FFmpeg's configure can find it.
RUN ln -sf shaderc.pc /usr/lib/aarch64-linux-gnu/pkgconfig/spirv_library.pc

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
    -DBUILD_SHARED_LIBS=OFF \
    -DUSE_NUMA=OFF \
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

# Create dummy libsvtav1.so.0 so upstream COPY --from=ffmpeg-source libsvtav1.so* succeeds.
# The real SVT-AV1 is statically linked into ffmpeg — this file is never loaded.
RUN touch /usr/local/lib/libsvtav1.so.0

RUN git clone --depth 1 --branch n${FFMPEG_VERSION} \
    https://github.com/FFmpeg/FFmpeg.git /src/ffmpeg

WORKDIR /src/ffmpeg

# Include both local and system pkg-config paths so FFmpeg's configure
# can find our local libs (SVT-AV1) AND system libs (shaderc, vulkan, etc.)
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig

# Debug: verify pkg-config finds shaderc (FFmpeg uses it via name "shaderc")
RUN echo "=== pkg-config checks ===" \
    && pkg-config --exists --print-errors "shaderc >= 2019.1" \
    && pkg-config --modversion shaderc \
    && pkg-config --cflags --static shaderc \
    && pkg-config --libs --static shaderc \
    && echo "--- shaderc.pc ---" \
    && cat /usr/lib/aarch64-linux-gnu/pkgconfig/shaderc.pc \
    && echo "--- shaderc libs ---" \
    && ls -la /usr/lib/aarch64-linux-gnu/libshaderc* \
    && echo "=== shaderc pkg-config OK ==="

# Build with --enable-static --disable-shared: FFmpeg's own libs (.a) and
# SVT-AV1 (.a) are statically linked into the binary.  External codec .so
# files (libx264, libx265, libvpx, libfdk-aac, libshaderc) are still
# dynamically linked and must be present at runtime.  Vulkan and OpenCL are
# loaded at runtime via dlopen — no link-time dependency.
RUN (./configure \
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
    --enable-openssl \
    --enable-vulkan \
    --enable-libshaderc \
    --enable-opencl \
    --enable-protocol=file \
    --enable-protocol=pipe \
    --enable-protocol=tcp \
    --enable-protocol=rtsp \
    --enable-protocol=rtmp \
    --enable-protocol=hls \
    --enable-protocol=https \
    --enable-static \
    --disable-shared \
    --disable-doc \
    --extra-cflags="${CFLAGS}" \
    --extra-cxxflags="${CFLAGS}" \
    --extra-ldflags="-flto=thin" \
    && make -j"$(nproc)" \
    && make install) \
  || { echo "=== CONFIGURE FAILED — dumping config.log ===" ; \
       cat ffbuild/config.log 2>/dev/null | tail -200 ; \
       echo "=== END config.log ===" ; \
       exit 1 ; }

# ---------------------------------------------------------------------------
# Stage 4: Runtime — minimal Debian with binaries + shared libs
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 debian:bookworm AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Runtime deps: libshaderc1 (linked by ffmpeg for Vulkan SPIR-V compilation).
# Vulkan and OpenCL ICD loaders are NOT needed here — ffmpeg loads them via
# dlopen at runtime from the host system.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgcc-s1 libstdc++6 curl ca-certificates \
    libshaderc1 \
    && rm -rf /var/lib/apt/lists/*

# FFmpeg binaries (statically linked against FFmpeg core + SVT-AV1; dynamic for codec .so)
COPY --from=ffmpeg /usr/local/bin/ffmpeg /usr/local/bin/ffmpeg
COPY --from=ffmpeg /usr/local/bin/ffprobe /usr/local/bin/ffprobe
COPY --from=ffmpeg /usr/local/lib/ /usr/local/lib/

# Codec shared libraries — needed by the upstream HomeTimeline Dockerfile
COPY --from=ffmpeg /usr/lib/aarch64-linux-gnu/ /usr/lib/aarch64-linux-gnu/

RUN ldconfig

ENV PATH=/usr/local/bin:${PATH}

LABEL org.opencontainers.image.title="hometimeline-base" \
      org.opencontainers.image.description="FFmpeg optimised for Snapdragon 8cx Gen 3 — ARM64 Cortex-A78C (Vulkan + OpenCL GPU filters)" \
      org.opencontainers.image.source="https://github.com/dk307/arm64dockers" \
      org.opencontainers.image.licenses="MIT"

# No EXPOSE, no ENTRYPOINT — pure base layer for COPY --from
