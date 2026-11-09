import os
import uuid

import pytest
import requests

pytestmark = pytest.mark.skipif(
    os.getenv("IM_E2E") != "1",
    reason="set IM_E2E=1 and start GateServer with JWT enabled",
)

EMAIL = os.getenv("IM_TEST_EMAIL", "imtest1@example.com")
PASSWORD = os.getenv("IM_TEST_PASSWORD", "Test123456")


def access_token(gate_url: str) -> str:
    response = requests.post(
        f"{gate_url}/oauth/token",
        data={
            "grant_type": "password",
            "username": EMAIL,
            "password": PASSWORD,
            "scope": "im.friend im.message",
        },
        timeout=5,
    )
    assert response.status_code == 200, response.text
    return response.json()["access_token"]


def test_access_token_revocation_is_immediate(gate_url: str):
    token = access_token(gate_url)
    headers = {"Authorization": f"Bearer {token}"}

    before = requests.post(
        f"{gate_url}/get_my_friends",
        json={"uid": 1},
        headers=headers,
        timeout=5,
    )
    assert before.status_code == 200, before.text

    revoked = requests.post(
        f"{gate_url}/oauth/revoke",
        data={"token": token},
        timeout=5,
    )
    assert revoked.status_code == 200, revoked.text

    after = requests.post(
        f"{gate_url}/get_my_friends",
        json={"uid": 1},
        headers=headers,
        timeout=5,
    )
    assert after.status_code == 401
    assert after.json()["error"] == "invalid_token"


def test_verification_code_is_one_time(gate_url: str):
    import redis

    email = f"one-time-{uuid.uuid4().hex[:8]}@example.com"
    client = redis.Redis(
        host=os.getenv("IM_REDIS_HOST", "127.0.0.1"),
        port=int(os.getenv("IM_REDIS_PORT", "16380")),
        password=os.getenv("IM_REDIS_PASSWORD", "test"),
        decode_responses=True,
    )
    client.set(f"code_{email}", "123456", ex=180)

    payload = {
        "user": "one-time-user",
        "email": email,
        "passwd": PASSWORD,
        "confirm": PASSWORD,
        "verifycode": "123456",
    }
    first = requests.post(f"{gate_url}/user_register", json=payload, timeout=5)
    assert first.status_code == 200, first.text

    second = requests.post(f"{gate_url}/user_register", json=payload, timeout=5)
    assert second.status_code == 200, second.text
    assert second.json()["error"] == 1003  # VerifyExpired: code consumed


def test_code_attempts_are_rate_limited(gate_url: str):
    email = f"rate-limit-{uuid.uuid4().hex[:8]}@example.com"
    payload = {
        "user": "rate-limit-user",
        "email": email,
        "passwd": PASSWORD,
        "confirm": PASSWORD,
        "verifycode": "000000",
    }

    for _ in range(5):
        response = requests.post(f"{gate_url}/user_register", json=payload, timeout=5)
        assert response.status_code == 200, response.text

    blocked = requests.post(f"{gate_url}/user_register", json=payload, timeout=5)
    assert blocked.status_code == 429
    assert blocked.json()["error"] == 1013

def test_plaintext_password_still_logs_in(gate_url: str):
    response = requests.post(
        f"{gate_url}/user_login",
        json={"user": EMAIL, "passwd": PASSWORD},
        timeout=5,
    )
    assert response.status_code == 200, response.text
    data = response.json()
    assert data["error"] == 0, data
    assert data.get("access_token"), data


def test_password_hash_is_not_accepted_as_password(gate_url: str):
    import hashlib

    digest = hashlib.sha256(PASSWORD.encode()).hexdigest()
    response = requests.post(
        f"{gate_url}/user_login",
        json={"user": EMAIL, "passwd": digest},
        timeout=5,
    )
    assert response.status_code == 200, response.text
    data = response.json()
    assert data["error"] != 0, data