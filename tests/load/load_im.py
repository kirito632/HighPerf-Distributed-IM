#!/usr/bin/env python3
"""IM load generator (chat data plane, Gate HTTP API, delivery latency).

The script talks the real wire protocol: it mints RS256 access tokens with the
test key, logs in over TCP, sends 1017 text messages and waits for the 1018
ack. This makes it usable for capacity checks without going through the Gate
login rate limit (60 logins/min/IP).

Examples:

    # 50 TCP connections, 10 seconds, messages to uid 2
    python3 tests/load/load_im.py chat --connections 50 --duration 10 \
        --key .run-test/keys/jwt_private.pem

    # Gate HTTP API throughput using a bearer token
    python3 tests/load/load_im.py http --threads 16 --duration 10 \
        --key .run-test/keys/jwt_private.pem

    # end-to-end delivery latency (sender uid 1 -> receiver uid 2)
    python3 tests/load/load_im.py delivery --messages 200 \
        --key .run-test/keys/jwt_private.pem
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import statistics
import struct
import subprocess
import sys
import threading
import time
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone

import pymysql
import requests

MSG_CHAT_LOGIN = 1005
MSG_CHAT_LOGIN_RSP = 1006
ID_TEXT_CHAT_MSG_REQ = 1017
ID_TEXT_CHAT_MSG_RSP = 1018
ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019

DEFAULT_PEER_UID = 2
CLIENT_MSG_ID_LIMIT = 64


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def mint_token(private_key: str, uid: int, kid: str, ttl: int = 3600) -> str:
    now = int(time.time())
    header = {"alg": "RS256", "kid": kid, "typ": "JWT"}
    payload = {
        "iss": "im-gate",
        "aud": "im-backend",
        "sub": str(uid),
        "iat": now,
        "exp": now + ttl,
        "jti": uuid.uuid4().hex,
        "scope": "im.friend im.message",
    }
    signing_input = (
        b64url(json.dumps(header, separators=(",", ":")).encode())
        + "."
        + b64url(json.dumps(payload, separators=(",", ":")).encode())
    )
    signature = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", private_key],
        input=signing_input.encode(),
        check=True,
        capture_output=True,
    ).stdout
    return signing_input + "." + b64url(signature)


def ensure_users(count: int, start_uid: int, email_prefix: str,
                 password: str) -> None:
    connection = pymysql.connect(
        host=os.getenv("IM_DB_HOST", "127.0.0.1"),
        port=int(os.getenv("IM_DB_PORT", "13306")),
        user=os.getenv("IM_DB_USER", "chatuser"),
        password=os.getenv("IM_DB_PASSWORD", "test"),
        database=os.getenv("IM_DB_SCHEMA", "chat_system_test"),
        autocommit=True,
    )
    digest = hashlib.sha256(password.encode()).hexdigest()
    try:
        with connection.cursor() as cursor:
            rows = [
                (
                    start_uid + index,
                    f"load{start_uid + index}",
                    f"{email_prefix}{start_uid + index}@example.com",
                    digest,
                    f"load{start_uid + index}",
                )
                for index in range(count)
            ]
            cursor.executemany(
                "INSERT IGNORE INTO user (uid, name, email, pwd, nick) "
                "VALUES (%s, %s, %s, %s, %s)",
                rows,
            )
    finally:
        connection.close()


class PacketError(RuntimeError):
    pass


def send_packet(sock: socket.socket, msg_id: int, payload: dict) -> None:
    body = json.dumps(payload, separators=(",", ":")).encode()
    if len(body) > 64 * 1024:
        raise PacketError("payload too large")
    sock.sendall(struct.pack("!hhi", msg_id, 0, len(body)) + body)


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise PacketError("connection closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def recv_packet(sock: socket.socket) -> tuple[int, dict]:
    msg_id, _flags, length = struct.unpack("!hhi", recv_exact(sock, 8))
    if length < 0 or length > 64 * 1024:
        raise PacketError(f"bad length {length}")
    body = recv_exact(sock, length)
    return msg_id, json.loads(body.decode("utf-8", "replace"))


def chat_login(host: str, port: int, uid: int, token: str,
               timeout: float = 5.0) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    send_packet(sock, MSG_CHAT_LOGIN, {"uid": uid, "access_token": token})
    msg_id, response = recv_packet(sock)
    if msg_id != MSG_CHAT_LOGIN_RSP or response.get("error") != 0:
        sock.close()
        raise PacketError(f"login failed uid={uid}: {msg_id} {response}")
    return sock


def make_text_payload(uid: int, peer: int, content: str,
                      client_msg_id: str) -> dict:
    return {
        "fromuid": uid,
        "touid": peer,
        "client_msg_id": client_msg_id,
        "text_array": [{
            "content": content,
            "msgid": client_msg_id,
            "client_msg_id": client_msg_id,
        }],
    }


@dataclass
class Stats:
    sent: int = 0
    received: int = 0
    errors: dict[str, int] = field(default_factory=dict)
    latencies: list[float] = field(default_factory=list)
    lock: threading.Lock = field(default_factory=threading.Lock)

    def error(self, name: str) -> None:
        with self.lock:
            self.errors[name] = self.errors.get(name, 0) + 1

    def add_latency(self, value: float) -> None:
        if len(self.latencies) < 200_000:
            self.latencies.append(value)


def percentile(values: list[float], quantile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round(quantile * (len(ordered) - 1))))
    return ordered[index]


class ChatWorker(threading.Thread):
    def __init__(self, uid: int, token: str, host: str, port: int,
                 duration: float, stats: Stats, barrier: threading.Barrier,
                 peer: int, content: str):
        super().__init__(daemon=True)
        self.uid = uid
        self.token = token
        self.host = host
        self.port = port
        self.duration = duration
        self.stats = stats
        self.barrier = barrier
        self.peer = peer
        self.content = content
        self.sock: socket.socket | None = None
        self.ready = False

    def run(self) -> None:
        try:
            self.sock = chat_login(self.host, self.port, self.uid, self.token)
            self.ready = True
        except Exception as exc:  # noqa: BLE001
            self.stats.error(f"login:{type(exc).__name__}")
            self.barrier.wait(timeout=30)
            return

        try:
            self.barrier.wait(timeout=30)
        except threading.BrokenBarrierError:
            pass

        deadline = time.perf_counter() + self.duration
        while time.perf_counter() < deadline:
            client_msg_id = uuid.uuid4().hex
            payload = make_text_payload(self.uid, self.peer, self.content,
                                        client_msg_id)
            started = time.perf_counter()
            try:
                send_packet(self.sock, ID_TEXT_CHAT_MSG_REQ, payload)
                with self.stats.lock:
                    self.stats.sent += 1
                msg_id, response = recv_packet(self.sock)
                elapsed = time.perf_counter() - started
                if msg_id != ID_TEXT_CHAT_MSG_RSP or response.get("error") != 0:
                    self.stats.error(f"rsp:{msg_id}:{response.get('error')}")
                    continue
                with self.stats.lock:
                    self.stats.received += 1
                self.stats.add_latency(elapsed)
            except Exception as exc:  # noqa: BLE001
                self.stats.error(f"send:{type(exc).__name__}")
                break

        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass


class HttpWorker(threading.Thread):
    def __init__(self, url: str, token: str, duration: float, stats: Stats,
                 barrier: threading.Barrier):
        super().__init__(daemon=True)
        self.url = url
        self.token = token
        self.duration = duration
        self.stats = stats
        self.barrier = barrier

    def run(self) -> None:
        session = requests.Session()
        headers = {"Authorization": f"Bearer {self.token}"}
        self.barrier.wait(timeout=30)
        deadline = time.perf_counter() + self.duration
        while time.perf_counter() < deadline:
            started = time.perf_counter()
            try:
                response = session.post(
                    f"{self.url}/get_my_friends",
                    json={"uid": 1},
                    headers=headers,
                    timeout=5,
                )
                elapsed = time.perf_counter() - started
                with self.stats.lock:
                    self.stats.sent += 1
                if response.status_code == 200:
                    with self.stats.lock:
                        self.stats.received += 1
                    self.stats.add_latency(elapsed)
                else:
                    self.stats.error(f"http:{response.status_code}")
            except Exception as exc:  # noqa: BLE001
                self.stats.error(f"http:{type(exc).__name__}")


def read_proc(pid: int) -> tuple[float, int]:
    with open(f"/proc/{pid}/stat", "r", encoding="utf-8") as handle:
        parts = handle.read().split()
    ticks = int(parts[13]) + int(parts[14])
    with open(f"/proc/{pid}/status", "r", encoding="utf-8") as handle:
        rss = 0
        for line in handle:
            if line.startswith("VmRSS:"):
                rss = int(line.split()[1]) * 1024
                break
    return ticks / os.sysconf("SC_CLK_TCK"), rss


class Sampler(threading.Thread):
    def __init__(self, pids: dict[str, int], interval: float = 0.5):
        super().__init__(daemon=True)
        self.pids = pids
        self.interval = interval
        self.running = True
        self.samples: dict[str, dict[str, list[float]]] = {
            name: {"cpu": [], "rss": []} for name in pids
        }
        self._last: dict[str, tuple[float, int]] = {}

    def run(self) -> None:
        for name, pid in self.pids.items():
            try:
                self._last[name] = read_proc(pid)
            except OSError:
                pass
        while self.running:
            time.sleep(self.interval)
            for name, pid in self.pids.items():
                try:
                    cpu_time, rss = read_proc(pid)
                except OSError:
                    continue
                previous = self._last.get(name)
                self._last[name] = (cpu_time, rss)
                if previous:
                    delta = cpu_time - previous[0]
                    self.samples[name]["cpu"].append(
                        100.0 * delta / self.interval)
                self.samples[name]["rss"].append(float(rss))

    def stop(self) -> dict:
        self.running = False
        self.join(timeout=2)
        summary = {}
        for name, values in self.samples.items():
            cpu = values["cpu"]
            rss = values["rss"]
            summary[name] = {
                "cpu_avg_pct": round(statistics.mean(cpu), 1) if cpu else 0,
                "cpu_max_pct": round(max(cpu), 1) if cpu else 0,
                "rss_mb": round(max(rss) / 1024 / 1024, 1) if rss else 0,
            }
        return summary


def run_mode(args) -> dict:
    if args.key is None:
        args.key = os.getenv("IM_JWT_KEY", ".run-test/keys/jwt_private.pem")

    if args.mode == "chat":
        ensure_users(args.connections, args.start_uid, "load", args.password)
        tokens = [
            mint_token(args.key, args.start_uid + index, args.kid)
            for index in range(args.connections)
        ]
        stats = Stats()
        barrier = threading.Barrier(args.connections)
        workers = [
            ChatWorker(args.start_uid + index, tokens[index], args.host,
                       args.port, args.duration, stats, barrier, args.peer,
                       args.content)
            for index in range(args.connections)
        ]
        sampler = Sampler(parse_pids(args.pids))
        sampler.start()
        started = time.perf_counter()
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join(timeout=args.duration + 30)
        elapsed = time.perf_counter() - started
        resources = sampler.stop()
        return summarize(stats, elapsed, args, resources,
                         connected=sum(1 for worker in workers if worker.ready))

    if args.mode == "http":
        token = mint_token(args.key, 1, args.kid)
        stats = Stats()
        barrier = threading.Barrier(args.threads)
        workers = [
            HttpWorker(args.gate_url, token, args.duration, stats, barrier)
            for _ in range(args.threads)
        ]
        sampler = Sampler(parse_pids(args.pids))
        sampler.start()
        started = time.perf_counter()
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join(timeout=args.duration + 30)
        elapsed = time.perf_counter() - started
        resources = sampler.stop()
        return summarize(stats, elapsed, args, resources, connected=args.threads)

    if args.mode == "delivery":
        return run_delivery(args)

    raise SystemExit(f"unknown mode {args.mode}")


def run_delivery(args) -> dict:
    sender_token = mint_token(args.key, 1, args.kid)
    receiver_token = mint_token(args.key, 2, args.kid)
    sender = chat_login(args.host, args.port, 1, sender_token)
    receiver = chat_login(args.host, args.port, 2, receiver_token)
    # Delivery can be idle for a while when the outbox publisher is catching
    # up; a socket timeout here would silently stop the measurement.
    receiver.settimeout(None)

    sent_at: list[float] = []
    received_at: list[float] = []
    lock = threading.Lock()
    errors = 0
    ready = threading.Event()
    stop = threading.Event()

    def receive_loop() -> None:
        ready.set()
        while not stop.is_set():
            try:
                msg_id, _payload = recv_packet(receiver)
            except socket.timeout:
                continue
            except Exception:  # noqa: BLE001
                return
            if msg_id == ID_NOTIFY_TEXT_CHAT_MSG_REQ:
                with lock:
                    received_at.append(time.perf_counter())

    receiver_thread = threading.Thread(target=receive_loop, daemon=True)
    receiver_thread.start()
    ready.wait(timeout=5)

    for index in range(args.messages):
        client_msg_id = uuid.uuid4().hex
        payload = make_text_payload(1, 2, f"delivery-{index}", client_msg_id)
        try:
            with lock:
                sent_at.append(time.perf_counter())
            send_packet(sender, ID_TEXT_CHAT_MSG_REQ, payload)
            msg_id, response = recv_packet(sender)
            if msg_id != ID_TEXT_CHAT_MSG_RSP or response.get("error") != 0:
                errors += 1
        except Exception:  # noqa: BLE001
            errors += 1
            break

    deadline = time.time() + 15
    while time.time() < deadline:
        with lock:
            if len(received_at) >= len(sent_at):
                break
        time.sleep(0.05)
    stop.set()
    receiver_thread.join(timeout=2)
    sender.close()
    receiver.close()

    with lock:
        pairs = list(zip(sent_at, received_at))
    latencies = [received - sent for sent, received in pairs]
    elapsed = (sent_at[-1] - sent_at[0]) if len(sent_at) > 1 else 0.0

    return {
        "mode": "delivery",
        "messages": args.messages,
        "ack_errors": errors,
        "received": len(received_at),
        "send_elapsed_s": round(elapsed, 3),
        "send_rate_per_s": round((len(sent_at) - 1) / elapsed, 1)
        if elapsed > 0 else 0,
        "delivery_loss": max(0, len(sent_at) - len(received_at)),
        "delivery_p50_ms": round(percentile(latencies, 0.50) * 1000, 2),
        "delivery_p95_ms": round(percentile(latencies, 0.95) * 1000, 2),
        "delivery_p99_ms": round(percentile(latencies, 0.99) * 1000, 2),
        "delivery_mean_ms": round(statistics.mean(latencies) * 1000, 2)
        if latencies else 0,
    }


def summarize(stats: Stats, elapsed: float, args, resources: dict,
              connected: int) -> dict:
    latencies = stats.latencies
    return {
        "mode": args.mode,
        "concurrency": args.connections if args.mode == "chat" else args.threads,
        "duration_s": round(elapsed, 2),
        "connected": connected,
        "sent": stats.sent,
        "received": stats.received,
        "errors": stats.errors,
        "throughput_per_s": round(stats.received / elapsed, 1),
        "latency_p50_ms": round(percentile(latencies, 0.50) * 1000, 2),
        "latency_p95_ms": round(percentile(latencies, 0.95) * 1000, 2),
        "latency_p99_ms": round(percentile(latencies, 0.99) * 1000, 2),
        "latency_max_ms": round(max(latencies) * 1000, 2) if latencies else 0,
        "resources": resources,
    }


def parse_pids(value: str) -> dict[str, int]:
    result = {}
    for entry in (value or "").split(","):
        if not entry.strip():
            continue
        name, _, pid = entry.partition("=")
        if pid:
            result[name.strip()] = int(pid)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["chat", "http", "delivery"])
    parser.add_argument("--connections", type=int, default=10)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--duration", type=float, default=10.0)
    parser.add_argument("--messages", type=int, default=200)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18090)
    parser.add_argument("--gate-url", default="http://127.0.0.1:18080")
    parser.add_argument("--key", default=None)
    parser.add_argument("--kid", default="test-key-1")
    parser.add_argument("--start-uid", type=int, default=1001)
    parser.add_argument("--peer", type=int, default=DEFAULT_PEER_UID)
    parser.add_argument("--password", default="LoadTest123!")
    parser.add_argument("--content", default="load-test-message")
    parser.add_argument("--pids", default="")
    parser.add_argument("--json", action="store_true")

    args = parser.parse_args()
    result = run_mode(args)
    result["timestamp"] = datetime.now(timezone.utc).isoformat()
    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    else:
        print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
