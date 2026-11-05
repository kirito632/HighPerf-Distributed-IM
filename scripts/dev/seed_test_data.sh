#!/usr/bin/env bash
# Create deterministic test users/friendships in the isolated MySQL container.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE_FILE="${ROOT_DIR}/docker-compose.test.yml"
SEED_FILE="${ROOT_DIR}/deploy/sql/002_seed.sql"

docker compose -f "${COMPOSE_FILE}" exec -T mysql-test \
    mysql -uchatuser -ptest chat_system_test < "${SEED_FILE}"

echo "Seeded test data:"
docker compose -f "${COMPOSE_FILE}" exec -T mysql-test \
    mysql -uchatuser -ptest -N -e \
    "SELECT uid, name, email FROM chat_system_test.user ORDER BY uid;"
