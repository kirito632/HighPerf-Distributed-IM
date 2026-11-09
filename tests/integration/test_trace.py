"""End-to-end W3C trace-context propagation.

The suite is opt-in because it needs a trace-enabled build and a running
Jaeger instance:

    cmake -S . -B build_otel -DIM_WITH_OPENTELEMETRY=ON
    cmake --build build_otel --target gate_server chat_server status_server
    docker compose -f docker-compose.test.yml --profile observability up -d jaeger-test
    BIN_DIR=$PWD/build_otel/bin IM_TRACING_ENABLED=1 bash scripts/dev/run_test_services.sh
    IM_TRACE_E2E=1 pytest tests/integration/test_trace.py -q

The test asserts the trace id that the client sends to Gate survives the HTTP
login, the TCP login, the chat message, the Outbox row, the Kafka publish and
the delivery consumer, and that Jaeger contains spans from both services.
"""

from __future__ import annotations

import os
import time
import uuid

import pytest
import requests

from im_client import ImClient

pytestmark = pytest.mark.skipif(
    os.getenv("IM_TRACE_E2E") != "1",
    reason="set IM_TRACE_E2E=1, build with IM_WITH_OPENTELEMETRY=ON and start Jaeger",
)

GATE_URL = os.getenv("IM_GATE_URL", "http://127.0.0.1:18080")
JAEGER_URL = os.getenv("IM_JAEGER_URL", "http://127.0.0.1:16686").rstrip("/")


def new_trace() -> tuple[str, str]:
    trace_id = uuid.uuid4().hex
    return trace_id, f"00-{trace_id}-{uuid.uuid4().hex[:16]}-01"


def wait_for_spans(trace_id: str, required: set[str], timeout: float = 30.0) -> dict:
    """Poll Jaeger until every required span name has been exported."""
    deadline = time.time() + timeout
    last: dict = {}
    while time.time() < deadline:
        response = requests.get(f"{JAEGER_URL}/api/traces/{trace_id}", timeout=5)
        if response.status_code == 200:
            data = response.json().get("data") or []
            if data:
                last = data[0]
                names = {span["operationName"] for span in last.get("spans", [])}
                if required <= names:
                    return last
        time.sleep(1)
    return last


def test_gate_http_span_continues_incoming_trace(test_credentials):
    trace_id, parent = new_trace()
    email, password = test_credentials

    response = requests.post(
        f"{GATE_URL}/user_login",
        json={"user": email, "passwd": password},
        headers={"traceparent": parent},
        timeout=5,
    )
    assert response.status_code == 200, response.text
    assert response.json().get("error") == 0, response.text

    child = response.headers.get("traceparent")
    assert child, (
        "Gate did not return a traceparent response header; "
        "build with IM_WITH_OPENTELEMETRY=ON and set IM_TRACING_ENABLED=1"
    )
    version, child_trace_id, child_span_id, flags = child.split("-")
    assert version == "00"
    assert child_trace_id == trace_id
    assert child_span_id != parent.split("-")[2]
    assert flags == "01"


def test_chat_chain_reaches_outbox_and_jaeger(test_credentials, peer_uid, db):
    trace_id, parent = new_trace()
    email, password = test_credentials
    client_msg_id = f"trace-{uuid.uuid4().hex}"

    client = ImClient(GATE_URL)
    client.login(email, password, traceparent=parent)
    try:
        response = client.send_text(
            peer_uid, "trace-e2e", client_msg_id=client_msg_id
        )
    finally:
        client.close()

    assert response["error"] == 0, response
    assert trace_id in str(response.get("traceparent", "")), response

    rows = db.query(
        "SELECT traceparent FROM outbox "
        f"WHERE payload LIKE '%{client_msg_id}%' ORDER BY id DESC LIMIT 1"
    )
    assert rows, f"no outbox row found for client_msg_id={client_msg_id}"
    assert rows[0][0].startswith(f"00-{trace_id}-"), rows[0][0]

    trace = wait_for_spans(
        trace_id,
        {
            "POST /user_login",
            "tcp.login",
            "tcp.chat.send",
            "kafka.outbox.publish",
            "kafka.delivery",
        },
    )
    assert trace, f"Jaeger has no trace for {trace_id}"

    services = {process["serviceName"] for process in trace.get("processes", {}).values()}
    assert "gate-server" in services, services
    assert "chat-server" in services, services

    names = {span["operationName"] for span in trace["spans"]}
    assert "kafka.delivery" in names, names
