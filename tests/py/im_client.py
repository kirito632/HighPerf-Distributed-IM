"""Small IM test client for the GateServer HTTP + ChatServer TCP protocols.

The client deliberately uses only the public wire protocol so it can be used
for smoke tests, load tests and failure-injection tests.
"""

from __future__ import annotations

import json
import socket
import struct
import time
import uuid
from dataclasses import dataclass
from typing import Any, Optional

import requests

# Protocol ids shared with ChatServer/const.h and ChatClient/global.h.
MSG_CHAT_LOGIN = 1005
MSG_CHAT_LOGIN_RSP = 1006
ID_TEXT_CHAT_MSG_REQ = 1017
ID_TEXT_CHAT_MSG_RSP = 1018
ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019
ID_GET_OFFLINE_MSG_REQ = 1023
ID_NOTIFY_TEXT_CHAT_MSG_RSP = 1024


class ImProtocolError(RuntimeError):
    pass


@dataclass
class LoginResult:
    uid: int
    token: str
    host: str
    port: int
    raw: dict[str, Any]
    access_token: str = ""


class ImClient:
    def __init__(self, gate_url: str = "http://127.0.0.1:18080") -> None:
        self.gate_url = gate_url.rstrip("/")
        self.sock: Optional[socket.socket] = None
        self.uid = 0
        self.token = ""
        self.access_token = ""
        self._buffer = b""

    # ------------------------------------------------------------------ HTTP
    def login_gate(self, email: str, password: str, timeout: float = 5.0) -> LoginResult:
        response = requests.post(
            f"{self.gate_url}/user_login",
            json={"user": email, "passwd": password},
            timeout=timeout,
        )
        response.raise_for_status()
        data = response.json()
        if data.get("error") != 0:
            raise ImProtocolError(f"gate login failed: {data}")
        host = str(data.get("host", ""))
        port = int(data.get("port", 0))
        if not host or port <= 0:
            raise ImProtocolError(f"gate login returned invalid chat endpoint: {data}")
        return LoginResult(
            uid=int(data["uid"]),
            token=str(data["token"]),
            host=host,
            port=port,
            raw=data,
            access_token=str(data.get("access_token", "")),
        )

    # ------------------------------------------------------------------- TCP
    def connect_chat(self, host: str, port: int, uid: int, token: str,
                     timeout: float = 5.0, access_token: str = "") -> dict[str, Any]:
        self.uid = uid
        self.token = token
        self.access_token = access_token
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)

        payload = {"uid": uid, "token": token}
        if access_token:
            payload["access_token"] = access_token
        self._send_packet(MSG_CHAT_LOGIN, payload)
        msg_id, body = self._recv_packet()
        if msg_id != MSG_CHAT_LOGIN_RSP:
            raise ImProtocolError(f"expected login response {MSG_CHAT_LOGIN_RSP}, got {msg_id}")
        result = json.loads(body)
        if result.get("error") != 0:
            raise ImProtocolError(f"chat login failed: {result}")
        return result

    def login(self, email: str, password: str, timeout: float = 5.0) -> dict[str, Any]:
        gate = self.login_gate(email, password, timeout=timeout)
        return self.connect_chat(
            gate.host, gate.port, gate.uid, gate.token,
            timeout=timeout, access_token=gate.access_token)

    def send_text(
        self,
        to_uid: int,
        content: str,
        client_msg_id: Optional[str] = None,
        timeout: float = 5.0,
    ) -> dict[str, Any]:
        """Send a 1017 text message and return the 1018 response."""
        msg_id = client_msg_id or str(uuid.uuid4())
        payload = {
            "fromuid": self.uid,
            "touid": to_uid,
            "client_msg_id": msg_id,
            "text_array": [{
                "content": content,
                "msgid": msg_id,
                "client_msg_id": msg_id,
            }],
        }
        self._send_packet(ID_TEXT_CHAT_MSG_REQ, payload)
        response_id, body = self._recv_packet(timeout=timeout)
        if response_id != ID_TEXT_CHAT_MSG_RSP:
            raise ImProtocolError(f"expected chat response {ID_TEXT_CHAT_MSG_RSP}, got {response_id}")
        return json.loads(body)

    def fetch_offline(self, cursor: int = 0) -> None:
        self._send_packet(ID_GET_OFFLINE_MSG_REQ, {"cursor": cursor})

    def ack(self, server_msg_ids: list[int]) -> None:
        self._send_packet(ID_NOTIFY_TEXT_CHAT_MSG_RSP, {"ids": server_msg_ids})

    def wait_notification(self, timeout: float = 5.0) -> dict[str, Any]:
        msg_id, body = self._recv_packet(timeout=timeout, allow_any=True)
        if msg_id != ID_NOTIFY_TEXT_CHAT_MSG_REQ:
            raise ImProtocolError(f"expected notification {ID_NOTIFY_TEXT_CHAT_MSG_REQ}, got {msg_id}")
        return json.loads(body)

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self) -> "ImClient":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    # ------------------------------------------------------------- internals
    def _send_packet(self, msg_id: int, payload: dict[str, Any]) -> None:
        if self.sock is None:
            raise ImProtocolError("socket is not connected")
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        if len(body) > 64 * 1024:
            raise ImProtocolError(f"body too large: {len(body)}")
        self.sock.sendall(struct.pack("!hhi", msg_id, 0, len(body)) + body)

    def _recv_exact(self, size: int, timeout: float) -> bytes:
        if self.sock is None:
            raise ImProtocolError("socket is not connected")
        self.sock.settimeout(timeout)
        chunks = []
        remaining = size
        while remaining > 0:
            chunk = self.sock.recv(remaining)
            if not chunk:
                raise ImProtocolError("connection closed by peer")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _recv_packet(self, timeout: float = 5.0, allow_any: bool = False) -> tuple[int, str]:
        header = self._recv_exact(8, timeout)
        msg_id, _flags, length = struct.unpack("!hhi", header)
        if length < 0 or length > 64 * 1024:
            raise ImProtocolError(f"invalid body length: {length}")
        body = self._recv_exact(length, timeout)
        return msg_id, body.decode("utf-8", "replace")
