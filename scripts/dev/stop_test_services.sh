#!/usr/bin/env bash
# Stop services started by run_test_services.sh.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUN_DIR="${ROOT_DIR}/.run-test"

for name in gate chat status; do
    pid_file="${RUN_DIR}/${name}.pid"
    if [[ -f "${pid_file}" ]]; then
        pid="$(cat "${pid_file}")"
        if kill -0 "${pid}" 2>/dev/null; then
            kill -TERM "${pid}" || true
            for _ in $(seq 1 20); do
                kill -0 "${pid}" 2>/dev/null || break
                sleep 0.2
            done
            kill -9 "${pid}" 2>/dev/null || true
        fi
        rm -f "${pid_file}"
    fi
done

echo "test services stopped"
