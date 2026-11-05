import os

import grpc
import pytest

pytestmark = pytest.mark.skipif(
    os.getenv("IM_E2E") != "1",
    reason="set IM_E2E=1 and start StatusServer",
)

STATUS_PORT = int(os.getenv("IM_STATUS_GRPC_PORT", "15052"))
INTERNAL_TOKEN = os.getenv("IM_INTERNAL_TOKEN", "dev-internal-token")


def _method():
    channel = grpc.insecure_channel(f"127.0.0.1:{STATUS_PORT}")
    return channel.unary_unary(
        "/message.StatusService/GetChatServer",
        request_serializer=lambda value: value,
        response_deserializer=lambda value: value,
    )


def test_status_rejects_missing_internal_token():
    request = b"\x08\x01"  # GetChatServerReq { uid: 1 }
    with pytest.raises(grpc.RpcError) as exc_info:
        _method()(request, timeout=3)
    assert exc_info.value.code() == grpc.StatusCode.UNAUTHENTICATED


def test_status_accepts_internal_token():
    request = b"\x08\x01"
    reply = _method()(
        request,
        timeout=3,
        metadata=(("x-internal-token", INTERNAL_TOKEN),),
    )
    assert reply
