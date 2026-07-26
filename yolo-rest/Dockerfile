ARG NCNN_TAG=20260526
ARG MODELS_TAG=yolo-models-v1

# ============================================================
# Stage 1 — build ncnn + server binary
# ============================================================
FROM --platform=linux/arm64 ubuntu:24.04 AS builder

ARG NCNN_TAG
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang lld llvm libomp-dev cmake make git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# ── ncnn (CPU-only, static, clang + thinLTO) ──
RUN git clone --depth 1 --branch ${NCNN_TAG} https://github.com/Tencent/ncnn.git /ncnn

WORKDIR /ncnn

RUN cmake -B build-clang -DCMAKE_BUILD_TYPE=Release \
    -DNCNN_VULKAN=OFF \
    -DNCNN_BUILD_EXAMPLES=OFF \
    -DNCNN_BUILD_TOOLS=OFF \
    -DNCNN_BUILD_BENCHMARK=OFF \
    -DNCNN_ARM82=ON \
    -DNCNN_SHARED_LIB=OFF \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_AR="$(command -v llvm-ar)" \
    -DCMAKE_RANLIB="$(command -v llvm-ranlib)" \
    -DCMAKE_C_FLAGS="-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math -freciprocal-math -fno-plt -flto=thin" \
    -DCMAKE_CXX_FLAGS="-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math -freciprocal-math -fno-plt -flto=thin" \
    && cmake --build build-clang -j"$(nproc)" \
    && cmake --install build-clang

# ── server binary ──
COPY server/ /build/server/
WORKDIR /build/server

RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_AR="$(command -v llvm-ar)" \
    -DCMAKE_RANLIB="$(command -v llvm-ranlib)" \
    -DCMAKE_PREFIX_PATH=/ncnn/build-clang/install \
    -DCMAKE_C_FLAGS="-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c -flto=thin -fopenmp" \
    -DCMAKE_CXX_FLAGS="-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c -flto=thin -fopenmp" \
    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -flto=thin" \
    && cmake --build build -j"$(nproc)" \
    && cp build/yolo_server /yolo_server

# ============================================================
# Stage 2 — runtime
# ============================================================
FROM --platform=linux/arm64 ubuntu:24.04

ARG MODELS_TAG
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libomp5 curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# server binary
COPY --from=builder /yolo_server /usr/local/bin/yolo_server

# models (from GitHub release assets)
RUN mkdir -p /models
ADD https://github.com/dk307/arm64dockers/releases/download/${MODELS_TAG}/yolo26n_opt.param /models/
ADD https://github.com/dk307/arm64dockers/releases/download/${MODELS_TAG}/yolo26n_opt.bin /models/
ADD https://github.com/dk307/arm64dockers/releases/download/${MODELS_TAG}/yolo26m_opt.param /models/
ADD https://github.com/dk307/arm64dockers/releases/download/${MODELS_TAG}/yolo26m_opt.bin /models/

EXPOSE 18080

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -sf http://localhost:18080/health || exit 1

ENTRYPOINT ["yolo_server"]
CMD ["--port", "18080", "--model_type", "yolo26m"]
