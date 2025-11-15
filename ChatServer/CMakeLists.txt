# 获取真实的源代码路径
set(CHATSERVER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ChatServer)

# 收集源码
file(GLOB CHAT_SRCS
    ${CHATSERVER_SRC_DIR}/*.cpp
    ${CHATSERVER_SRC_DIR}/*.cc
    ${CHATSERVER_SRC_DIR}/*.cxx
)

# 可执行文件
add_executable(chat_server ${CHAT_SRCS})

# 链接共用第三方库（来自顶层）
target_link_libraries(chat_server PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    Boost::system Boost::filesystem
)

if(HIREDIS_TARGET)
    target_link_libraries(chat_server PRIVATE ${HIREDIS_TARGET})
endif()

if(JSONCPP_TARGET)
    target_link_libraries(chat_server PRIVATE ${JSONCPP_TARGET})
endif()

if(MYSQLCPP_TARGET)
    target_link_libraries(chat_server PRIVATE ${MYSQLCPP_TARGET})
elseif(MYSQLCPP_FALLBACK_LIB)
    target_link_libraries(chat_server PRIVATE ${MYSQLCPP_FALLBACK_LIB})
endif()

if(WIN32)
    target_link_libraries(chat_server PRIVATE ws2_32)
endif()