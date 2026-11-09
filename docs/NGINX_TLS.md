# Nginx TLS gateway

`nginx-test` is the edge for the local test stack. It terminates TLS on
`https://127.0.0.1:18443`, proxies HTTP to Gate (`host.docker.internal:18080`)
and applies two per-IP rate-limit zones.

```text
client --TLS--> nginx:443 (18443) --HTTP--> Gate:18080 --gRPC--> Status
                                          \--TCP--> Chat (unchanged, direct)
```

## Files

- `deploy/nginx/nginx.conf` â€?TLS server, upstream, rate limits, trace-aware access log
- `scripts/dev/gen_nginx_certs.sh` â€?self-signed certificate generator (`deploy/nginx/certs/`, gitignored)
- `docker-compose.test.yml` â€?`nginx-test` service, profile `gateway`
- `tests/integration/test_nginx.py` â€?opt-in HTTPS/rate-limit/trace tests

## Run

```bash
bash scripts/dev/gen_nginx_certs.sh
docker compose -f docker-compose.test.yml --profile gateway up -d nginx-test
curl -k https://127.0.0.1:18443/user_login \
  -H 'Content-Type: application/json' \
  -d '{"user":"imtest1@example.com","passwd":"Test123456"}'
```

Gate must be running on the host (see `scripts/dev/run_test_services.sh`).
The container entry for `host.docker.internal` is provided by `extra_hosts`, so
this works on Linux as well as Docker Desktop.

## Configuration choices

- **TLS**: TLS 1.2/1.3 only, HTTP/2 enabled. The certificate is self-signed
  (`CN=localhost`, SAN `DNS:localhost,IP:127.0.0.1`); tests use `verify=False`
  and production must mount a real certificate chain.
- **Rate limits**: `login_zone` 5 r/s (burst 5) for `/user_login` and
  `/oauth/token`; `api_zone` 20 r/s (burst 20) for everything else. Both answer
  `429`, which is easier to assert and document than nginx's default `503`.
- **Proxy headers**: `X-Real-IP`, `X-Forwarded-For`, `X-Forwarded-Proto: https`
  and `Connection: ""` (required for upstream keepalive).
- **Trace context**: the access log records `trace=$http_traceparent`, and the
  `traceparent` header reaches Gate unchanged, so a request can be followed
  from nginx access log to the Jaeger trace.

## Test

```bash
IM_NGINX_E2E=1 ~/venvs/im/bin/python -m pytest tests/integration/test_nginx.py -q
```

Last verified 2026-11-09:

```text
3 passed
- HTTPS login proxied to Gate (nginx terminates TLS, Gate returns the JWT)
- inbound traceparent reaches Gate and the child trace id is returned
- 120 rapid requests hit the api zone and produce 429
```

## Limitations

- The rate limit is per source IP; clients behind one NAT share a bucket.
- Chat TCP (18090/18091) is not proxied by nginx; the HTTP API and chat data
  plane are terminated separately in this test setup.
- Certificates are generated outside the repo and must be rotated manually.
