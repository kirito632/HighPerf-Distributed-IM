import os

import requests

from im_client import ImClient

CHAT_METRICS = os.getenv("IM_CHAT_METRICS_URL", "http://127.0.0.1:19101/metrics")
GATE_METRICS = os.getenv("IM_GATE_METRICS_URL", "http://127.0.0.1:19103/metrics")


def test_chat_metrics_exposes_message_counter(im_client: ImClient, peer_uid: int):
    response = im_client.send_text(peer_uid, "metrics-check")
    assert response["error"] == 0, response

    metrics = requests.get(CHAT_METRICS, timeout=5)
    assert metrics.status_code == 200
    assert "im_messages_received_total" in metrics.text


def test_gate_metrics_endpoint_is_live():
    metrics = requests.get(GATE_METRICS, timeout=5)
    assert metrics.status_code == 200
    assert "text/plain" in metrics.headers.get("Content-Type", "")
