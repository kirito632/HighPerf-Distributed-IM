import time
import uuid

from im_client import ImClient


def _wait_for(predicate, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.1)
    return predicate()


def test_message_creates_and_publishes_outbox(im_client: ImClient, peer_uid: int, db):
    content = f"outbox-{uuid.uuid4().hex}"
    response = im_client.send_text(peer_uid, content)
    assert response["error"] == 0, response
    server_msg_id = response["server_msg_id"]
    event_id = f"message:{server_msg_id}:created"

    def row():
        result = db.query(
            "SELECT status, payload FROM outbox "
            f"WHERE event_id='{event_id}'"
        )
        return result[0] if result else None

    created = _wait_for(lambda: row(), timeout=3.0)
    assert created, "outbox row was not written in the same transaction"
    assert content in created[1]

    published = _wait_for(lambda: row() and row()[0] == "1", timeout=10.0)
    assert published, "outbox row was not published to Kafka"
