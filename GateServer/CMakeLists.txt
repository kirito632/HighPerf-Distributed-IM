# 获取真实的源代码路径
set(GATESERVER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/GateServer)

# 收集源码
file(GLOB GATE_SRCS
    ${GATESERVER_SRC_DIR}/*.cpp
    ${GATESERVER_SRC_DIR}/*.cc
    ${GATESERVER_SRC_DIR}/*.cxx
)

# 要排除的源文件
set(GATE_EXCLUDE_SRCS
    ${GATESERVER_SRC_DIR}/json_reader.cpp
    ${GATESERVER_SRC_DIR}/json_value.cpp
    ${GATESERVER_SRC_DIR}/json_writer.cpp
    ${GATESERVER_SRC_DIR}/connect_test.cpp   # 如果这个只是测试也不需要
)

list(REMOVE_ITEM GATE_SRCS ${GATE_EXCLUDE_SRCS})

# 可执行文件
add_executable(gate_server ${GATE_SRCS})

# 链接共用第三方库（来自顶层）
target_link_libraries(gate_server PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    Boost::system Boost::filesystem
)

if(HIREDIS_TARGET)
    target_link_libraries(gate_server PRIVATE ${HIREDIS_TARGET})
elseif(UNIX)
    # Linux 下直接链接系统的 hiredis 库
    target_link_libraries(gate_server PRIVATE hiredis)
endif()

if(JSONCPP_TARGET)
    target_link_libraries(gate_server PRIVATE ${JSONCPP_TARGET})
endif()

if(MYSQLCPP_TARGET)
    target_link_libraries(gate_server PRIVATE ${MYSQLCPP_TARGET})
elseif(MYSQLCPP_FALLBACK_LIB)
    target_link_libraries(gate_server PRIVATE ${MYSQLCPP_FALLBACK_LIB})
endif()

if(WIN32)
    target_link_libraries(gate_server PRIVATE ws2_32)
endif()