# Tests

## Unit tests

```bash
cmake -S . -B build_test -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build_test --target im_unit_tests -j"$(nproc)"
ctest --test-dir build_test --output-on-failure
```

GoogleTest is fetched by CMake (`FetchContent`) the first time; later builds reuse
the downloaded source in `build_test/_deps`. Production builds should pass
`-DBUILD_TESTING=OFF` to avoid the fetch/test dependency.

## End-to-end smoke test

The smoke test is intentionally skipped unless `IM_E2E=1` is set, so the default
CI does not require a running cluster.

1. Start infrastructure:

   ```bash
   make test-up
   make seed
   ```

2. Start StatusServer, ChatServer and GateServer with `config_test.ini`
   (see `scripts/dev/run_test_services.sh` for the expected local layout).

3. Run:

   ```bash
   IM_E2E=1 IM_GATE_URL=http://127.0.0.1:18080 \
     python3 -m pytest tests/integration -v
   ```

The test users are:

| uid | email | password |
|---:|---|---|
| 1 | `imtest1@example.com` | `Test123456` |
| 2 | `imtest2@example.com` | `Test123456` |

Override with `IM_TEST_EMAIL`, `IM_TEST_PASSWORD`, `IM_TEST_PEER_EMAIL`,
`IM_TEST_PEER_PASSWORD`, `IM_TEST_PEER_UID`.

## Test client

`tests/py/im_client.py` implements the GateServer HTTP login and the ChatServer
4-byte-header TCP protocol. It is used by smoke tests and will be extended for
the Step 2 idempotency/ACK tests.
