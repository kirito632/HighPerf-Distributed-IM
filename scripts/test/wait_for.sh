#!/usr/bin/env bash
# Poll a command until it succeeds or the timeout expires.
# Usage: wait_for "description" timeout_seconds "command"
set -euo pipefail

desc="${1:?description required}"
timeout="${2:?timeout required}"
shift 2
cmd="$*"

start=$(date +%s)
until eval "$cmd"; do
    now=$(date +%s)
    if (( now - start >= timeout )); then
        echo "TIMEOUT: ${desc} (${timeout}s)" >&2
        exit 1
    fi
    sleep 0.5
done
echo "OK: ${desc}"
