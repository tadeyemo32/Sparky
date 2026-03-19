#!/usr/bin/env bash
# Run SparkyServer locally with .env loaded.
# Usage: ./scripts/run-local.sh
set -e
cd "$(dirname "$0")/.."          # always run from SparkyServer/

if [ ! -f .env ]; then
    echo "ERROR: .env not found in $(pwd)" >&2
    exit 1
fi

# source the .env file — handles values that contain spaces (e.g. libpq connstr)
set -a
# shellcheck source=../.env
source .env
set +a

echo "[local] SPARKY_ALLOW_PLAINTEXT=${SPARKY_ALLOW_PLAINTEXT}"
echo "[local] DB host=$(echo "$SPARKY_PG_CONNSTR" | grep -o 'host=[^ ]*')"
echo "[local] Launching ./bin/SparkyServer ..."
exec ./bin/SparkyServer "$@"
