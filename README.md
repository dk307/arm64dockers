# ARM64 Media/HomeLab Containers

ARM64-only Docker containers built and published to GHCR. Single repo, automated CI/CD.

**Platform:** Linux ARM64 (Cortex-A78C)
**Registry:** [ghcr.io/dk307](https://ghcr.io/dk307) (public)

---

## Containers

| Container | Upstream | Description | Tags |
|-----------|----------|-------------|------|
| [`llama-cpp-embed-nomic`](#llama-cpp-embed-nomic) | [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) | nomic-embed-text embedding server | `b10369`, `latest` |
| [`yolo-rest`](#yolo-rest) | [Tencent/ncnn](https://github.com/Tencent/ncnn) | YOLO object-detection REST server | `ncnn-20260526`, `latest` |
| [`hometimeline-base`](#hometimeline-base) | [FFmpeg/FFmpeg](https://github.com/FFmpeg/FFmpeg) | FFmpeg optimised for ARM64 Cortex-A78C | `ffmpeg-8.1.2`, `latest` |
| [`hometimeline-custom`](#hometimeline-custom) | [dk307/HomeTimeline](https://github.com/dk307/HomeTimeline) | HomeTimeline with optimised FFmpeg | `v0.12.5`, `latest` |

---

### llama-cpp-embed-nomic

Embedding server running [nomic-embed-text v1.5](https://huggingface.co/nomic-ai/nomic-embed-text-v1.5) (f16, 137M params, 768-dim) via llama.cpp. Built with Clang 21 and Cortex-A78C tuned flags.

**Pull:**
```bash
# Latest upstream release
docker pull ghcr.io/dk307/llama-cpp-embed-nomic:latest

# Pinned to specific release
docker pull ghcr.io/dk307/llama-cpp-embed-nomic:b10369
```

**Run:**
```bash
# Docker (with log rotation)
docker run -d \
  --name embed \
  -p 8080:8080 \
  --ulimit memlock=-1:-1 \
  --log-opt max-size=10m \
  --log-opt max-file=3 \
  ghcr.io/dk307/llama-cpp-embed-nomic:latest

# Podman (with auto-restart on unhealthy)
podman run -d \
  --name embed \
  -p 8080:8080 \
  --ulimit memlock=-1:-1 \
  --health-cmd "curl -sf http://localhost:8080/health || exit 1" \
  --health-interval 30s \
  --health-timeout 5s \
  --health-retries 3 \
  --health-start-period 30s \
  --health-on-failure restart \
  ghcr.io/dk307/llama-cpp-embed-nomic:latest
```

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check (returns `{"status":"ok"}`) |
| `/v1/models` | GET | Model metadata |
| `/v1/embeddings` | POST | Generate embeddings |

**Usage example:**
```bash
# Health check
curl http://localhost:8080/health

# Generate embedding
curl -s http://localhost:8080/v1/embeddings \
  -H "Content-Type: application/json" \
  -d '{"input": "hello world", "model": "nomic-embed-text"}'
```

**Asymmetric retrieval:** Prefix documents with `search_document:`, queries with `search_query:` for best retrieval quality.

**Healthcheck:** The image includes a Dockerfile `HEALTHCHECK` (30s interval, 5s timeout, 30s start period, 3 retries) that tests the `/health` endpoint. Check status with:
```bash
docker inspect --format='{{.State.Health.Status}}' embed
```

**Log rotation:** Container logs (stderr) grow unbounded by default. Add log driver options:
```bash
# Per container
docker run -d ... --log-opt max-size=10m --log-opt max-file=3 ...

# Global default (/etc/docker/daemon.json)
{
  "log-driver": "json-file",
  "log-opts": { "max-size": "10m", "max-file": "3" }
}
```

---

### yolo-rest

YOLO object-detection REST server using ncnn. Bundles **yolo26n** (fast, ~40.9 mAP) and **yolo26m** (accurate, ~53.1 mAP) with FP16 inference. Built with Clang 21 and Cortex-A78C tuned flags. No Python at runtime.

**Pull:**
```bash
# Latest ncnn release
docker pull ghcr.io/dk307/yolo-rest:latest

# Pinned to specific ncnn version
docker pull ghcr.io/dk307/yolo-rest:ncnn-20260526
```

**Run:**
```bash
# Docker (with log rotation) — default model is yolo26m
docker run -d \
  --name yolo \
  -p 18080:18080 \
  --log-opt max-size=10m \
  --log-opt max-file=3 \
  ghcr.io/dk307/yolo-rest:latest

# Docker — use yolo26n (fast)
docker run -d \
  --name yolo-fast \
  -p 18080:18080 \
  --log-opt max-size=10m \
  --log-opt max-file=3 \
  ghcr.io/dk307/yolo-rest:latest \
  --model_type yolo26n

# Podman (with auto-restart on unhealthy)
podman run -d \
  --name yolo \
  -p 18080:18080 \
  --health-cmd "curl -sf http://localhost:18080/health || exit 1" \
  --health-interval 30s \
  --health-timeout 5s \
  --health-retries 3 \
  --health-start-period 10s \
  --health-on-failure restart \
  ghcr.io/dk307/yolo-rest:latest
```

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check, default model, available models list |
| `/models` | GET | List bundled models with param paths |
| `/detect` | POST | Object detection on image(s) |

**POST /detect query parameters:**
| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `model_type` | string | `yolo26m` | Bundled model: `yolo26n` or `yolo26m` |
| `conf` | float | `0.25` | Confidence threshold |
| `classes` | string | all | Comma-separated COCO class names or IDs |

**Usage examples:**
```bash
# Health
curl http://localhost:18080/health

# List models
curl http://localhost:18080/models

# Detect (default model: yolo26m)
curl -X POST http://localhost:18080/detect \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @image.jpg

# Detect with yolo26n (fast), only people, conf >= 0.4
curl -X POST 'http://localhost:18080/detect?model_type=yolo26n&classes=person&conf=0.4' \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @image.jpg

# Batch (multipart)
curl -F f1=@frame1.jpg -F f2=@frame2.jpg \
  'http://localhost:18080/detect?model_type=yolo26n'
```

**Healthcheck:** The image includes a Dockerfile `HEALTHCHECK` (30s interval, 5s timeout, 10s start period, 3 retries) that tests the `/health` endpoint. Check status with:
```bash
docker inspect --format='{{.State.Health.Status}}' yolo
```

---

### hometimeline-base

FFmpeg optimised for the Snapdragon 8cx Gen 3 (Cortex-A78C). Built from source with Clang and ARM64-tuned flags. Includes libx264, libx265, libvpx, libsvtav1 (AV1), libfdk-aac, libopus, libjpeg-turbo, and Vulkan (dlopen'd at runtime).

**No EXPOSE, no ENTRYPOINT** — this is a base image for `COPY --from` in downstream containers.

**Pull:**
```bash
# Latest FFmpeg release
docker pull ghcr.io/dk307/hometimeline-base:latest

# Pinned to specific FFmpeg version
docker pull ghcr.io/dk307/hometimeline-base:ffmpeg-8.1.2
```

**Test:**
```bash
# Verify ffmpeg
docker run --rm ghcr.io/dk307/hometimeline-base:latest ffmpeg -version

# Verify ffprobe
docker run --rm ghcr.io/dk307/hometimeline-base:latest ffprobe -version

# List available codecs
docker run --rm ghcr.io/dk307/hometimeline-base:latest ffmpeg -codecs 2>/dev/null | grep -E "libx264|libx265|libvpx|libsvtav1|libfdk_aac|mjpeg"
```

**Use in downstream Dockerfile:**
```dockerfile
FROM ghcr.io/dk307/hometimeline-base:latest AS ffmpeg

FROM debian:bookworm AS final
COPY --from=ffmpeg /usr/local/bin/ffmpeg /usr/local/bin/ffmpeg
COPY --from=ffmpeg /usr/local/bin/ffprobe /usr/local/bin/ffprobe
COPY --from=ffmpeg /usr/local/lib/ /usr/local/lib/
COPY --from=ffmpeg /usr/lib/aarch64-linux-gnu/ /usr/lib/aarch64-linux-gnu/
```

**Codecs:**
| Type | Codec | Library |
|------|-------|---------|
| Video | H.264/AVC | libx264 |
| Video | H.265/HEVC | libx265 |
| Video | VP8/VP9 | libvpx |
| Video | AV1 | libsvtav1 (built from source) |
| Video | MJPEG | libjpeg-turbo |
| Audio | AAC | libfdk-aac |
| Audio | Opus | libopus |

**Protocols:** file, pipe, tcp, rtsp, rtmp, hls, https

---

### hometimeline-custom

[HomeTimeline](https://github.com/dk307/HomeTimeline) — a Python 3.14 + FastAPI + React 18 timeline app with live WebRTC camera view — built with optimised FFmpeg from `hometimeline-base`. Includes go2rtc for WebRTC.

**Pull:**
```bash
# Latest release
docker pull ghcr.io/dk307/hometimeline-custom:latest

# Pinned to specific release
docker pull ghcr.io/dk307/hometimeline-custom:v0.12.9
```

**Run:**
```bash
docker run -d \
  --name hometimeline \
  -p 8080:8080 \
  -p 8555:8555 \
  -e DATABASE_URL="sqlite:///data/timeline.db" \
  -e RECORDING_LOCATIONS="[]"
  ghcr.io/dk307/hometimeline-custom:latest
```

**Ports:**
| Port | Service |
|------|---------|
| 8080 | FastAPI backend |
| 8555 | go2rtc WebRTC |

**Environment variables:**
| Variable | Default | Description |
|----------|---------|-------------|
| `DATABASE_URL` | `sqlite:///data/timeline.db` | Database connection string |
| `RECORDING_LOCATIONS` | `[]` | JSON array of recording paths |
| `THUMBNAIL_DIR` | `/data/thumbnails` | Thumbnail storage path |
| `LOG_LEVEL` | `INFO` | Python log level |
| `GO2RTC_ENABLED` | `true` | Enable go2rtc WebRTC view |

---

## How Updates Work

Upstream releases are monitored. When a new release is detected:

**llama-cpp-embed-nomic:** `release-monitor.yml` checks [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) daily → triggers `llama-cpp-embed-nomic.yml` → build, test, push.

**yolo-rest:** `ncnn-release-monitor.yml` checks [Tencent/ncnn](https://github.com/Tencent/ncnn) daily → triggers `yolo-rest.yml` → build, test, push.

**hometimeline-base:** `hometimeline-base.yml` checks [FFmpeg/FFmpeg](https://github.com/FFmpeg/FFmpeg) weekly (Monday 02:00 UTC) → build, test, push. FFmpeg has no GitHub Releases — only tags (`n*` format).

**hometimeline-custom:** `hometimeline-release-monitor.yml` checks [dk307/HomeTimeline](https://github.com/dk307/HomeTimeline) daily → triggers `hometimeline-custom.yml` → build, test, push. Also auto-rebuilds when `hometimeline-base` is updated.

1. Monitor workflow compares upstream tag against published GHCR tags
2. If new, triggers build workflow (via `repository_dispatch` or direct schedule)
3. Build compiles from source with Clang and ARM64-tuned flags
4. Smoke tests verify binaries, codecs, and protocols
5. Image pushed to GHCR with version tag and `:latest`

**Manual trigger:**
```bash
# llama-cpp
gh workflow run llama-cpp-embed-nomic.yml -f tag=<release-tag>

# yolo-rest
gh workflow run yolo-rest.yml -f ncnn_tag=20260526

# hometimeline-base
gh workflow run hometimeline-base.yml -f ffmpeg_version=8.1.2

# hometimeline-custom
gh workflow run hometimeline-custom.yml -f hometimeline_tag=v0.12.5
```

---

## Building Locally

```bash
# llama-cpp — requires the upstream release tag as build arg
docker build \
  --build-arg LLAMA_CPP_TAG:b10199 \
  -t llama-cpp-embed-nomic:local \
  ./llama-cpp

# yolo-rest — requires ncnn release tag
docker build \
  --build-arg NCNN_TAG=20260526 \
  --build-arg MODELS_TAG=yolo-models-v1 \
  -t yolo-rest:local \
  ./yolo-rest

# hometimeline-base — requires FFmpeg version as build arg
docker build \
  --build-arg FFMPEG_VERSION=8.1.2 \
  -t hometimeline-base:local \
  ./hometimeline-base

# hometimeline-custom — clones upstream Dockerfile, needs buildx + build-context
# (see .github/workflows/hometimeline-custom.yml for exact command)
```
