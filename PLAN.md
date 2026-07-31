# ARM64 Media/HomeLab Containers — Build Plan

> **Status:** Ready for implementation
> **Last updated:** July 25 2026

---

## 1. Project Overview

Build and publish ARM64-only Docker containers to GitHub Container Registry (GHCR) for Media/HomeLab use. Single GitHub repository builds all containers.

**Containers:**

| # | Container | Upstream | Purpose |
|---|-----------|----------|---------|
| 1 | `llama-cpp-embed-nomic` | [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) | nomic-embed-text embedding server |
| 2 | `ncnn` | [Tencent/ncnn](https://github.com/Tencent/ncnn) | Neural network inference framework |

**Platform:** Linux ARM64 only (Cortex-A78C class)
**Registry:** `ghcr.io/<owner>/` (public)
**CI/CD:** GitHub Actions
**Container runtime:** Podman
**Update strategy:** Webhook-based (`repository_dispatch` or `workflow_dispatch`) triggered by upstream releases

---

## 2. Repository Structure

```
arm64devcontainer/
├── PLAN.md                              # This file
├── README.md                            # Container catalog (auto-updated by CI)
├── AGENTS.md                            # AI assistant context
├── .gitignore
├── .github/
│   └── workflows/
│       ├── release-monitor.yml           # Daily check for upstream releases
│       ├── llama-cpp-embed-nomic.yml     # Build & push on new release
│       └── ncnn.yml                      # (future)
├── llama-cpp/
│   └── Dockerfile                        # Multi-stage build + model download
└── ncnn/
    └── Dockerfile                        # (future)
```

---

## 3. Container: llama-cpp-embed-nomic

### 3.1 Upstream Details

**Repository:** https://github.com/ggml-org/llama.cpp
**Upstream release tag:** `b5570` (Jun 14 2026) — **only release tags, never master/main/dev branches**
**Compiler:** Clang 21.1.8 (C and C++) — **mandatory, not GCC** (18% better PP on Cortex-A78C)

### 3.2 Model

**Model:** [`nomic-ai/nomic-embed-text-v1.5-GGUF`](https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF)
**Variant:** `f16` (262 MB, 768-dim output, 137M params)
**URL:** `https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.f16.gguf`

Full precision chosen over quantized — embedding cosine-similarity quality is more sensitive to quantization error than chat generation, and at this model size the RAM/disk cost difference is negligible.

**Usage convention (asymmetric retrieval):** Prefix documents with `search_document: ` and queries with `search_query: ` — baked into how the model was trained. Matters for retrieval quality.

### 3.3 Binaries produced

| Binary | Purpose |
|--------|---------|
| `llama-server` | OpenAI-compatible API server (no web UI) |
| `llama-bench` | Benchmarking |
| `llama-cli` | Interactive CLI |
| `llama-mtmd-cli` | Multimodal / VLM inference CLI |
| `llama-quantize` | Model quantization |

**Not built:** tests, examples. Embedded web UI is compiled into `libllama-server-impl.so` (always present) — disabled at runtime via `--no-ui`.

### 3.4 CMake flags (exact)

```cmake
CMAKE_BUILD_TYPE=Release
CMAKE_C_COMPILER=clang
CMAKE_CXX_COMPILER=clang++

# ARM64 Cortex-A78C optimised flags
CMAKE_CXX_FLAGS=-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c \
  -fno-math-errno -fassociative-math -fno-signed-zeros \
  -fno-trapping-math -freciprocal-math -fno-plt -flto=thin
CMAKE_C_FLAGS=<same as CXX — required for GGML C kernel files>
CMAKE_EXE_LINKER_FLAGS=-flto=thin
CMAKE_SHARED_LINKER_FLAGS=-flto=thin

GGML_NATIVE=OFF
GGML_BLAS=OFF
LLAMA_BUILD_TESTS=OFF
BUILD_TESTING=OFF
LLAMA_TESTS_INSTALL=OFF
LLAMA_BUILD_EXAMPLES=OFF
LLAMA_BUILD_UI=OFF
LLAMA_USE_PREBUILT_UI=OFF
LLAMA_BUILD_SERVER=ON
LLAMA_BUILD_TOOLS=ON
```

**Critical notes:**
- **Both `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS` must be set.** Omitting `CMAKE_C_FLAGS` causes ~5× PP regression (GGML C kernel files lose dotprod/fp16 extensions).
- **ThinLTO (`-flto=thin`)** is production standard. Full LTO (`-flto`) gives same perf but ~3× slower compile.
- **Clang is mandatory.** GCC gives ~18% lower PP (SIMD count: 2349 vs 3058+). Root cause: Clang produces better vectorized NEON code for GGML's C kernel files on Cortex-A78C.

### 3.5 Flags tested and rejected

| Flag / approach | Why rejected |
|---|---|
| `GGML_NATIVE=ON` | Overrides cortex-a78c tune with generic native. Slower. |
| `GGML_CPU_KLEIDIAI=ON` | Replaces GGML NEON kernels. TG −12.8% on Gemma E2B. |
| `GGML_BLAS=ON (OpenBLAS)` | PP −12–24%, TG −7–10%. |
| `GGML_VULKAN=ON` | PP ~9× slower than CPU on Adreno 690. |
| GCC | ~18% lower PP than Clang. |
| PGO (Clang or GCC) | Zero gain on Clang; −6% PP on GCC. |
| Full LTO | Same perf as ThinLTO, 3× slower compile. |

### 3.6 Dockerfile

**File:** `llama-cpp/Dockerfile`

Three-stage build: compile llama.cpp → download model → slim runtime.

```dockerfile
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
```

**Build-time configuration:**

```bash
# Build with default release tag
podman build -t llama-cpp-embed-nomic .

# Build with specific release tag
podman build --build-arg LLAMA_CPP_TAG=b5570 -t llama-cpp-embed-nomic .

# Build with different model
podman build --build-arg MODEL_URL=https://huggingface.co/.../other-model.gguf -t llama-cpp-other .
```

### 3.7 Runtime Parameters

#### Key flags

| Flag | Purpose | Default (embedding server) |
|------|---------|---------------------------|
| `-m` | Model path (GGUF) | `/llm/models/model.gguf` |
| `-c` | Context size | `8192` |
| `-t` | Threads | `4` |
| `-tb` | Threads batch | `4` |
| `-b` | Batch size | `8192` |
| `-ub` | Ubatch size | `8192` |
| `--host` | Bind address | `0.0.0.0` |
| `--port` | Listen port | `8080` |
| `--no-ui` | Disable embedded web UI | enabled |
| `--flash-attn` | Flash attention | `on` |
| `--no-mmap` | Disable memory mapping | enabled |
| `--mlock` | Lock model in RAM | enabled |
| `--cpu-range` | Pin to CPU cores | (not set by default; use `0-3` for embedding) |
| `--embedding` | Embedding mode | enabled |
| `--alias` | Model alias for API | `nomic-embed-text` |
| `--override-kv` | Override model KV metadata | `nomic-bert.context_length=int:8192` |
| `--pooling` | Embedding pooling | `mean` |
| `--embd-normalize` | Normalize embeddings | `2` |
| `--rope-scaling` | RoPE scaling method | `yarn` |
| `--rope-freq-scale` | RoPE frequency scale | `.75` |

#### Run examples

```bash
# Default (nomic-embed-text on port 8080)
podman run -d -p 8080:8080 llama-cpp-embed-nomic

# Custom port
podman run -d -p 10004:10004 llama-cpp-embed-nomic --port 10004

# Override context size
podman run -d -p 8080:8080 llama-cpp-embed-nomic -c 4096

# With hugepages (host must have hugepages configured)
podman run -d -p 8080:8080 --ulimit memlock=-1:-1 llama-cpp-embed-nomic
```

### 3.8 Host-side runtime optimisations

These are configured on the host before starting the container:

- **CPU governor:** `echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
- **Thread pinning (embedding):** `taskset -c 0-3` for cpu0-3 cluster
- **Hugepages:** `echo 4096 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages` (~8 GB)
- **MEMLOCK:** `--ulimit memlock=-1:-1` on `podman run`

### 3.9 Performance Baseline

| Metric | Value | Config |
|--------|-------|--------|
| Embedding output | 768-dim float | nomic-embed-text-v1.5 f16 |
| Model size | 262 MB | f16 variant |
| Context length | 8192 tokens | with yarn rope scaling |

> Chat performance baselines (for reference only, not this container):
> Gemma 4 E2B Q4_K_M — PP ~138 t/s (8T), TG ~18.6 t/s (3T). Last verified b9627, Jun 14 2026.

---

## 4. Container: ncnn

> **Status:** Awaiting upstream build instructions from user.

### 4.1 Upstream Details

**Repository:** https://github.com/Tencent/ncnn

### 4.2 Placeholders

- Upstream commit/tag: TBD
- Build flags: TBD
- Required packages: TBD
- Binaries produced: TBD
- Dockerfile: TBD

---

## 5. GitHub Actions Workflows

### 5.1 Runner Strategy

GitHub provides **native ARM64 runners** — free for public repos:

| Label | CPU | RAM | Arch |
|-------|-----|-----|------|
| `ubuntu-24.04-arm` | 4 | 16 GB | arm64 |
| `ubuntu-22.04-arm` | 4 | 16 GB | arm64 |

**Used:** `ubuntu-24.04-arm` — native ARM64 build, no QEMU needed.

### 5.2 Build Caching

- Use `docker/build-push-action` with `cache-from` / `cache-to` pointing to GitHub Actions cache or GHCR-based cache.
- Multi-stage builder cache avoids recompiling llama.cpp from scratch.

### 5.3 Tagging Strategy

| Tag | When |
|-----|------|
| `latest` | Most recent successful build |
| `<release-tag>` | Upstream release tag (e.g. `b5570`) |

### 5.4 Workflow — `release-monitor.yml`

**File:** `.github/workflows/release-monitor.yml`

**Purpose:** Runs daily, checks upstream GitHub releases. If a new release tag is found that hasn't been built yet, triggers the build workflow.

**Policy:** Only release tags (`b*` format) are built. Never master, main, or dev branches.

```yaml
name: Check upstream releases

on:
  schedule:
    - cron: '0 6 * * *'  # Daily at 06:00 UTC
  workflow_dispatch:       # Manual trigger for testing

env:
  LLAMA_CPP_REPO: ggml-org/llama.cpp

permissions:
  contents: read

jobs:
  check-llama-cpp:
    runs-on: ubuntu-24.04-arm  # Native ARM64
    steps:
      - name: Get latest upstream release tag
        id: upstream
        run: |
          TAG=$(curl -sf https://api.github.com/repos/${{ env.LLAMA_CPP_REPO }}/releases/latest | jq -r '.tag_name')
          if [ -z "$TAG" ] || [ "$TAG" = "null" ]; then
            echo "ERROR: Could not fetch latest release tag"
            exit 1
          fi
          echo "tag=$TAG" >> "$GITHUB_OUTPUT"
          echo "Latest upstream release: $TAG"

      - name: Get last built tag from GHCR
        id: builtin
        run: |
          # Check existing image tags in GHCR
          TAGS=$(curl -sf "https://ghcr.io/v2/${{ env.LLAMA_CPP_REPO }}/llama-cpp-embed-nomic/tags/list" 2>/dev/null || echo "")
          # Extract the release tag (b*) from the list
          BUILT=$(echo "$TAGS" | grep -oP 'b[0-9]+' | sort -V | tail -1)
          echo "tag=${BUILT:-none}" >> "$GITHUB_OUTPUT"
          echo "Last built tag: ${BUILT:-none}"

      - name: Trigger build if new release
        if: steps.upstream.outputs.tag != steps.builtin.outputs.tag
        run: |
          echo "New release detected: ${{ steps.upstream.outputs.tag }} (built: ${{ steps.builtin.outputs.tag }})"
          curl -sf -X POST \
            -H "Authorization: token ${{ secrets.GITHUB_TOKEN }}" \
            -H "Accept: application/vnd.github.v3+json" \
            https://api.github.com/repos/${{ github.repository }}/dispatches \
            -d "{\"event_type\":\"llama-cpp-release\",\"client_payload\":{\"tag\":\"${{ steps.upstream.outputs.tag }}\"}}"

      - name: No new release
        if: steps.upstream.outputs.tag == steps.builtin.outputs.tag
        run: echo "Already up to date: ${{ steps.upstream.outputs.tag }}"
```

### 5.5 Workflow — `llama-cpp-embed-nomic.yml`

**File:** `.github/workflows/llama-cpp-embed-nomic.yml`

**Triggers:**
- `repository_dispatch` — from release-monitor (event type `llama-cpp-release`)
- `workflow_dispatch` — manual trigger with release tag input

**Pipeline:** Build → Smoke test → Push → Update README → Commit

```yaml
name: Build llama-cpp-embed-nomic

on:
  repository_dispatch:
    types: [llama-cpp-release]

  workflow_dispatch:
    inputs:
      tag:
        description: 'llama.cpp release tag (e.g. b5570)'
        required: true
        type: string

env:
  REGISTRY: ghcr.io
  IMAGE_NAME: ${{ github.repository_owner }}/llama-cpp-embed-nomic

permissions:
  contents: write
  packages: write

jobs:
  build-test-push:
    runs-on: ubuntu-24.04-arm  # Native ARM64 — no QEMU needed
    steps:
      - name: Resolve tag
        id: tag
        run: |
          TAG="${{ github.event.client_payload.tag || github.event.inputs.tag }}"
          if [ -z "$TAG" ]; then
            echo "ERROR: No tag provided"
            exit 1
          fi
          if [[ ! "$TAG" =~ ^b[0-9]+$ ]]; then
            echo "ERROR: Tag '$TAG' is not a valid release tag (must match b* pattern)"
            exit 1
          fi
          echo "tag=$TAG" >> "$GITHUB_OUTPUT"
          echo "Building tag: $TAG"

      - uses: actions/checkout@v4

      - name: Log in to GHCR
        uses: docker/login-action@v3
        with:
          registry: ${{ env.REGISTRY }}
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      - name: Extract metadata
        id: meta
        uses: docker/metadata-action@v5
        with:
          images: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}
          tags: |
            type=raw,value=${{ steps.tag.outputs.tag }}
            type=raw,value=latest

      - name: Build
        uses: docker/build-push-action@v6
        with:
          context: ./llama-cpp
          push: false
          load: true
          tags: ${{ env.IMAGE_NAME }}:test
          build-args: |
            LLAMA_CPP_TAG=${{ steps.tag.outputs.tag }}

      - name: Smoke test
        run: |
          sudo apt-get update && sudo apt-get install -y jq
          podman run -d --name test -p 8080:8080 ${{ env.IMAGE_NAME }}:test
          echo "Waiting for server to start..."
          sleep 30
          
          # Test 1: Health endpoint
          echo "Test 1: Health check..."
          curl -sf http://localhost:8080/health || (podman logs test; exit 1)
          
          # Test 2: Model info
          echo "Test 2: Model info..."
          curl -sf http://localhost:8080/v1/models | grep -q "nomic-embed-text" || (podman logs test; exit 1)
          
          # Test 3: Embedding generation
          echo "Test 3: Embedding generation..."
          RESPONSE=$(curl -sf http://localhost:8080/v1/embeddings \
            -d '{"input":"hello world","model":"nomic-embed-text"}')
          echo "$RESPONSE" | grep -q '"embedding"' || (podman logs test; exit 1)
          
          # Test 4: Embedding dimensions (768)
          echo "Test 4: Embedding dimensions..."
          DIMS=$(echo "$RESPONSE" | jq '.data[0].embedding | length')
          [ "$DIMS" -eq 768 ] || (echo "Expected 768 dims, got $DIMS"; podman logs test; exit 1)
          
          echo "All tests passed!"
          podman stop test && podman rm test

      - name: Push
        uses: docker/build-push-action@v6
        with:
          context: ./llama-cpp
          push: true
          tags: ${{ steps.meta.outputs.tags }}
          labels: ${{ steps.meta.outputs.labels }}
          build-args: |
            LLAMA_CPP_TAG=${{ steps.tag.outputs.tag }}

      - name: Update README with new tag
        run: |
          TAG="${{ steps.tag.outputs.tag }}"
          sed -i "/llama-cpp-embed-nomic/s|\`[a-zA-Z0-9._-]*\`, \`latest\`|\`${TAG}\`, \`latest\`|" README.md
          sed -i "s|llama-cpp-embed-nomic:[a-zA-Z0-9._-]*|llama-cpp-embed-nomic:${TAG}|g" README.md

      - name: Commit README update
        run: |
          git config user.name "github-actions[bot]"
          git config user.email "github-actions[bot]@users.noreply.github.com"
          git add README.md
          if ! git diff --cached --quiet; then
            git commit -m "docs: update README for llama-cpp-embed-nomic:${{ steps.tag.outputs.tag }}"
            git push origin main
          fi
```

### 5.6 Workflow — `ncnn.yml` (future)

> Same pattern as llama-cpp-embed-nomic: release-monitor triggers build workflow with upstream tag.

---

## 6. GHCR Configuration

### Image naming

```
ghcr.io/<owner>/llama-cpp-embed-nomic
ghcr.io/<owner>/ncnn                   (future)
```

### Visibility

Public — no `GITHUB_TOKEN` needed for pulls.

### Package settings (via GitHub UI)

- Set description and README per package
- Add labels: `platform: linux/arm64`, `use-case: medialab`

---

## 7. Update Strategy

### Automatic (primary)

A daily cron workflow (`release-monitor.yml`) checks upstream GitHub releases via the API. When a new release tag is detected (comparing against tags already pushed to GHCR), it triggers the build workflow via `repository_dispatch`.

**Flow:**
```
release-monitor.yml (daily cron)
  → checks api.github.com/repos/ggml-org/llama.cpp/releases/latest
  → compares tag against GHCR image tags
  → if new: triggers llama-cpp-embed-nomic.yml via repository_dispatch

llama-cpp-embed-nomic.yml:
  → builds image with release tag
  → smoke tests (health, model, embedding, dimensions)
  → pushes to GHCR (:bNNNN + :latest)
  → updates README.md with new tag
  → commits and pushes README to main
```

### Manual (fallback)

Trigger `llama-cpp-embed-nomic.yml` via `workflow_dispatch` with a specific release tag (e.g. `b5570`) for ad-hoc builds.

### Policy

**Only release tags** (`b*` format like `b5570`) are built. Never master, main, or dev branches. This is enforced by tag format validation in the build workflow.

### Tag Policy

| Tag | Behavior |
|-----|----------|
| `:b5570` | Points to latest build for that release (overwrites on rebuild) |
| `:latest` | Always points to the most recent build across all releases |

- **Same-release rebuilds** (Dockerfile fix, flag change, model update): Tag is overwritten in-place. Previous image is lost.
- **No weekly/scheduled rebuilds** — only triggered by new upstream releases.
- **No revision suffixes** (`-1`, `-2`) — keeps tags clean and predictable.

---

## 8. Open Questions / TODO

- [ ] **ncnn build instructions** — user to provide (commit, cmake flags, binaries, special deps)

---

## 9. Implementation Order

1. **Create repo structure** — directories, `.gitignore`
2. **llama-cpp/Dockerfile** — multi-stage build with model download
3. **release-monitor.yml** — daily cron checking upstream releases
4. **llama-cpp-embed-nomic.yml** — build workflow triggered by release monitor
5. **ncnn/Dockerfile** — when build instructions provided
6. **ncnn.yml** — same pattern as llama-cpp
7. **GHCR setup** — visibility, descriptions, labels

---

## 10. Container: hometimeline-base

### 10.1 Upstream Details

**Repository:** https://github.com/FFmpeg/FFmpeg
**Upstream release tag:** `n8.1.2` (Jun 17 2026) — tags only, no GitHub Releases
**Compiler:** Clang 21+ (C and C++) — mandatory, matches other containers
**Base OS:** Ubuntu 26.04 LTS

### 10.2 Purpose

Optimised FFmpeg + ffprobe base image for the Snapdragon 8cx Gen 3 (SC8280XP). Provides a foundation for downstream video processing containers (`hometimeline-video`, `hometimeline-detect`, `hometimeline-api`) via `COPY --from`.

### 10.3 Binaries produced

| Binary | Purpose |
|--------|---------|
| `ffmpeg` | Media transcoding, filtering, streaming |
| `ffprobe` | Media analysis and inspection |

### 10.4 Video Codecs

| Codec | Library | Enabled |
|-------|---------|---------|
| H.264/AVC | libx264 | Yes |
| H.265/HEVC | libx265 | Yes |
| VP8/VP9 | libvpx | Yes |
| AV1 | libsvtav1 | Yes (built from source) |
| MJPEG | libjpeg-turbo | Yes |

### 10.5 Audio Codecs

| Codec | Library | Enabled |
|-------|---------|---------|
| AAC | libfdk-aac | Yes |
| Opus | libopus | Yes |

### 10.6 Protocols

| Protocol | Enabled |
|----------|---------|
| file | Yes |
| pipe | Yes |
| tcp | Yes |
| rtsp | Yes |
| rtmp | Yes |
| hls | Yes |
| https | Yes |

### 10.7 CMake / Configure flags (exact)

**SVT-AV1 (built from source):**
```cmake
CMAKE_BUILD_TYPE=Release
CMAKE_C_COMPILER=clang
CMAKE_CXX_COMPILER=clang++
CMAKE_C_FLAGS=<same as CXX>
CMAKE_CXX_FLAGS=<same as CXX>
CMAKE_EXE_LINKER_FLAGS=-flto=thin
BUILD_SHARED_LIBS=ON
BUILD_APPS=OFF
BUILD_DEC=ON
```

**SVT-AV1 source:** https://gitlab.com/AOMediaCodec/SVT-AV1.git (not GitHub — GitHub mirror has no tags)
**SVT-AV1 default version:** v4.2.0

**FFmpeg:**
```
./configure \
  --prefix=/usr/local \
  --cc=clang --cxx=clang++ \
  --enable-gpl --enable-nonfree --enable-version3 \
  --enable-libx264 --enable-libx265 --enable-libvpx \
  --enable-libsvtav1 --enable-libfdk-aac --enable-libopus \
  --enable-vulkan \
  --enable-openssl \
  --enable-protocol=file --enable-protocol=pipe \
  --enable-protocol=tcp --enable-protocol=rtsp \
  --enable-protocol=rtmp --enable-protocol=hls \
  --enable-protocol=https \
  --enable-shared --disable-static \
  --disable-doc \
  --extra-cflags="${CFLAGS}" \
  --extra-cxxflags="${CFLAGS}" \
  --extra-ldflags="-flto=thin"
```

### 10.8 CFLAGS (exact)

```
-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c
-fno-math-errno -fassociative-math -fno-signed-zeros
-fno-trapping-math -freciprocal-math -fno-plt -flto=thin
```

**Critical notes:**
- **Both `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS` must be set** for SVT-AV1 build.
- **`--extra-cflags` and `--extra-cxxflags` must be passed** to FFmpeg's `./configure` for the same reason.
- **ThinLTO (`-flto=thin`)** is production standard across all containers.
- **Clang is mandatory** — matches llama-cpp and yolo-rest containers.

### 10.9 Dockerfile

**File:** `hometimeline-base/Dockerfile`

Four-stage build: install deps → build SVT-AV1 → build FFmpeg → slim runtime.

**Build-time configuration:**
```bash
# Build with default version
docker build -t hometimeline-base:local ./hometimeline-base

# Build with specific FFmpeg version
docker build --build-arg FFMPEG_VERSION=8.1.2 -t hometimeline-base:local ./hometimeline-base

# Build with specific SVT-AV1 version
docker build --build-arg SVTAV1_VERSION=3.0.2 -t hometimeline-base:local ./hometimeline-base
```

### 10.10 Usage pattern (downstream COPY --from)

```dockerfile
FROM ghcr.io/dk307/hometimeline-base:latest AS ffmpeg

FROM ubuntu:26.04 AS app-base
# ... install your deps ...

FROM app-base AS final
COPY --from=ffmpeg /usr/local/bin/ffmpeg /usr/local/bin/ffmpeg
COPY --from=ffmpeg /usr/local/bin/ffprobe /usr/local/bin/ffprobe
COPY --from=ffmpeg /usr/local/lib/ /usr/local/lib/
ENV LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH}
```

### 10.11 Tag Policy

| Tag | Behavior |
|-----|----------|
| `:ffmpeg-8.1.2` | Pinned FFmpeg version (overwrites on rebuild) |
| `:latest` | Most recent build |

### 10.12 CI/CD

**Workflow:** `.github/workflows/hometimeline-base.yml`
**Triggers:** Weekly cron (Monday 02:00 UTC) + push to Dockerfile + manual dispatch
**Runner:** `ubuntu-24.04-arm` (native ARM64)

**Note:** FFmpeg has no GitHub Releases — only tags (`n*` format). The weekly cron fetches the latest `n*` tag from the GitHub API, compares against GHCR tags, and builds if new.

**Smoke tests:** ffmpeg binary, ffprobe binary, required codecs, required protocols, ARM64 build config, GPL/libx264 verification.
