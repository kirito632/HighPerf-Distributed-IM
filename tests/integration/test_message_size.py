import socket
import struct

import pytest

from im_client import ImClient, ImProtocolError


def test_55kb_text_roundtrip(im_client: ImClient, peer_uid: int):
    content = "x" * (55 * 1024)
    response = im_client.send_text(peer_uid, content, timeout=10)
    assert response["error"] == 0, response
    assert response["server_msg_id"] > 0
    assert len(response["text_array"][0]["content"]) == len(content)


def test_oversized_packet_closes_only_that_connection(gate_url: str, test_credentials):
    email, password = test_credentials
    first = ImClient(gate_url)
    first.login(email, password)
    oversized = 64 * 1024 + 1
    try:
        first.sock.sendall(struct.pack("!hhi", 1017, 0, oversized) + b"x" * oversized)
        with pytest.raises((ImProtocolError, ConnectionError, socket.timeout, OSError)):
            first._recv_packet(timeout=3)
    finally:
        first.close()

    # The server must still accept a fresh connection after the oversized frame.
    second = ImClient(gate_url)
    try:
        second.login(email, password)
        response = second.send_text(2, "after-oversize")
        assert response["error"] == 0, response
    finally:
        second.close()
