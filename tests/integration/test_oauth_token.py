import base64
import json
import os

import requests

import pytest

pytestmark = pytest.mark.skipif(
    os.getenv("IM_E2E") != "1",
    reason="set IM_E2E=1 and start GateServer with JWT enabled",
)

EMAIL = os.getenv("IM_TEST_EMAIL", "imtest1@example.com")
PASSWORD = os.getenv("IM_TEST_PASSWORD", "Test123456")


def b64url_decode(value: str) -> bytes:
    padding = "=" * (-len(value) % 4)
    return base64.urlsafe_b64decode(value + padding)


def decode_jwt(token: str) -> tuple[dict, dict]:
    header, payload, _signature = token.split(".")
    return json.loads(b64url_decode(header)), json.loads(b64url_decode(payload))


def password_grant(gate_url: str) -> dict:
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
    return response.json()


def test_password_grant_returns_jwt_and_refresh(gate_url: str):
    tokens = password_grant(gate_url)
    assert tokens["token_type"] == "Bearer"
    assert tokens["expires_in"] == 900
    assert tokens["refresh_expires_in"] == 604800
    assert tokens["uid"] == 1

    header, payload = decode_jwt(tokens["access_token"])
    assert header["alg"] == "RS256"
    assert header["kid"] == "test-key-1"
    assert payload["iss"] == "im-gate"
    assert payload["aud"] == "im-backend"
    assert payload["sub"] == "1"
    assert payload["scope"] == "im.friend im.message"


def test_jwks_matches_token_kid(gate_url: str):
    tokens = password_grant(gate_url)
    header, _ = decode_jwt(tokens["access_token"])

    response = requests.get(f"{gate_url}/.well-known/jwks.json", timeout=5)
    assert response.status_code == 200, response.text
    jwks = response.json()
    assert len(jwks["keys"]) == 1
    key = jwks["keys"][0]
    assert key["kty"] == "RSA"
    assert key["alg"] == "RS256"
    assert key["kid"] == header["kid"]
    assert key["n"]
    assert key["e"]


def test_refresh_rotation_and_reuse_detection(gate_url: str):
    first = password_grant(gate_url)

    refreshed = requests.post(
        f"{gate_url}/oauth/token",
        data={"grant_type": "refresh_token", "refresh_token": first["refresh_token"]},
        timeout=5,
    )
    assert refreshed.status_code == 200, refreshed.text
    second = refreshed.json()
    assert second["refresh_token"] != first["refresh_token"]
    assert second["uid"] == first["uid"]

    replay = requests.post(
        f"{gate_url}/oauth/token",
        data={"grant_type": "refresh_token", "refresh_token": first["refresh_token"]},
        timeout=5,
    )
    assert replay.status_code == 400
    assert replay.json()["error"] == "invalid_grant"

    # Reuse must revoke the whole family, including the rotated token.
    after_reuse = requests.post(
        f"{gate_url}/oauth/token",
        data={"grant_type": "refresh_token", "refresh_token": second["refresh_token"]},
        timeout=5,
    )
    assert after_reuse.status_code == 400


def test_user_login_returns_jwt_in_both_token_fields(gate_url: str):
    response = requests.post(
        f"{gate_url}/user_login",
        json={"user": EMAIL, "passwd": PASSWORD},
        timeout=5,
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["error"] == 0
    assert body["token"] == body["access_token"]
    assert body["refresh_token"]
    assert body["expires_in"] == 900
    header, _ = decode_jwt(body["token"])
    assert header["kid"] == "test-key-1"
