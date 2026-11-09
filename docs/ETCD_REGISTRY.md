# etcd service registry

Chat servers register themselves in etcd with a lease; StatusServer reads the
registry on every `GetChatServer` call and only hands out live instances. The
integration is optional (`EtcdEnabled`, default `false`) and falls back to the
static `[chatservers]` config when etcd is disabled or unreachable.

```text
ChatServer ──PUT im/services/chatserver/<name> (lease 10s)──> etcd
     ▲  keepalive every 3s                                   │
     │                                                       │ range
StatusServer ──GET /v3/kv/range im/services/chatserver/ ─────┘
```

## Components

- `common/registry/EtcdRegistry.{h,cpp}` — minimal etcd v3 client over the
  HTTP JSON gateway (libcurl): lease grant/keepalive/revoke, kv put/range,
  range delete, base64 helpers. No etcd gRPC dependency.
- `common/registry` is built as `im_registry`; `CURL::libcurl` is a public
  dependency.
- `tools/registry_probe.cpp` — `register` / `discover` helper for tests and
  demos.
- `ChatServer` / `ChatServer2` register `chatserver/<SelfServer.Name>` with a
  10s lease and revoke it in the shutdown path.
- `StatusServer::getChatServer` discovers `chatserver` instances and applies
  the existing least-connections choice per instance; if discovery returns
  nothing it falls back to the config list.

## Configuration

```ini
[Feature]
EtcdEnabled = false

[Etcd]
Endpoints = http://127.0.0.1:12379
```

Environment overrides (used by the test runner so the checked-in config stays
usable without etcd):

```bash
IM_ETCD_ENABLED=1
IM_ETCD_ENDPOINT=http://127.0.0.1:12379
```

## Run

```bash
docker compose -f docker-compose.test.yml --profile governance up -d etcd-test
IM_ETCD_ENABLED=1 BIN_DIR=$PWD/build_test/bin bash scripts/dev/run_test_services.sh

build_test/bin/registry_probe discover --service chatserver
# chatserver1 127.0.0.1 18090
# count=1
```

`registry_probe register --service chatserver --id demo --host 127.0.0.1
--port 19999 --ttl 30 --hold 5` registers an instance, keeps its lease alive
for five seconds and revokes it on exit, which is how the lease-expiry and
deregistration behaviour is tested.

## Test

```bash
IM_ETCD_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration/test_etcd_registry.py -q
```

Covered:

- the running ChatServer appears under `im/services/chatserver/` with its
  advertised host/port (18090);
- StatusServer's gRPC reply carries the same host/port from the registry;
- a probe instance is discoverable while it holds its lease and disappears
  immediately after `revoke`, even though its TTL is 30s.

Last verified 2026-11-09:

```text
39 unit tests passed
37 integration tests passed, 1 skipped (Kafka opt-in)
etcd: registry entry chatserver1 -> 127.0.0.1:18090
status: [getChatServer] etcd discovered 1 chatserver instance(s)
```

## Limitations

- Discovery polls on each `GetChatServer` call; there is no watch stream. That
  keeps the client small and is cheap at the current call rate.
- Single endpoint, plain HTTP, no auth. Production should use multiple
  endpoints, TLS and etcd's auth/RBAC.
- The lease TTL (10s) bounds how long a crashed instance can remain visible;
  graceful shutdown revokes immediately.
