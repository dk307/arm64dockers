# =============================================================================
# llama-cpp-embed-nomic: nomic-embed-text embedding server
# ARM64-only, Cortex-A78C optimised
# =============================================================================

# ---------------------------------------------------------------------------
# Stage 1: Build llama.cpp
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 debian:bookworm AS builder

RUN apt-get update && apt-get install -y \
    clang cmake make git curl \
    libgcc-s1 libc6-dev \
    && rm -rf /var/lib/apt/lists/*

ARG LLAMA_CPP_TAG

WORKDIR /src
RUN git clone --depth 1 --branch ${LLAMA_CPP_TAG} \
    https://github.com/ggml-org/llama.cpp.git .

ARG CFLAGS="-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c \
  -fno-math-errno -fassociative-math -fno-signed-zeros \
  -fno-trapping-math -freciprocal-math -fno-plt -flto=thin"

RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS="${CFLAGS}" \
    -DCMAKE_CXX_FLAGS="${CFLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="-flto=thin" \
    -DCMAKE_SHARED_LINKER_FLAGS="-flto=thin" \
    -DGGML_NATIVE=OFF \
    -DGGML_BLAS=OFF \
    -DLLAMA_BUILD_TESTS=OFF \
    -DBUILD_TESTING=OFF \
    -DLLAMA_TESTS_INSTALL=OFF \
    -DLLAMA_BUILD_EXAMPLES=OFF \
    -DLLAMA_BUILD_UI=OFF \
    -DLLAMA_USE_PREBUILT_UI=OFF \
    -DLLAMA_BUILD_SERVER=ON \
    -DLLAMA_BUILD_TOOLS=ON && \
    cmake --build build --config Release -j$(nproc)

# ---------------------------------------------------------------------------
# Stage 2: Download model
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 debian:bookworm AS model

ARG MODEL_URL=https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.f16.gguf
ARG MODEL_DIR=/models

RUN apt-get update && apt-get install -y curl && rm -rf /var/lib/apt/lists/*
RUN mkdir -p ${MODEL_DIR} && \
    curl -L -o ${MODEL_DIR}/model.gguf ${MODEL_URL}

# ---------------------------------------------------------------------------
# Stage 3: Runtime
# ---------------------------------------------------------------------------
FROM --platform=linux/arm64 debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y \
    libgcc-s1 libstdc++6 curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/bin/ /llm/llama-cpp/bin/
COPY --from=builder /src/build/lib*/ /llm/llama-cpp/lib/
COPY --from=model /models/ /llm/models/

ENV LD_LIBRARY_PATH=/llm/llama-cpp/lib

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
  CMD curl -sf http://localhost:8080/health || exit 1

ENTRYPOINT ["/llm/llama-cpp/bin/llama-server"]
CMD ["-m", "/llm/models/model.gguf", \
     "--embedding", \
     "--alias", "nomic-embed-text", \
     "--override-kv", "nomic-bert.context_length=int:8192", \
     "--pooling", "mean", \
     "--embd-normalize", "2", \
     "-c", "8192", \
     "--rope-scaling", "yarn", \
     "--rope-freq-scale", ".75", \
     "-b", "8192", "-ub", "8192", \
     "-t", "4", "-tb", "4", \
     "--flash-attn", "on", \
     "--no-mmap", "--mlock", \
     "--no-ui", \
     "--host", "0.0.0.0", "--port", "8080"]
