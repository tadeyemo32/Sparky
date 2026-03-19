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
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libssl-dev \
        libpq-dev \
        openssl \
    && rm -rf /var/lib/apt/lists/*

# Copy the whole Sparky repo root so both SparkyServer/ and SparkyLoader/ are
# available during the build (TlsLayer.h / Protocol.h live in SparkyLoader/).
WORKDIR /src
COPY . .

# If CI injected real cert/key via secrets, use them.
# Otherwise generate a self-signed fallback so the server always starts.
# (The loader skips cert verification; Cloud Run gen2 forwards raw TCP.)
RUN if [ ! -s SparkyServer/sparky.crt ] || [ ! -s SparkyServer/sparky.key ]; then \
        openssl req -x509 -newkey rsa:4096 \
            -keyout SparkyServer/sparky.key \
            -out    SparkyServer/sparky.crt \
            -days 3650 -nodes -subj "/CN=sparky" 2>/dev/null; \
    fi

# CMAKE_SOURCE_DIR will be /src/SparkyServer, so the binary lands in
# /src/SparkyServer/bin/SparkyServer (set by CMAKE_RUNTIME_OUTPUT_DIRECTORY).
RUN cmake -S SparkyServer -B SparkyServer/build \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build SparkyServer/build --target SparkyServer --parallel

# ---------------------------------------------------------------------------
# Stage 2: runtime
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

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

USER sparky

EXPOSE 7777

ENTRYPOINT ["/app/SparkyServer"]
