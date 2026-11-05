BUILD_DIR ?= build_test
COMPOSE_FILE := docker-compose.test.yml

.PHONY: configure test-unit test-up test-services-up test-services-down seed test-e2e test-down test-all ci clean

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON

test-unit: configure
	cmake --build $(BUILD_DIR) --target im_unit_tests -j$$(nproc)
	ctest --test-dir $(BUILD_DIR) --output-on-failure

test-up:
	bash scripts/dev/up_test_env.sh

test-services-up:
	bash scripts/dev/run_test_services.sh

test-services-down:
	bash scripts/dev/stop_test_services.sh

seed:
	bash scripts/dev/seed_test_data.sh

test-e2e:
	IM_E2E=1 python3 -m pytest tests/integration -v

test-down:
	bash scripts/dev/down_test_env.sh

test-all: test-unit test-e2e

ci:
	bash scripts/ci/run_ci.sh

clean:
	rm -rf $(BUILD_DIR)
