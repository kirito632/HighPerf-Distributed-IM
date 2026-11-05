from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent / "py"))

from im_client import ImClient  # noqa: E402

DEFAULT_EMAIL = os.getenv("IM_TEST_EMAIL", "imtest1@example.com")
DEFAULT_PASSWORD = os.getenv("IM_TEST_PASSWORD", "Test123456")
DEFAULT_PEER_EMAIL = os.getenv("IM_TEST_PEER_EMAIL", "imtest2@example.com")
DEFAULT_PEER_PASSWORD = os.getenv("IM_TEST_PEER_PASSWORD", "Test123456")
DEFAULT_PEER_UID = int(os.getenv("IM_TEST_PEER_UID", "2"))


@pytest.fixture(scope="session")
def gate_url() -> str:
    return os.getenv("IM_GATE_URL", "http://127.0.0.1:18080")


@pytest.fixture(scope="session")
def test_credentials() -> tuple[str, str]:
    return DEFAULT_EMAIL, DEFAULT_PASSWORD


@pytest.fixture(scope="session")
def peer_credentials() -> tuple[str, str]:
    return DEFAULT_PEER_EMAIL, DEFAULT_PEER_PASSWORD


@pytest.fixture()
def im_client(gate_url: str, test_credentials: tuple[str, str]):
    email, password = test_credentials
    client = ImClient(gate_url)
    client.login(email, password)
    try:
        yield client
    finally:
        client.close()


@pytest.fixture()
def peer_client(gate_url: str, peer_credentials: tuple[str, str]):
    email, password = peer_credentials
    client = ImClient(gate_url)
    client.login(email, password)
    try:
        yield client
    finally:
        client.close()


@pytest.fixture()
def peer_uid() -> int:
    return DEFAULT_PEER_UID


class TestDb:
    def __init__(self) -> None:
        self.host = os.getenv("IM_DB_HOST", "127.0.0.1")
        self.port = os.getenv("IM_DB_PORT", "13306")
        self.user = os.getenv("IM_DB_USER", "chatuser")
        self.password = os.getenv("IM_DB_PASSWORD", "test")
        self.schema = os.getenv("IM_DB_SCHEMA", "chat_system_test")

    def query(self, sql: str) -> list[list[str]]:
        result = subprocess.run(
            [
                "mysql",
                f"-h{self.host}",
                f"-P{self.port}",
                f"-u{self.user}",
                f"-p{self.password}",
                self.schema,
                "-N",
                "-B",
                "-e",
                sql,
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        return [line.split("\t") for line in result.stdout.splitlines() if line]


@pytest.fixture(scope="session")
def db() -> TestDb:
    return TestDb()
