#!/usr/bin/env bash
# Start Status/Chat/Gate against the isolated test infrastructure.
# Requires: docker compose test env up + seeded + binaries built.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN_DIR="${BIN_DIR:-${ROOT_DIR}/build_release/bin}"
RUN_DIR="${ROOT_DIR}/.run-test"
CONFIG="${ROOT_DIR}/config_test.ini"

for bin in gate_server chat_server status_server; do
    if [[ ! -x "${BIN_DIR}/${bin}" ]]; then
        echo "missing binary: ${BIN_DIR}/${bin}" >&2
        echo "build it first, or set BIN_DIR=/path/to/bin" >&2
        exit 1
    fi
done

rm -rf "${RUN_DIR}"
mkdir -p "${RUN_DIR}"
cp "${CONFIG}" "${RUN_DIR}/config_gate.ini"
cp "${CONFIG}" "${RUN_DIR}/config_chat1.ini"
cp "${CONFIG}" "${RUN_DIR}/config_chat2.ini"
cp "${CONFIG}" "${RUN_DIR}/config_status.ini"

# The shared test config has Chat metrics on 19101; move Gate to 19103.
python3 - "${RUN_DIR}/config_gate.ini" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
text = p.read_text()
text = text.replace("[Metrics]\nEnabled = true\nPort = 19101",
                    "[Metrics]\nEnabled = true\nPort = 19103")
p.write_text(text)
PY

# Ephemeral JWT keys for the test run; never committed.
"${ROOT_DIR}/scripts/dev/gen_jwt_keys.sh" "${RUN_DIR}/keys" >/dev/null

cd "${RUN_DIR}"

nohup "${BIN_DIR}/status_server" > status.log 2>&1 &
echo $! > status.pid
sleep 1

nohup "${BIN_DIR}/chat_server" > chat.log 2>&1 &
echo $! > chat.pid
sleep 2

nohup "${BIN_DIR}/gate_server" > gate.log 2>&1 &
echo $! > gate.pid
sleep 2

echo "status pid: $(cat status.pid)"
echo "chat   pid: $(cat chat.pid)"
echo "gate   pid: $(cat gate.pid)"
echo "logs: ${RUN_DIR}/*.log"
