#!/usr/bin/env bash
# Generate an RSA key pair for local JWT development.
# Never commit the private key.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KEY_DIR="${1:-${ROOT_DIR}/keys}"

mkdir -p "${KEY_DIR}"
chmod 700 "${KEY_DIR}"

if [[ -f "${KEY_DIR}/jwt_private.pem" ]]; then
    echo "refusing to overwrite ${KEY_DIR}/jwt_private.pem" >&2
    exit 1
fi

openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 \
    -out "${KEY_DIR}/jwt_private.pem"
openssl pkey -in "${KEY_DIR}/jwt_private.pem" \
    -pubout -out "${KEY_DIR}/jwt_public.pem"
chmod 600 "${KEY_DIR}/jwt_private.pem"
chmod 644 "${KEY_DIR}/jwt_public.pem"

echo "generated:"
echo "  ${KEY_DIR}/jwt_private.pem"
echo "  ${KEY_DIR}/jwt_public.pem"
