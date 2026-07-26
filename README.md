# ARM64 Media/HomeLab Containers

ARM64-only Docker containers built and published to GHCR. Single repo, automated CI/CD.

**Platform:** Linux ARM64 (Cortex-A78C)
**Registry:** [ghcr.io/dk307](https://ghcr.io/dk307) (public)

---

## Containers

| Container | Upstream | Description | Tags |
|-----------|----------|-------------|------|
| [`llama-cpp-embed-nomic`](#llama-cpp-embed-nomic) | [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) | nomic-embed-text embedding server | `b10107`, `latest` |

---

### llama-cpp-embed-nomic

Embedding server running [nomic-embed-text v1.5](https://huggingface.co/nomic-ai/nomic-embed-text-v1.5) (f16, 137M params, 768-dim) via llama.cpp. Built with Clang 21 and Cortex-A78C tuned flags.

**Pull:**
```bash
# Latest upstream release
docker pull ghcr.io/dk307/llama-cpp-embed-nomic:latest

# Pinned to specific release
docker pull ghcr.io/dk307/llama-cpp-embed-nomic:b10107
```

**Run:**
```bash
# Docker
docker run -d \
  --name embed \
  -p 8080:8080 \
  --ulimit memlock=-1:-1 \
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

---

## How Updates Work

Upstream releases are monitored daily. When a new llama.cpp release is detected:

1. `release-monitor.yml` compares the upstream tag against published GHCR tags
2. If new, triggers `llama-cpp-embed-nomic.yml` via `repository_dispatch`
3. Build compiles llama.cpp from the release tag source
4. Smoke tests verify health, model loading, and embedding generation
5. Image pushed as `ghcr.io/dk307/llama-cpp-embed-nomic:<tag>` and `:latest`

**Manual trigger:** `gh workflow run llama-cpp-embed-nomic.yml -f tag=<release-tag>`

---

## Building Locally

```bash
# Requires the upstream release tag as build arg
docker build \
  --build-arg LLAMA_CPP_TAG=b10107 \
  -t llama-cpp-embed-nomic:local \
  ./llama-cpp
```
