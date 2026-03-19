# syntax=docker/dockerfile:1
#
# Build context must be the Sparky repo ROOT (one level above SparkyServer/),
# because TlsLayer.h and Protocol.h live in SparkyLoader/user/include/.
#
#   docker build -f SparkyServer/Dockerfile -t sparkyserver .
#
# Run (supply a real PostgreSQL connection string):
#   docker run -p 7777:7777 \
#     -e SPARKY_PG_CONNSTR="host=<host> port=5432 dbname=sparky user=sparky password=<pw> sslmode=require" \
#     sparkyserver

# ---------------------------------------------------------------------------
# Stage 1: builder
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libssl-dev \
        libpq-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy the whole Sparky repo root so both SparkyServer/ and SparkyLoader/ are
# available during the build (TlsLayer.h / Protocol.h live in SparkyLoader/).
WORKDIR /src
COPY . .

# CMAKE_SOURCE_DIR will be /src/SparkyServer, so the binary lands in
# /src/SparkyServer/bin/SparkyServer (set by CMAKE_RUNTIME_OUTPUT_DIRECTORY).
RUN cmake -S SparkyServer -B SparkyServer/build \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build SparkyServer/build --parallel

# ---------------------------------------------------------------------------
# Stage 2: runtime
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 \
        libpq5 \
        postgresql-client \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Run as a non-root user (required by GCP Cloud Run policies and best practice).
RUN useradd --system --no-create-home --shell /usr/sbin/nologin sparky

WORKDIR /app

# Binary is written to CMAKE_SOURCE_DIR/bin by CMakeLists.txt
COPY --from=builder /src/SparkyServer/bin/SparkyServer /app/SparkyServer
COPY --from=builder /src/SparkyServer/sparky.crt /app/sparky.crt
COPY --from=builder /src/SparkyServer/sparky.key /app/sparky.key

RUN chown sparky:sparky /app/SparkyServer /app/sparky.crt /app/sparky.key

# Create data directory for Cloud Storage FUSE mount and symlink persistent files
RUN mkdir -p /app/data/backups /app/payloads && \
    ln -s /app/data/backups /app/backups && \
    ln -s /app/data/config.bin /app/config.bin && \
    ln -s /app/payloads/SparkyCore.dll /app/SparkyCore.dll && \
    chown -R sparky:sparky /app/data /app/payloads
# Note: /app/backups, /app/config.bin and /app/SparkyCore.dll are symlinks
# into the cloud-mounted /app/data and /app/payloads volumes.  Chowning the
# symlinks themselves at build time is a no-op (dangling targets), so we only
# chown the directories — the mounted content will be owned by the runtime UID.

USER sparky

# SPARKY_ALLOW_PLAINTEXT is intentionally NOT set here.
# TLS is mandatory in production: sparky.crt + sparky.key are injected by CI
# via GitHub Secrets (see deploy.yml).  Set this only for local dev explicitly:
#   docker run -e SPARKY_ALLOW_PLAINTEXT=1 ...

# Default standalone port. On GCP Cloud Run, $PORT is injected automatically
# (typically 8080) and the binary honours it at startup — EXPOSE is advisory only.
EXPOSE 7777

ENTRYPOINT ["/app/SparkyServer"]
