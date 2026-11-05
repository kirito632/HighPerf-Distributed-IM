import os

import pytest
import requests

pytestmark = pytest.mark.skipif(
    os.getenv("IM_E2E") != "1",
    reason="set IM_E2E=1 and start GateServer with JWT enabled",
)

EMAIL = os.getenv("IM_TEST_EMAIL", "imtest1@example.com")
PASSWORD = os.getenv("IM_TEST_PASSWORD", "Test123456")


def _access_token(gate_url: str) -> str:
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


def test_friend_api_requires_bearer(gate_url: str):
    response = requests.post(
        f"{gate_url}/get_my_friends",
        json={"uid": 1},
        timeout=5,
    )
    assert response.status_code == 401
    assert response.json()["error"] == "invalid_token"


def test_bearer_token_reads_authenticated_user(gate_url: str):
    token = _access_token(gate_url)
    response = requests.post(
        f"{gate_url}/get_my_friends",
        json={"uid": 2},  # must be ignored: token belongs to uid=1
        headers={"Authorization": f"Bearer {token}"},
        timeout=5,
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["error"] == 0
    assert [friend["uid"] for friend in body["friends"]] == [2]


def test_cross_uid_reply_is_forbidden(gate_url: str):
    token = _access_token(gate_url)
    response = requests.post(
        f"{gate_url}/reply_friend_request",
        json={"from_uid": 2, "to_uid": 2, "agree": True},
        headers={"Authorization": f"Bearer {token}"},
        timeout=5,
    )
    assert response.status_code == 403
    assert response.json()["error"] == 1012


def test_tampered_bearer_is_rejected(gate_url: str):
    response = requests.post(
        f"{gate_url}/get_my_friends",
        json={"uid": 1},
        headers={"Authorization": "Bearer not-a-jwt"},
        timeout=5,
    )
    assert response.status_code == 401
