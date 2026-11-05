import json
import os
import time

import pytest

from im_client import ImClient

pytestmark = pytest.mark.skipif(
    os.getenv("IM_E2E") != "1",
    reason="set IM_E2E=1 and start GateServer/ChatServer test environment",
)


def test_login_and_send_message(im_client: ImClient, peer_uid: int):
    response = im_client.send_text(peer_uid, "hello from pytest")

    assert response.get("error") == 0, response
    assert response.get("fromuid") == im_client.uid
    assert response.get("touid") == peer_uid

    body = json.dumps(response)
    assert "hello from pytest" in body


def test_receiver_gets_notification(im_client: ImClient, peer_client: ImClient, peer_uid: int):
    assert peer_client.uid == peer_uid
    im_client.send_text(peer_uid, "notification from pytest")

    # Kafka delivery is at-least-once; drain any replayed older events and
    # assert the new notification eventually arrives.
    deadline = time.time() + 10
    while time.time() < deadline:
        notification = peer_client.wait_notification(timeout=5)
        if "notification from pytest" in json.dumps(notification):
            assert notification.get("error") == 0
            assert notification.get("touid") == peer_uid
            return
    pytest.fail("expected notification was not delivered")
