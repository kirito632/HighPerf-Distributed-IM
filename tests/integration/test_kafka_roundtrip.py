import os
import subprocess
import uuid

import pytest

pytestmark = pytest.mark.skipif(
    os.getenv("IM_KAFKA_E2E") != "1",
    reason="set IM_KAFKA_E2E=1 and start Kafka with topic im.message.created",
)


def test_kafka_produce_consume_roundtrip():
    mq_smoke = os.getenv("MQ_SMOKE_BIN", "build_test/bin/mq_smoke")
    value = f"roundtrip-{uuid.uuid4().hex}"
    group = f"im-test-{uuid.uuid4().hex}"

    produced = subprocess.run(
        [mq_smoke, "produce", "im.message.created", "test-key", value],
        check=False,
        capture_output=True,
        text=True,
        timeout=15,
    )
    assert produced.returncode == 0, produced.stderr

    consumed = subprocess.run(
        [mq_smoke, "consume", "im.message.created", group, value],
        check=False,
        capture_output=True,
        text=True,
        timeout=15,
    )
    assert consumed.returncode == 0, consumed.stderr
    assert value in consumed.stdout
    assert "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01" in consumed.stdout
