"""Nginx TLS gateway tests.

Opt-in because the suite needs the nginx-test container and certificates:

    bash scripts/dev/gen_nginx_certs.sh
    docker compose -f docker-compose.test.yml --profile gateway up -d nginx-test
    IM_NGINX_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration/test_nginx.py -q

The container terminates TLS on 18443, proxies to Gate on the host and applies
two rate-limit zones (5 r/s for credential endpoints, 20 r/s for the rest).
"""

from __future__ import annotations

import os
import uuid

import pytest
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

pytestmark = pytest.mark.skipif(
    os.getenv("IM_NGINX_E2E") != "1",
    reason="set IM_NGINX_E2E=1 and start the nginx-test compose service",
)

NGINX_URL = os.getenv("IM_NGINX_URL", "https://127.0.0.1:18443").rstrip("/")


def test_https_login_is_proxied_to_gate(test_credentials):
    email, password = test_credentials
    response = requests.post(
        f"{NGINX_URL}/user_login",
        json={"user": email, "passwd": password},
        verify=False,
        timeout=5,
    )
    assert response.status_code == 200, response.text
    data = response.json()
    assert data.get("error") == 0, data
    assert data.get("access_token"), data
    # nginx terminates TLS and replaces the upstream Server header.
    assert response.headers.get("Server", "").startswith("nginx"), response.headers


def test_traceparent_is_forwarded_through_tls(test_credentials):
    email, password = test_credentials
    trace_id = uuid.uuid4().hex
    parent = f"00-{trace_id}-{uuid.uuid4().hex[:16]}-01"

    response = requests.post(
        f"{NGINX_URL}/user_login",
        json={"user": email, "passwd": password},
        headers={"traceparent": parent},
        verify=False,
        timeout=5,
    )
    assert response.status_code == 200, response.text

    child = response.headers.get("traceparent")
    if not child:
        pytest.skip("tracing is disabled; start services with IM_TRACING_ENABLED=1")
    assert child.split("-")[1] == trace_id, child


def test_rate_limit_returns_429():
    codes = []
    for _ in range(300):
        response = requests.get(f"{NGINX_URL}/not-a-route", verify=False, timeout=2)
        codes.append(response.status_code)

    assert 429 in codes, f"nginx never rate-limited: {sorted(set(codes))}"
    assert set(codes) <= {404, 429}, sorted(set(codes))
