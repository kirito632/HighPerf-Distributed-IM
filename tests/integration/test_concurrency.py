"""Concurrency regression tests for the sharded LogicSystem workers.

Each TCP session is pinned to one worker by a hash of its session id, so
messages from the same session must keep their arrival order while different
sessions are processed in parallel. This test exercises both properties from
the client side: every connection sends a burst and the ACKed server message
ids must be strictly increasing per connection and unique across connections.
"""

from __future__ import annotations

import os
import threading
import uuid

import pytest

from im_client import ImClient

pytestmark = pytest.mark.skipif(
    os.getenv("IM_E2E") != "1",
    reason="set IM_E2E=1 and start Chat/Gate",
)

CLIENTS = int(os.getenv("IM_CONCURRENCY_CLIENTS", "8"))
MESSAGES = int(os.getenv("IM_CONCURRENCY_MESSAGES", "15"))


def test_concurrent_sessions_keep_order_without_loss(
    gate_url: str,
    test_credentials: tuple[str, str],
    peer_uid: int,
):
    email, password = test_credentials
    clients = []
    for _ in range(CLIENTS):
        client = ImClient(gate_url)
        client.login(email, password)
        clients.append(client)

    barrier = threading.Barrier(CLIENTS)
    results: list[list[int]] = []
    errors: list[dict] = []
    guard = threading.Lock()

    def worker(client: ImClient) -> None:
        ids: list[int] = []
        try:
            barrier.wait(timeout=30)
            for index in range(MESSAGES):
                response = client.send_text(
                    peer_uid,
                    f"concurrency-{index}",
                    client_msg_id=str(uuid.uuid4()),
                )
                if response.get("error") != 0:
                    with guard:
                        errors.append(response)
                    break
                ids.append(int(response["server_msg_id"]))
        except Exception as exc:  # noqa: BLE001
            with guard:
                errors.append({"exception": repr(exc)})
        finally:
            with guard:
                results.append(ids)
            client.close()

    threads = [threading.Thread(target=worker, args=(c,)) for c in clients]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(timeout=60)

    assert not errors, errors
    assert len(results) == CLIENTS, len(results)

    all_ids: list[int] = []
    for ids in results:
        assert len(ids) == MESSAGES, ids
        assert ids == sorted(ids), f"per-session order violated: {ids}"
        all_ids.extend(ids)

    assert len(all_ids) == len(set(all_ids)), "duplicate server_msg_id across sessions"
    assert all(msg_id > 0 for msg_id in all_ids)
