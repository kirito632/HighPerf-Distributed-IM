#!/usr/bin/env bash
# Stop and remove the isolated test environment (including volumes).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE_FILE="${ROOT_DIR}/docker-compose.test.yml"

docker compose -f "${COMPOSE_FILE}" --profile kafka --profile observability --profile governance \
    down -v --remove-orphans
