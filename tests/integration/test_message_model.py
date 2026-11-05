import time
import uuid

import pytest

from im_client import ImClient


def _wait_for(predicate, timeout=3.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.1)
    return predicate()


def test_same_client_msg_id_is_idempotent(im_client: ImClient, peer_uid: int, db):
    cid = str(uuid.uuid4())
    first = im_client.send_text(peer_uid, "idempotent", client_msg_id=cid)
    second = im_client.send_text(peer_uid, "idempotent", client_msg_id=cid)

    assert first["error"] == 0, first
    assert second["error"] == 0, second
    assert first["server_msg_id"] == second["server_msg_id"]
    assert first["server_msg_id"] > 0

    rows = db.query(
        "SELECT COUNT(*) FROM messages "
        f"WHERE from_uid={im_client.uid} AND client_msg_id='{cid}'"
    )
    assert int(rows[0][0]) == 1


def test_ack_marks_only_the_requested_message(im_client: ImClient, peer_client: ImClient,
                                             peer_uid: int, db):
    ids = [im_client.send_text(peer_uid, f"ack-{i}")["server_msg_id"] for i in range(3)]
    assert all(msg_id > 0 for msg_id in ids)

    peer_client.ack([ids[0]])

    def statuses():
        rows = db.query(
            "SELECT id, status FROM messages "
            f"WHERE to_uid={peer_uid} AND id IN ({','.join(str(i) for i in ids)}) "
            "ORDER BY id"
        )
        return {int(row[0]): int(row[1]) for row in rows}

    result = _wait_for(lambda: (s := statuses()).get(ids[0]) == 1 and s, timeout=3.0)
    assert result, "ACK was not persisted"
    assert result[ids[0]] == 1
    assert result[ids[1]] == 0
    assert result[ids[2]] == 0


def test_spoofed_fromuid_is_rejected(im_client: ImClient, peer_uid: int):
    payload = {
        "fromuid": peer_uid,  # must not be trusted
        "touid": peer_uid,
        "client_msg_id": str(uuid.uuid4()),
        "text_array": [{"content": "spoof", "msgid": str(uuid.uuid4())}],
    }
    im_client._send_packet(1017, payload)
    _msg_id, body = im_client._recv_packet(timeout=5)
    response = __import__("json").loads(body)
    assert response["error"] == 1012


def test_cursor_fetch_returns_only_newer_messages(im_client: ImClient,
                                                  peer_credentials: tuple[str, str],
                                                  peer_uid: int, db):
    db.query(f"DELETE FROM messages WHERE to_uid={peer_uid}")
    sent_ids = []
    for i in range(3):
        response = im_client.send_text(peer_uid, f"cursor-{i}")
        sent_ids.append(response["server_msg_id"])

    # Let the Kafka consumer finish these events before logging in, otherwise
    # live delivery and offline cursor fetch can legitimately race.
    event_ids = ",".join(f"'message:{msg_id}:created'" for msg_id in sent_ids)

    def processed():
        rows = db.query(
            f"SELECT COUNT(*) FROM processed_events WHERE event_id IN ({event_ids})"
        )
        return int(rows[0][0]) == len(sent_ids)

    assert _wait_for(processed, timeout=10.0), "consumer did not process events"

    receiver = ImClient(im_client.gate_url)
    try:
        receiver.login(*peer_credentials)
        receiver.fetch_offline(cursor=0)
        received = []
        for _ in range(3):
            notification = receiver.wait_notification(timeout=5)
            received.append(notification["server_msg_id"])
        assert received == sent_ids
    finally:
        receiver.close()
