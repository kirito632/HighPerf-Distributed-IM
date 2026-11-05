cmake_minimum_required(VERSION 3.16)
project(FullStackProject LANGUAGES CXX)

option(BUILD_TESTING "Build the unit-test tree" OFF)
option(IM_WITH_OPENTELEMETRY "Build OpenTelemetry OTLP tracing support" OFF)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
option(BUILD_SHARED_LIBS "Build with shared libraries" ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

# Third-party deps (vcpkg-friendly)
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_package(Boost REQUIRED COMPONENTS system filesystem)

# hiredis
set(HIREDIS_TARGET "")
find_package(hiredis CONFIG QUIET)
if(NOT hiredis_FOUND)
  find_package(unofficial-hiredis CONFIG QUIET)
endif()
if(TARGET hiredis::hiredis)
  set(HIREDIS_TARGET hiredis::hiredis)
elseif(TARGET hiredis)
  set(HIREDIS_TARGET hiredis)
elseif(TARGET unofficial::hiredis::hiredis)
  set(HIREDIS_TARGET unofficial::hiredis::hiredis)
endif()

# jsoncpp (optional, don't link in GateServer because it embeds sources)
set(JSONCPP_TARGET "")
find_package(jsoncpp CONFIG QUIET)
if(jsoncpp_FOUND)
  if(TARGET JsonCpp::JsonCpp)
    set(JSONCPP_TARGET JsonCpp::JsonCpp)
  endif()
endif()

# MySQL Connector/C++
set(MYSQLCPP_TARGET "")
find_package(MySQLConnectorCPP CONFIG QUIET)
if(MySQLConnectorCPP_FOUND)
  if(TARGET MySQL::ConnectorCpp)
    set(MYSQLCPP_TARGET MySQL::ConnectorCpp)
  endif()
endif()
if(NOT MYSQLCPP_TARGET)
  find_package(mysql-connector-cpp CONFIG QUIET)
  if(TARGET mysql-connector-cpp)
    set(MYSQLCPP_TARGET mysql-connector-cpp)
  elseif(TARGET MySQL::mysqlconnectorcpp)
    set(MYSQLCPP_TARGET MySQL::mysqlconnectorcpp)
  endif()
endif()
if(NOT MYSQLCPP_TARGET)
  find_package(unofficial-mysql-connector-cpp CONFIG QUIET)
  if(TARGET unofficial::mysql-connector-cpp::mysql-connector-cpp)
    set(MYSQLCPP_TARGET unofficial::mysql-connector-cpp::mysql-connector-cpp)
  endif()
endif()
if(NOT MYSQLCPP_TARGET)
  set(MYSQLCPP_LIB_NAMES mysqlcppconn mysqlcppconn8)
  foreach(_lib ${MYSQLCPP_LIB_NAMES})
    find_library(MYSQLCPP_FALLBACK_LIB NAMES ${_lib})
    if(MYSQLCPP_FALLBACK_LIB)
      break()
    endif()
  endforeach()
endif()

# JWT/JWKS/refresh-token core is needed by GateServer at runtime, not only
# by tests, so it is built unconditionally.
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

FetchContent_Declare(
  jwtcpp
  URL https://codeload.github.com/Thalhammer/jwt-cpp/tar.gz/refs/tags/v0.7.0
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(jwtcpp)

add_library(im_auth STATIC
  common/auth/JwtKeys.cpp
  common/auth/JwtSigner.cpp
  common/auth/JwtVerifier.cpp
  common/auth/Jwks.cpp
  common/auth/TokenService.cpp
)
target_include_directories(im_auth PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common/auth
)
target_link_libraries(im_auth PUBLIC
  OpenSSL::Crypto
  jwt-cpp::jwt-cpp
)

# Lightweight Prometheus text exporter (no external dependency).
add_library(im_metrics STATIC
  common/metrics/Metrics.cpp
  common/metrics/MetricsServer.cpp
)
target_include_directories(im_metrics PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common/metrics
)
target_link_libraries(im_metrics PUBLIC
  Boost::system
  Threads::Threads
)

add_library(im_tracing STATIC common/tracing/Tracing.cpp)
target_include_directories(im_tracing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common/tracing)
target_link_libraries(im_tracing PUBLIC Threads::Threads)
if(IM_WITH_OPENTELEMETRY)
  set(IM_BUILD_TESTING ${BUILD_TESTING})
  set(WITH_OTLP_GRPC OFF CACHE BOOL "" FORCE)
  set(WITH_OTLP_HTTP ON CACHE BOOL "" FORCE)
  set(WITH_HTTP_CLIENT_CURL ON CACHE BOOL "" FORCE)
  set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(WITH_TESTING OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(WITH_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(WITH_LOGS_PREVIEW OFF CACHE BOOL "" FORCE)
  set(WITH_METRICS_PREVIEW OFF CACHE BOOL "" FORCE)
  set(WITH_NO_DEPRECATED_CODE ON CACHE BOOL "" FORCE)
  set(WITH_STL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    opentelemetry_proto
    URL https://codeload.github.com/open-telemetry/opentelemetry-proto/tar.gz/refs/tags/v1.4.0
    URL_HASH SHA256=53cd32cedb27762ea2060a9c8d83e4b822de13d73b5d5d37a2db3cf55018d694
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(opentelemetry_proto)
  set(OTELCPP_PROTO_PATH ${opentelemetry_proto_SOURCE_DIR})
  FetchContent_Declare(
    opentelemetry_cpp
    URL https://codeload.github.com/open-telemetry/opentelemetry-cpp/tar.gz/refs/tags/v1.19.0
    URL_HASH SHA256=e0330194b72f2fe4c0ce3ece06b02dc4aa0ab491eb75bf42c6f5e283912e468c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(opentelemetry_cpp)
  set(BUILD_TESTING ${IM_BUILD_TESTING})
  target_compile_definitions(im_tracing PUBLIC IM_WITH_OPENTELEMETRY=1)
  target_link_libraries(im_tracing PUBLIC
    opentelemetry_trace
    opentelemetry_sdk
    opentelemetry_exporter_otlp_http
  )
endif()

# Kafka wrapper (librdkafka via pkg-config); optional so Kafka can stay off.
add_subdirectory(common/mq)

# Kafka smoke tool: produce/consume one message against the test broker.
if(TARGET im_mq)
  add_executable(mq_smoke tools/mq_smoke.cpp)
  target_link_libraries(mq_smoke PRIVATE im_mq)
endif()

# Unit tests. GoogleTest is fetched only when BUILD_TESTING is enabled so
# production builds do not pay the googleTest download/compile cost.
if(BUILD_TESTING)
  FetchContent_Declare(
    googletest
    URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.14.0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_subdirectory(tests)
endif()

add_subdirectory(ChatServer)
add_subdirectory(ChatServer2)
add_subdirectory(GateServer)
add_subdirectory(StatusServer)

# function(collect_sources DIR OUT_VAR)
#   file(GLOB _SRCS "${DIR}/*.cpp" "${DIR}/*.cc" "${DIR}/*.cxx")
#   set(${OUT_VAR} ${_SRCS} PARENT_SCOPE)
# endfunction()

# set(CHAT_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/ChatServer/ChatServer)
# set(GATE_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/GateServer/GateServer)
# set(STATUS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/StatusServer/StatusServer)

# # ChatServer
# collect_sources(${CHAT_DIR} CHAT_SRCS)
# add_executable(chat_server ${CHAT_SRCS})
# target_include_directories(chat_server PRIVATE ${CHAT_DIR} ${Boost_INCLUDE_DIRS})
# target_link_libraries(chat_server PRIVATE
#   gRPC::grpc++
#   protobuf::libprotobuf
#   OpenSSL::SSL
#   OpenSSL::Crypto
#   Threads::Threads
# )
# if(HIREDIS_TARGET)
#   target_link_libraries(chat_server PRIVATE ${HIREDIS_TARGET})
# endif()
# if(JSONCPP_TARGET)
#   target_link_libraries(chat_server PRIVATE ${JSONCPP_TARGET})
# endif()
# if(MYSQLCPP_TARGET)
#   target_link_libraries(chat_server PRIVATE ${MYSQLCPP_TARGET})
# elseif(MYSQLCPP_FALLBACK_LIB)
#   target_link_libraries(chat_server PRIVATE ${MYSQLCPP_FALLBACK_LIB})
#   if(DEFINED MYSQLCPP_INCLUDE_DIR)
#     target_include_directories(chat_server PRIVATE ${MYSQLCPP_INCLUDE_DIR})
#   endif()
# endif()
# if(WIN32)
#   target_link_libraries(chat_server PRIVATE ws2_32)
# endif()

# # GateServer
# collect_sources(${GATE_DIR} GATE_SRCS)
# add_executable(gate_server ${GATE_SRCS})
# target_include_directories(gate_server PRIVATE ${GATE_DIR} ${Boost_INCLUDE_DIRS})
# target_link_libraries(gate_server PRIVATE
#   gRPC::grpc++
#   protobuf::libprotobuf
#   OpenSSL::SSL
#   OpenSSL::Crypto
#   Threads::Threads
# )
# if(HIREDIS_TARGET)
#   target_link_libraries(gate_server PRIVATE ${HIREDIS_TARGET})
# endif()
# # Do NOT link JsonCpp here; sources are embedded.
# if(MYSQLCPP_TARGET)
#   target_link_libraries(gate_server PRIVATE ${MYSQLCPP_TARGET})
# elseif(MYSQLCPP_FALLBACK_LIB)
#   target_link_libraries(gate_server PRIVATE ${MYSQLCPP_FALLBACK_LIB})
#   if(DEFINED MYSQLCPP_INCLUDE_DIR)
#     target_include_directories(gate_server PRIVATE ${MYSQLCPP_INCLUDE_DIR})
#   endif()
# endif()
# if(WIN32)
#   target_link_libraries(gate_server PRIVATE ws2_32)
# endif()

# # StatusServer
# collect_sources(${STATUS_DIR} STATUS_SRCS)
# add_executable(status_server ${STATUS_SRCS})
# target_include_directories(status_server PRIVATE ${STATUS_DIR} ${Boost_INCLUDE_DIRS})
# target_link_libraries(status_server PRIVATE
#   gRPC::grpc++
#   protobuf::libprotobuf
#   OpenSSL::SSL
#   OpenSSL::Crypto
#   Threads::Threads
# )
# if(HIREDIS_TARGET)
#   target_link_libraries(status_server PRIVATE ${HIREDIS_TARGET})
# endif()
# if(JSONCPP_TARGET)
#   target_link_libraries(status_server PRIVATE ${JSONCPP_TARGET})
# endif()
# if(MYSQLCPP_TARGET)
#   target_link_libraries(status_server PRIVATE ${MYSQLCPP_TARGET})
# elseif(MYSQLCPP_FALLBACK_LIB)
#   target_link_libraries(status_server PRIVATE ${MYSQLCPP_FALLBACK_LIB})
#   if(DEFINED MYSQLCPP_INCLUDE_DIR)
#     target_include_directories(status_server PRIVATE ${MYSQLCPP_INCLUDE_DIR})
#   endif()
# endif()
# if(WIN32)
#   target_link_libraries(status_server PRIVATE ws2_32)
# endif()
