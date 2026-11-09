# OpenTelemetry tracing

Tracing is disabled by default. Build the services with the OpenTelemetry C++ SDK and official OTLP/HTTP protobuf exporter:

```bash
cmake -S . -B build_otel -DIM_WITH_OPENTELEMETRY=ON -DBUILD_TESTING=ON
cmake --build build_otel --target gate_server chat_server chat_server2 status_server im_unit_tests -j4
```

`opentelemetry-cpp` and `opentelemetry-proto` are fetched by CMake at configure time, so the machine needs access to `codeload.github.com`. Without `IM_WITH_OPENTELEMETRY=ON` the binary still compiles, but `im::tracing::StartSpan` returns `nullptr` and no `traceparent` is generated.

Apply `deploy/sql/007_outbox_trace_context.sql` to an existing database. Fresh test databases receive it through `docker-compose.test.yml`'s SQL init mount.

Start Jaeger and enable tracing:

```bash
docker compose -f docker-compose.test.yml --profile observability up -d jaeger-test
IM_TRACING_ENABLED=1 BIN_DIR=$PWD/build_otel/bin bash scripts/dev/run_test_services.sh
```

`IM_TRACING_ENABLED=1` overrides `[Feature] TracingEnabled` for Gate/Chat, which keeps the checked-in test config usable both with and without a collector. The default endpoint is `http://127.0.0.1:4318/v1/traces`; `OTEL_EXPORTER_OTLP_ENDPOINT` overrides the full traces endpoint. Jaeger UI is at `http://127.0.0.1:16686`.

The compose file uses the Jaeger v2 single-binary image (`jaegertracing/jaeger:2.6.0`), which exposes OTLP/HTTP on 4318, OTLP/gRPC on 4317 and the query UI on 16686 out of the box. The old `jaegertracing/all-in-one:1.62` image is no longer used: the Docker Hub mirrors configured on the test VM answer `denied` for that tag.

The Qt login request starts a W3C trace at Gate. The response context is carried through TCP login and chat messages, stored in the Outbox row, propagated in Kafka headers, then continued by the delivery consumer and receiver ACK. The HTTP login and later TCP actions are linked by the propagated context, not inferred from the user ID.

## Verification

The opt-in end-to-end suite asserts that the trace id survives every hop:

```bash
IM_TRACE_E2E=1 IM_GATE_URL=http://127.0.0.1:18080 \
  ~/venvs/im/bin/python -m pytest tests/integration/test_trace.py -q
```

`tests/integration/test_trace.py` covers:

- Gate returns a child `traceparent` with the caller's trace id and a different span id;
- the TCP login and chat send responses keep the trace id;
- the `outbox.traceparent` column stores the chat-send span;
- Jaeger contains `POST /user_login`, `tcp.login`, `tcp.chat.send`, `kafka.outbox.publish` and `kafka.delivery` spans for the same trace, from both `gate-server` and `chat-server`.

Last verified 2026-11-09 on the test VM:

```text
35 unit tests passed (2 tracing tests, SDK build)
31 integration tests passed, 1 skipped (Kafka roundtrip opt-in)
traceparent: 00-c203044f...f3e3 -> tcp.login -> tcp.chat.send -> outbox -> kafka.delivery
```

The Jaeger query API confirmed the trace with spans from `gate-server` and `chat-server`; the outbox row for the test message stored the same trace id.
