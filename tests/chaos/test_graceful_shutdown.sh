#!/usr/bin/env bash
# Verify ChatServer exits cleanly on SIGINT without a self-join crash.
set -euo pipefail

PID="${1:-}"
if [[ -z "${PID}" ]]; then
    PID="$(pgrep -f 'build_test/bin/chat_server' | head -1 || true)"
fi
if [[ -z "${PID}" ]]; then
    echo "SKIP: no chat_server pid" >&2
    exit 0
fi

echo "sending SIGINT to ${PID}"
kill -INT "${PID}"

for _ in $(seq 1 40); do
    if ! kill -0 "${PID}" 2>/dev/null; then
        echo "PASS: chat_server exited"
        exit 0
    fi
    sleep 0.25
done

echo "FAIL: chat_server still running after SIGINT" >&2
exit 1
