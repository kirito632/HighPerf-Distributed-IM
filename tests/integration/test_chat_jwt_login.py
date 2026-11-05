import os

import pytest

from im_client import ImClient, ImProtocolError

pytestmark = pytest.mark.skipif(
    os.getenv("IM_E2E") != "1",
    reason="set IM_E2E=1 and start ChatServer with JWT enabled",
)


def test_chat_login_accepts_jwt(gate_url: str, test_credentials):
    email, password = test_credentials
    client = ImClient(gate_url)
    try:
        gate = client.login_gate(email, password)
        assert gate.access_token
        result = client.connect_chat(
            gate.host, gate.port, gate.uid, gate.token,
            access_token=gate.access_token)
        assert result["error"] == 0
        assert result["uid"] == gate.uid
    finally:
        client.close()


def test_chat_login_rejects_missing_jwt(gate_url: str, test_credentials):
    email, password = test_credentials
    client = ImClient(gate_url)
    try:
        gate = client.login_gate(email, password)
        with pytest.raises(ImProtocolError):
            client.connect_chat(gate.host, gate.port, gate.uid, gate.token)
    finally:
        client.close()


def test_chat_login_rejects_tampered_jwt(gate_url: str, test_credentials):
    email, password = test_credentials
    client = ImClient(gate_url)
    try:
        gate = client.login_gate(email, password)
        with pytest.raises(ImProtocolError):
            client.connect_chat(
                gate.host, gate.port, gate.uid, gate.token,
                access_token=gate.access_token[:-2] + "xx")
    finally:
        client.close()
