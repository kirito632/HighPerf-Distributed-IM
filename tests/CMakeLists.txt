include(GoogleTest)

# jsoncpp is vendored into GateServer historically and not installed under
# /usr/include/json/json.h on every distro; locate the distro include dir.
find_path(JSONCPP_INCLUDE_DIR
    NAMES json/json.h
    HINTS /usr/include/jsoncpp /usr/local/include/jsoncpp
)

find_program(OPENSSL_EXECUTABLE openssl REQUIRED)
set(JWT_TEST_KEY_DIR ${CMAKE_BINARY_DIR}/jwt_test_keys)
set(JWT_TEST_PRIVATE_KEY ${JWT_TEST_KEY_DIR}/private.pem)
set(JWT_TEST_PUBLIC_KEY ${JWT_TEST_KEY_DIR}/public.pem)

add_custom_command(
    OUTPUT ${JWT_TEST_PRIVATE_KEY} ${JWT_TEST_PUBLIC_KEY}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${JWT_TEST_KEY_DIR}
    COMMAND ${OPENSSL_EXECUTABLE} genpkey -algorithm RSA
            -pkeyopt rsa_keygen_bits:2048 -out ${JWT_TEST_PRIVATE_KEY}
    COMMAND ${OPENSSL_EXECUTABLE} pkey -in ${JWT_TEST_PRIVATE_KEY}
            -pubout -out ${JWT_TEST_PUBLIC_KEY}
    COMMENT "Generating ephemeral JWT test keys"
    VERBATIM
)

add_custom_target(jwt_test_keys DEPENDS ${JWT_TEST_PRIVATE_KEY} ${JWT_TEST_PUBLIC_KEY})

add_executable(im_unit_tests
    cpp/unit/test_msg_node.cpp
    cpp/unit/test_crypto.cpp
    cpp/unit/test_section_info.cpp
    cpp/unit/test_jwt.cpp
    cpp/unit/test_jwks.cpp
    cpp/unit/test_token_service.cpp
    cpp/unit/test_tracing.cpp
    cpp/unit/test_registry.cpp
    ${CMAKE_SOURCE_DIR}/ChatServer/ChatServer/MsgNode.cpp
    ${CMAKE_SOURCE_DIR}/ChatServer/ChatServer/crypto_utils.cpp
)

add_dependencies(im_unit_tests jwt_test_keys)

target_include_directories(im_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/ChatServer/ChatServer
    ${CMAKE_SOURCE_DIR}/common/auth
    ${CMAKE_SOURCE_DIR}/common/tracing
    ${CMAKE_SOURCE_DIR}/common/registry
    ${CMAKE_SOURCE_DIR}
    ${JSONCPP_INCLUDE_DIR}
)

target_compile_definitions(im_unit_tests PRIVATE
    JWT_TEST_KEY_DIR="${JWT_TEST_KEY_DIR}"
)

target_link_libraries(im_unit_tests PRIVATE
    GTest::gtest_main
    im_auth
    im_tracing
    im_registry
)

if(TARGET im_mq)
    target_sources(im_unit_tests PRIVATE cpp/unit/test_event_envelope.cpp)
    target_include_directories(im_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/common/mq)
    target_link_libraries(im_unit_tests PRIVATE im_mq)
endif()

if(IM_WITH_OPENTELEMETRY)
    target_compile_definitions(im_unit_tests PRIVATE IM_WITH_OPENTELEMETRY=1)
endif()

gtest_discover_tests(im_unit_tests
    DISCOVERY_TIMEOUT 30
)
