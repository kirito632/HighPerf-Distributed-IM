#!/usr/bin/env bash
# Create the IM topics once the kafka profile is up.
# Usage: scripts/dev/init_kafka_topics.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE_FILE="${ROOT_DIR}/docker-compose.test.yml"
BROKER="${KAFKA_BROKER:-kafka-test:9092}"

topics=(
    "im.message.created:12"
    "im.message.delivered:6"
    "im.friend.events:6"
    "im.presence:6"
    "im.message.dlq:3"
    "im.audit:6"
)

for item in "${topics[@]}"; do
    name="${item%%:*}"
    partitions="${item##*:}"
    docker compose -f "${COMPOSE_FILE}" exec -T kafka-test \
        /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${BROKER}" \
        --create --if-not-exists \
        --topic "${name}" \
        --partitions "${partitions}" \
        --replication-factor 1
done

docker compose -f "${COMPOSE_FILE}" exec -T kafka-test \
    /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${BROKER}" --list
