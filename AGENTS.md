# AGENTS.md — AI Assistant Context

> This file provides full project context to AI assistants working in this repository.
> Read this before making any changes.

---

## Project Summary

**arm64dockers** — ARM64-only Docker containers for Media/HomeLab use, built and published to GHCR (GitHub Container Registry). Single GitHub repository builds all containers.

- **Platform:** Linux ARM64 only (Cortex-A78C class)
- **Registry:** `ghcr.io/dk307/` (public, no auth needed for pulls)
- **CI/CD:** GitHub Actions on native ARM64 runners (`ubuntu-24.04-arm`)
- **Container runtime:** Podman (local), Docker (GitHub Actions)
- **Update strategy:** Daily cron checks upstream releases → auto-build → auto-publish → auto-update README

---

## Repository Structure

```
arm64devcontainer/
├── PLAN.md                              # Detailed build plan (source of truth for flags/config)
├── README.md                            # Container catalog (auto-updated by CI)
├── AGENTS.md                            # This file — AI assistant context
├── LICENSE                              # MIT
├── .gitignore
├── .github/
│   └── workflows/
│       ├── release-monitor.yml          # Daily cron: checks llama.cpp releases
│       ├── llama-cpp-embed-nomic.yml    # Build, test, push, update README
│       ├── ncnn-release-monitor.yml     # Daily cron: checks ncnn releases
│       ├── yolo-rest.yml               # Build, test, push, update README
│       ├── hometimeline-base.yml       # Weekly cron: FFmpeg base image
│       ├── hometimeline-custom.yml     # Build HomeTimeline with optimised FFmpeg
│       └── hometimeline-release-monitor.yml  # Daily cron: checks HomeTimeline releases
├── llama-cpp/
│   └── Dockerfile                       # Multi-stage: build → model → runtime
├── yolo-rest/
│   ├── Dockerfile                       # Multi-stage: ncnn build → server → models
│   └── server/
│       ├── server.cpp                   # REST server with model_type API
│       ├── CMakeLists.txt               # Docker-compatible CMake build
│       └── third_party/
│           ├── httplib.h                # cpp-httplib (header-only)
│           └── stb_image.h              # stb image decoder (header-only)
└── hometimeline-base/
    └── Dockerfile                       # Multi-stage: SVT-AV1 → FFmpeg → runtime
```

---

## Containers

### llama-cpp-embed-nomic (active)

**Image:** `ghcr.io/dk307/llama-cpp-embed-nomic`
**Purpose:** nomic-embed-text v1.5 embedding server via llama.cpp
**Model:** `nomic-ai/nomic-embed-text-v1.5-GGUF` f16 variant (262 MB, 768-dim, 137M params)
**Port:** 8080

#### Key build facts

- **Compiler:** Clang 21.1.8 mandatory (18% better PP than GCC on Cortex-A78C)
- **Release tags only** — never build from master/main/dev. Tag format: `bNNNN` (e.g. `b10107`)
- **`ARG LLAMA_CPP_TAG`** — no default, must be passed as build arg. Build fails if omitted.
- **Both `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS` must be set** — omitting CFLAGS causes ~5× PP regression
- **ThinLTO** (`-flto=thin`) — production standard. Full LTO same perf but 3× slower compile.
- **GGML_NATIVE=OFF, GGML_BLAS=OFF**

#### CFLAGS (exact)

```
-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c
-fno-math-errno -fassociative-math -fno-signed-zeros
-fno-trapping-math -freciprocal-math -fno-plt -flto=thin
```

#### Runtime flags (embedding server defaults)

```
--embedding --alias nomic-embed-text
--override-kv nomic-bert.context_length=int:8192
--pooling mean --embd-normalize 2 -c 8192
--rope-scaling yarn --rope-freq-scale .75
-b 8192 -ub 8192 -t 4 -tb 4
--flash-attn on --no-mmap --mlock --no-ui
--host 0.0.0.0 --port 8080
```

#### Embedding usage convention

- Prefix **documents** with `search_document: `
- Prefix **queries** with `search_query: `
- This is how the model was trained; matters for retrieval quality.

#### Binary layout (important!)

llama.cpp puts `.so` files in `build/bin/` alongside executables, **not** in `build/lib*/`.
`LD_LIBRARY_PATH` must include both `/llm/llama-cpp/bin` and `/llm/llama-cpp/lib`.

#### Healthcheck

Dockerfile includes a `HEALTHCHECK` (30s interval, 5s timeout, 30s start period, 3 retries):
```
CMD curl -sf http://localhost:8080/health || exit 1
```

CI smoke test verifies both the `/health` endpoint AND Docker's HEALTHCHECK status via `docker inspect`.

**Podman users:** Use `--health-on-failure restart` for auto-restart on unhealthy containers (requires Podman v4.3+).

#### Flags tested and rejected

| Flag | Why rejected |
|------|-------------|
| GGML_NATIVE=ON | Overrides cortex-a78c tune. Slower. |
| GGML_CPU_KLEIDIAI=ON | TG −12.8% on Gemma E2B |
| GGML_BLAS=ON (OpenBLAS) | PP −12–24%, TG −7–10% |
| GGML_VULKAN=ON | PP ~9× slower than CPU on Adreno 690 |
| GCC | ~18% lower PP than Clang |
| PGO | Zero gain on Clang; −6% PP on GCC |
| Full LTO | Same perf, 3× slower compile |

### yolo-rest (active)

**Image:** `ghcr.io/dk307/yolo-rest`
**Purpose:** YOLO object-detection REST server via ncnn
**Models:** yolo26n (fast, ~40.9 mAP), yolo26m (accurate, ~53.1 mAP) — FP16 bundled
**Port:** 18080

#### Key build facts

- **Compiler:** Clang 21.1.8 (same as llama-cpp)
- **ncnn:** release tag `20260526` — tracked by `ncnn-release-monitor.yml`
- **`NCNN_VULKAN=OFF`** — CPU-only (GPU slower for nano models, has box-decode bugs)
- **`NCNN_SHARED_LIB=OFF`** — static ncnn, self-contained binary
- **`NCNN_ARM82=ON`** — FP16 NEON fast path
- **Models downloaded from GitHub release assets** (`yolo-models-v1` tag) during Docker build
- **Runtime dep:** `libomp5` (LLVM OpenMP)

#### CFLAGS (exact — identical to llama-cpp)

```
-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c
-fno-math-errno -fassociative-math -fno-signed-zeros
-fno-trapping-math -freciprocal-math -fno-plt -flto=thin
```

#### Model type API

The server supports selecting bundled models by name:

```
POST /detect?model_type=yolo26n    → /models/yolo26n_opt.param
POST /detect?model_type=yolo26m    → /models/yolo26m_opt.param (default)
```

Available endpoints:
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Status, default model type, available models |
| `/models` | GET | List bundled models with param paths |
| `/detect` | POST | Object detection (single image or multipart batch) |

#### CLI args

```
yolo_server [--port N] [--model_type yolo26n|yolo26m] [--threads N]
```

Default model: **yolo26m** (accurate).

#### Healthcheck

Dockerfile includes a `HEALTHCHECK` (30s interval, 5s timeout, 10s start period, 3 retries):
```
CMD curl -sf http://localhost:18080/health || exit 1
```

CI smoke tests verify: health endpoint, HEALTHCHECK status, /models listing, detection with yolo26n, detection with yolo26m, and default model behavior.

### hometimeline-base (active)

**Image:** `ghcr.io/dk307/hometimeline-base`
**Purpose:** FFmpeg optimised for Snapdragon 8cx Gen 3 — base layer for downstream video containers
**Codecs:** libx264, libx265, libvpx, libsvtav1, libfdk-aac, libopus, libjpeg-turbo
**GPU:** Vulkan encode/decode (dlopen at runtime)
**No EXPOSE, no ENTRYPOINT** — pure base layer for `COPY --from`

#### Key build facts

- **Compiler:** Clang 21+ (same as other containers)
- **FFmpeg:** Release tag `n8.1.2` — tags only, no GitHub Releases
- **SVT-AV1:** Built from source as **static** (`BUILD_SHARED_LIBS=OFF`), no NUMA — eliminates libSvtAv1Enc.so and libnuma.so runtime deps
- **Base OS:** Debian bookworm (matches python:3.14-slim glibc for upstream compat)
- **Vulkan:** `--enable-vulkan` with Vulkan headers from `vulkan-sdk-1.4.357.0` (bookworm ships 1.3.239, FFmpeg needs ≥1.3.277). dlopen'd at runtime, no link-time dependency.
- **libshaderc:** intentionally omitted — Debian bookworm's libshaderc.so has unresolved glslang/SPIRV-Tools references. Vulkan image filters (nlmeans_vulkan, scale_vulkan, etc.) are not built.
- **Weekly cron** (Monday 02:00 UTC) — FFmpeg releases ~monthly, daily check unnecessary

#### CFLAGS (exact — identical to llama-cpp and yolo-rest)

```
-O3 -march=armv8.2-a+dotprod+fp16+fp16fml+rcpc -mtune=cortex-a78c
-fno-math-errno -fassociative-math -fno-signed-zeros
-fno-trapping-math -freciprocal-math -fno-plt -flto=thin
```

#### Codecs

| Type | Codec | Library |
|------|-------|---------|
| Video | H.264/AVC | libx264 |
| Video | H.265/HEVC | libx265 |
| Video | VP8/VP9 | libvpx |
| Video | AV1 | libsvtav1 (built from source) |
| Video | MJPEG | libjpeg-turbo |
| Audio | AAC | libfdk-aac |
| Audio | Opus | libopus |

#### Protocols

file, pipe, tcp, rtsp, rtmp, hls, https

#### Downstream consumption pattern

```dockerfile
FROM ghcr.io/dk307/hometimeline-base:latest AS ffmpeg
COPY --from=ffmpeg /usr/local/bin/ffmpeg /usr/local/bin/ffmpeg
COPY --from=ffmpeg /usr/local/bin/ffprobe /usr/local/bin/ffprobe
COPY --from=ffmpeg /usr/local/lib/ /usr/local/lib/
```

#### Tag policy

| Container | Tag | Behavior |
|-----------|-----|----------|
| hometimeline-base | `:ffmpeg-NNN.N.N` | Pinned FFmpeg version (overwrites on rebuild) |
| hometimeline-base | `:latest` | Most recent build |

#### Notes

- FFmpeg has **no GitHub Releases** — only tags (`n*` format). Weekly cron fetches latest `n*` tag from API.
- **SVT-AV1** is built from source because it's not in Debian repos.
- **No EXPOSE, no ENTRYPOINT** — this is a base layer, not a runnable service.
- Downstream images use `COPY --from` to extract binaries and shared libraries.

### hometimeline-custom (active)

**Image:** `ghcr.io/dk307/hometimeline-custom`
**Purpose:** HomeTimeline app with optimised FFmpeg from hometimeline-base
**Upstream:** [dk307/HomeTimeline](https://github.com/dk307/HomeTimeline) — Python 3.14 + FastAPI + Peewee + SQLite backend, React 18 + TypeScript + Vite + shadcn/ui frontend
**Ports:** 8080 (FastAPI), 8555 (go2rtc WebRTC)
**Build:** Clones upstream at pinned tag, uses upstream `docker/Dockerfile` with `--target app-custom-ffmpeg`

#### Key build facts

- **No static Dockerfile** — clones HomeTimeline repo and uses upstream's `docker/Dockerfile` directly
- **FFmpeg source:** `--build-context ffmpeg-source=docker-image://ghcr.io/dk307/hometimeline-base:latest`
- **Build target:** `app-custom-ffmpeg` — upstream's custom FFmpeg stage
- **Patches applied via awk:** `libnuma1` install (libx265 transitive dep) before `ldconfig`
- **Build metadata:** `GIT_SHA` and `BUILD_TIME` passed as build-args, exposed via `/api/v1/system_info`
- **Runtime:** go2rtc 1.9.14, Python 3.14, all upstream deps
- **Tags:** `v*.*.*` format (e.g. `v0.12.5`)

#### CI/CD

- **`hometimeline-release-monitor.yml`** — Daily cron checks dk307/HomeTimeline releases → triggers `hometimeline-custom.yml`
- **`hometimeline-custom.yml`** — Build, test, push. Also triggered by `hometimeline-base-rebuild` event
- **`hometimeline-base.yml`** — Dispatches `hometimeline-base-rebuild` after push to rebuild custom image

#### Smoke tests (7 total)

1. `/api/v1/health` endpoint
2. Docker HEALTHCHECK status
3. Diagnostics (container contents, ffmpeg binary, ldd, env vars)
4. ffmpeg binary check (`ffmpeg -version`)
5. Build metadata (`GIT_SHA`, `BUILD_TIME`, `/api/v1/system_info` API)
6. go2rtc binary
7. Python app loads

---

## CI/CD Workflows

### release-monitor.yml

- **Trigger:** Daily cron at 06:00 UTC + manual dispatch
- **Runner:** `ubuntu-24.04-arm` (native ARM64)
- **Logic:** Fetches latest upstream release from GitHub API → compares against GHCR tags → triggers build via `repository_dispatch` if new
- **Permissions:** `contents: read`

### llama-cpp-embed-nomic.yml

- **Triggers:** `repository_dispatch` (from release-monitor) + `workflow_dispatch` (manual with tag input)
- **Runner:** `ubuntu-24.04-arm` (native ARM64)
- **Pipeline:** Resolve tag → Build → Smoke test → Push → Update README → Commit
- **Tag validation:** Must match `^b[0-9]+$`
- **Smoke tests:** Health check, model info, embedding generation, 768-dim verification
- **After push:** Auto-updates README.md tags column and commits to main
- **Permissions:** `contents: write`, `packages: write`

### ncnn-release-monitor.yml

- **Trigger:** Daily cron at 07:00 UTC + manual dispatch
- **Runner:** `ubuntu-24.04-arm` (native ARM64)
- **Logic:** Fetches latest Tencent/ncnn release → compares against GHCR tags → triggers build via `repository_dispatch` if new
- **Permissions:** `contents: read`

### yolo-rest.yml

- **Triggers:** `repository_dispatch` (from ncnn-release-monitor) + `workflow_dispatch` (manual with ncnn_tag input)
- **Runner:** `ubuntu-24.04-arm` (native ARM64)
- **Pipeline:** Resolve tag → Build → Smoke test → Push → Update README → Commit
- **Tag validation:** ncnn tag must be digits only (e.g. `20260526`)
- **Smoke tests:** Health check, HEALTHCHECK status, /models listing, detect yolo26n, detect yolo26m, default model (yolo26m)
- **After push:** Auto-updates README.md tags column and commits to main
- **Permissions:** `contents: write`, `packages: write`

### hometimeline-base.yml

- **Triggers:** Weekly cron (Monday 02:00 UTC) + push to Dockerfile + `workflow_dispatch` (manual with ffmpeg_version input)
- **Runner:** `ubuntu-24.04-arm` (native ARM64)
- **Pipeline:** Resolve version → Check GHCR → Build → Smoke test → Push → Update README → Commit
- **Tag validation:** version must match `^[0-9]+\.[0-9]+(\.[0-9]+)?$`
- **Smoke tests:** ffmpeg binary, ffprobe binary, required codecs (h264, hevc, vp9, av1, aac, mjpeg), required protocols (rtsp, rtmp, hls, http), ARM64 build config, GPL/libx264 verification
- **After push:** Auto-updates README.md tags column and commits to main
- **Permissions:** `contents: write`, `packages: write`
- **Note:** FFmpeg has no GitHub Releases — only tags (`n*` format). Weekly cron fetches latest `n*` tag from API.

### hometimeline-release-monitor.yml

- **Trigger:** Daily cron at 06:30 UTC + manual dispatch
- **Runner:** `ubuntu-24.04-arm` (native ARM64)
- **Logic:** Fetches latest dk307/HomeTimeline release → compares against GHCR tags → triggers `hometimeline-custom.yml` via `repository_dispatch` if new
- **Permissions:** `contents: read`

### hometimeline-custom.yml

- **Triggers:** `repository_dispatch` (from release-monitor + hometimeline-base-rebuild) + `workflow_dispatch` (manual with hometimeline_tag input)
- **Runner:** `ubuntu-24.04-arm` (native ARM64)
- **Pipeline:** Clone upstream at pinned tag → Patch Dockerfile (libnuma1) → Build with `--target app-custom-ffmpeg` → Smoke test (7 tests) → Push → Update README → Commit
- **Build args:** `GIT_SHA` (from git rev-parse), `BUILD_TIME` (from date), passed to upstream Dockerfile
- **Cache:** Scoped per tag with base image digest for cache busting
- **Smoke tests:** health endpoint, HEALTHCHECK status, diagnostics, ffmpeg binary, build metadata, go2rtc binary, Python app
- **After push:** Auto-updates README.md tags column and commits to main
- **Permissions:** `contents: write`, `packages: write`

### Tag policy

| Container | Tag | Behavior |
|-----------|-----|----------|
| llama-cpp-embed-nomic | `:bNNNN` | Specific upstream release (overwrites on rebuild) |
| llama-cpp-embed-nomic | `:latest` | Most recent build |
| yolo-rest | `:ncnn-NNNNNN` | Specific ncnn version (overwrites on rebuild) |
| yolo-rest | `:latest` | Most recent build |
| hometimeline-base | `:ffmpeg-NNN.N.N` | Pinned FFmpeg version (overwrites on rebuild) |
| hometimeline-base | `:latest` | Most recent build |
| hometimeline-custom | `:v*.*.*` | Specific HomeTimeline release (overwrites on rebuild) |
| hometimeline-custom | `:latest` | Most recent build |

---

## File relationships

- **PLAN.md** is the source of truth for build flags, CMake configuration, and runtime parameters
- **llama-cpp/Dockerfile** is the actual build definition — keep in sync with PLAN.md
- **yolo-rest/Dockerfile** is the actual build definition for yolo-rest
- **hometimeline-base/Dockerfile** is the actual build definition for hometimeline-base
- **hometimeline-custom.yml** clones upstream HomeTimeline and uses their Dockerfile directly
- **README.md** is auto-updated by CI — manual edits are overwritten on next build. Edit the workflow, not the README, to change tag presentation.
- **AGENTS.md** (this file) — update when adding containers or changing workflows

---

## Common tasks

### Manual build
```bash
# llama-cpp
gh workflow run llama-cpp-embed-nomic.yml --ref main -f tag=b10107

# yolo-rest
gh workflow run yolo-rest.yml --ref main -f ncnn_tag=20260526

# hometimeline-base
gh workflow run hometimeline-base.yml --ref main -f ffmpeg_version=8.1.2

# hometimeline-custom
gh workflow run hometimeline-custom.yml --ref main -f hometimeline_tag=v0.12.5
```

### Check build status
```bash
gh run list --workflow=llama-cpp-embed-nomic.yml --limit=5
gh run list --workflow=yolo-rest.yml --limit=5
gh run list --workflow=hometimeline-base.yml --limit=5
gh run list --workflow=hometimeline-custom.yml --limit=5
gh run view <run-id> --log
```

### Local build (on ARM64 host)
```bash
# llama-cpp
docker build --build-arg LLAMA_CPP_TAG=b10107 -t llama-cpp-embed-nomic:local ./llama-cpp

# yolo-rest
docker build --build-arg NCNN_TAG=20260526 --build-arg MODELS_TAG=yolo-models-v1 -t yolo-rest:local ./yolo-rest

# hometimeline-base
docker build --build-arg FFMPEG_VERSION=8.1.2 -t hometimeline-base:local ./hometimeline-base
```

### Run locally
```bash
# llama-cpp
docker run -d --name embed -p 8080:8080 --ulimit memlock=-1:-1 \
  ghcr.io/dk307/llama-cpp-embed-nomic:latest

# yolo-rest (default: yolo26m)
docker run -d --name yolo -p 18080:18080 \
  ghcr.io/dk307/yolo-rest:latest

# yolo-rest (fast: yolo26n)
docker run -d --name yolo-fast -p 18080:18080 \
  ghcr.io/dk307/yolo-rest:latest --model_type yolo26n

# hometimeline-base (test only — no ENTRYPOINT)
docker run --rm ghcr.io/dk307/hometimeline-base:latest ffmpeg -version

# hometimeline-custom
docker run -d --name hometimeline -p 8080:8080 -p 8555:8555 \
  -e DATABASE_URL="sqlite:///data/timeline.db" \
  ghcr.io/dk307/hometimeline-custom:latest
```

### Host-side optimizations
- CPU governor: `echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
- Thread pinning: `taskset -c 0-3`
- Hugepages: `echo 4096 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages`
- MEMLOCK: `--ulimit memlock=-1:-1`

---

## Constraints and policies

1. **ARM64 only** — no multi-arch. `--platform=linux/arm64` in Dockerfile, native ARM64 runners.
2. **Release tags only** — never master/main/dev. Enforced by regex validation in workflows.
3. **Single repo** — all containers in this one repository.
4. **Public GHCR** — no auth needed for pulls.
5. **No default tag** — `ARG LLAMA_CPP_TAG` has no default. Build fails if not passed. `ARG NCNN_TAG` defaults to `20260526` for local dev convenience; CI always passes it explicitly.
6. **README is CI-owned** — do not manually edit tags in README; the workflow updates them automatically.
