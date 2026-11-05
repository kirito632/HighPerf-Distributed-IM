import os
import subprocess
import time
import uuid


def _wait_for(predicate, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.1)
    return predicate()


def test_malformed_event_goes_to_dlq(db):
    mq_smoke = os.getenv("MQ_SMOKE_BIN", "build_test/bin/mq_smoke")
    event_id = f"malformed-{uuid.uuid4().hex}"
    payload = "{this-is-not-json"

    produced = subprocess.run(
        [mq_smoke, "produce", "im.message.created", event_id, payload],
        check=False, capture_output=True, text=True, timeout=15,
    )
    assert produced.returncode == 0, produced.stderr

    def processed():
        rows = db.query(
            "SELECT event_type FROM processed_events "
            f"WHERE event_id='{event_id}'"
        )
        return rows[0] if rows else None

    row = _wait_for(processed, timeout=10.0)
    assert row and row[0] == "Malformed"

    consumed = subprocess.run(
        [mq_smoke, "consume", "im.message.dlq", f"dlq-{uuid.uuid4().hex}", payload],
        check=False, capture_output=True, text=True, timeout=15,
    )
    assert consumed.returncode == 0, consumed.stderr
    assert payload in consumed.stdout
