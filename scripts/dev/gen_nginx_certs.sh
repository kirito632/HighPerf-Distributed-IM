#!/usr/bin/env bash
# Generate the self-signed certificate used by the local nginx test gateway.
# Usage: scripts/dev/gen_nginx_certs.sh [output-dir]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${1:-${ROOT_DIR}/deploy/nginx/certs}"
mkdir -p "${OUT_DIR}"

if [[ -f "${OUT_DIR}/server.crt" && -f "${OUT_DIR}/server.key" ]]; then
    echo "certificates already exist in ${OUT_DIR}" >&2
    exit 0
fi

openssl req -x509 -nodes -newkey rsa:2048 -days 365 \
    -keyout "${OUT_DIR}/server.key" \
    -out "${OUT_DIR}/server.crt" \
    -subj "/C=CN/ST=local/L=local/O=im-test/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" >/dev/null 2>&1

chmod 600 "${OUT_DIR}/server.key"
echo "wrote ${OUT_DIR}/server.crt and ${OUT_DIR}/server.key"
