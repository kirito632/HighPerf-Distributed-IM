"""etcd service registry tests.

Opt-in because the suite needs the etcd container and services with
``IM_ETCD_ENABLED=1``:

    docker compose -f docker-compose.test.yml --profile governance up -d etcd-test
    IM_ETCD_ENABLED=1 BIN_DIR=$PWD/build_test/bin bash scripts/dev/run_test_services.sh
    IM_ETCD_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration/test_etcd_registry.py -q
"""

from __future__ import annotations

import base64
import json
import os
import socket
import subprocess
import time
import uuid

import grpc
import pytest
import requests

pytestmark = pytest.mark.skipif(
    os.getenv("IM_ETCD_E2E") != "1",
    reason="set IM_ETCD_E2E=1 and start etcd plus etcd-enabled services",
)

ETCD = os.getenv("IM_ETCD_ENDPOINT", "http://127.0.0.1:12379").rstrip("/")
PROBE = os.getenv("REGISTRY_PROBE_BIN", "build_test/bin/registry_probe")
STATUS_PORT = int(os.getenv("IM_STATUS_GRPC_PORT", "15052"))
INTERNAL_TOKEN = os.getenv("IM_INTERNAL_TOKEN", "dev-internal-token")


def etcd_range(prefix: str) -> dict[str, str]:
    end = bytearray(prefix.encode())
    for i in range(len(end) - 1, -1, -1):
        if end[i] < 0xFF:
            end[i] += 1
            del end[i + 1:]
            break
    response = requests.post(
        f"{ETCD}/v3/kv/range",
        json={
            "key": base64.b64encode(prefix.encode()).decode(),
            "range_end": base64.b64encode(bytes(end)).decode(),
        },
        timeout=3,
    )
    response.raise_for_status()
    result = {}
    for kv in response.json().get("kvs", []):
        result[base64.b64decode(kv["key"]).decode()] = base64.b64decode(
            kv["value"]
        ).decode()
    return result


def discover() -> list[dict]:
    output = subprocess.run(
        [PROBE, "discover", "--service", "chatserver"],
        check=True,
        capture_output=True,
        text=True,
        timeout=10,
    ).stdout
    instances = []
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3:
            instances.append(
                {"id": parts[0], "host": parts[1], "port": int(parts[2])}
            )
    return instances


def wait_until(predicate, timeout: float = 15.0, interval: float = 0.5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(interval)
    return predicate()


def get_chat_server(uid: int = 1) -> dict[str, str]:
    channel = grpc.insecure_channel(f"127.0.0.1:{STATUS_PORT}")
    method = channel.unary_unary("/message.StatusService/GetChatServer")
    reply = method(
        b"\x08" + bytes([uid]),
        timeout=3,
        metadata=(("x-internal-token", INTERNAL_TOKEN),),
    )
    fields: dict[int, str] = {}
    index = 0
    while index < len(reply):
        tag = reply[index]
        index += 1
        field, wire = tag >> 3, tag & 7
        if wire == 2:
            length = reply[index]
            index += 1
            fields[field] = reply[index:index + length].decode()
            index += length
        elif wire == 0:
            index += 1
    return fields


def test_chatserver_instance_is_registered():
    keys = wait_until(lambda: etcd_range("im/services/chatserver/"))
    assert keys, "no chatserver instance registered in etcd"
    payload = json.loads(list(keys.values())[0])
    assert payload["host"] == "127.0.0.1"
    assert payload["port"] == 18090


def test_status_resolves_chatserver_from_registry():
    fields = get_chat_server()
    assert fields.get(2) == "127.0.0.1", fields
    assert fields.get(3) == "18090", fields


def test_probe_lease_registration_and_revoke():
    instance_id = f"probe-{uuid.uuid4().hex[:8]}"
    process = subprocess.Popen(
        [
            PROBE, "register",
            "--service", "chatserver",
            "--id", instance_id,
            "--host", "127.0.0.1",
            "--port", "19999",
            "--ttl", "30",
            "--hold", "3",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        found = wait_until(
            lambda: any(i["id"] == instance_id for i in discover()), timeout=10
        )
        assert found, "probe instance did not appear in discovery"
        assert f"im/services/chatserver/{instance_id}" in etcd_range(
            "im/services/chatserver/"
        )
    finally:
        process.wait(timeout=15)

    # The probe revokes its lease on exit, so the key must disappear well
    # before the 30s TTL would expire it.
    gone = wait_until(
        lambda: not any(i["id"] == instance_id for i in discover()), timeout=5
    )
    assert gone, "revoked instance is still discoverable"
