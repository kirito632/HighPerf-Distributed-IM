# OpenTelemetry tracing

Tracing is disabled by default. Build the services with the OpenTelemetry C++ SDK and official OTLP/HTTP protobuf exporter:

```bash
cmake -S . -B build_otel -DIM_WITH_OPENTELEMETRY=ON
cmake --build build_otel --target gate_server chat_server chat_server2
```

Apply `deploy/sql/007_outbox_trace_context.sql` to an existing database. Fresh test databases receive it through `docker-compose.test.yml`'s SQL init mount.

Start Jaeger and enable tracing in each service config:

```bash
docker compose -f docker-compose.test.yml --profile observability up -d jaeger-test
```

Set `[Feature] TracingEnabled = true`; the default endpoint is `http://127.0.0.1:4318/v1/traces`. `OTEL_EXPORTER_OTLP_ENDPOINT` overrides the full traces endpoint. Jaeger UI is at `http://127.0.0.1:16686`.

The Qt login request starts a W3C trace at Gate. The response context is carried through TCP login and chat messages, stored in the Outbox row, propagated in Kafka headers, then continued by the delivery consumer and receiver ACK. The HTTP login and later TCP actions are linked by the propagated context, not inferred from the user ID.
