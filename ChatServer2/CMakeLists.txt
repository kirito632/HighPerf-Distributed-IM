# 获取真实的源代码路径
set(CHATSERVER2_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ChatServer)

# 收集源码
file(GLOB CHAT2_SRCS
    ${CHATSERVER2_SRC_DIR}/*.cpp
    ${CHATSERVER2_SRC_DIR}/*.cc
    ${CHATSERVER2_SRC_DIR}/*.cxx
)

# 可执行文件
add_executable(chat_server2 ${CHAT2_SRCS})

# 链接共用第三方库（来自顶层）
target_link_libraries(chat_server2 PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    Boost::system Boost::filesystem
)

if(HIREDIS_TARGET)
    target_link_libraries(chat_server2 PRIVATE ${HIREDIS_TARGET})
endif()

if(JSONCPP_TARGET)
    target_link_libraries(chat_server2 PRIVATE ${JSONCPP_TARGET})
endif()

if(MYSQLCPP_TARGET)
    target_link_libraries(chat_server2 PRIVATE ${MYSQLCPP_TARGET})
elseif(MYSQLCPP_FALLBACK_LIB)
    target_link_libraries(chat_server2 PRIVATE ${MYSQLCPP_FALLBACK_LIB})
endif()

if(WIN32)
    target_link_libraries(chat_server PRIVATE ws2_32)
endif()