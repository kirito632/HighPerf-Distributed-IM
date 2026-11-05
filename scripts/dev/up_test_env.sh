#!/usr/bin/env bash
# Start the isolated integration-test infrastructure.
# Usage: scripts/dev/up_test_env.sh [compose profiles...]
#   e.g. scripts/dev/up_test_env.sh kafka observability
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE_FILE="${ROOT_DIR}/docker-compose.test.yml"

profiles=()
for profile in "$@"; do
    profiles+=(--profile "${profile}")
done

docker compose -f "${COMPOSE_FILE}" "${profiles[@]}" up -d --wait --wait-timeout 180

echo "Test infrastructure is up."
docker compose -f "${COMPOSE_FILE}" ps
