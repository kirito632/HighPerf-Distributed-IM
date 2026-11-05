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


def test_consumer_records_processed_event(im_client: ImClient, peer_uid: int, db):
    content = f"delivery-{uuid.uuid4().hex}"
    response = im_client.send_text(peer_uid, content)
    assert response["error"] == 0, response
    event_id = f"message:{response['server_msg_id']}:created"

    def processed():
        rows = db.query(
            "SELECT consumer FROM processed_events "
            f"WHERE event_id='{event_id}'"
        )
        return rows[0] if rows else None

    row = _wait_for(processed, timeout=10.0)
    assert row, "consumer did not record processed event"
    assert "chatserver" in row[0]
