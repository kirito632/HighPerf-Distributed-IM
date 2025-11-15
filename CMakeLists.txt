cmake_minimum_required(VERSION 3.16)
project(FullStackProject LANGUAGES CXX)

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
