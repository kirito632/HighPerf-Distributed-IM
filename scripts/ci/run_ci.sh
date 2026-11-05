#!/usr/bin/env bash
# Local approximation of the CI pipeline.
# Usage: scripts/ci/run_ci.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build_ci"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON

cmake --build "${BUILD_DIR}" --target im_unit_tests -j"$(nproc)"

ctest --test-dir "${BUILD_DIR}" --output-on-failure

if [[ "${IM_E2E:-0}" == "1" ]]; then
    python3 -m pytest "${ROOT_DIR}/tests/integration" -v
fi
