# 获取真实的源代码路径
set(CHATSERVER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ChatServer)

# 收集源码
file(GLOB CHAT_SRCS
    ${CHATSERVER_SRC_DIR}/*.cpp
    ${CHATSERVER_SRC_DIR}/*.cc
    ${CHATSERVER_SRC_DIR}/*.cxx
)

# Outbox publisher depends on librdkafka; keep it optional.
list(REMOVE_ITEM CHAT_SRCS ${CHATSERVER_SRC_DIR}/OutboxPublisher.cpp ${CHATSERVER_SRC_DIR}/DeliveryConsumer.cpp)
if(TARGET im_mq)
    list(APPEND CHAT_SRCS ${CHATSERVER_SRC_DIR}/OutboxPublisher.cpp ${CHATSERVER_SRC_DIR}/DeliveryConsumer.cpp)
endif()

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
    im_auth
    im_metrics
    im_tracing
    im_registry
)

if(TARGET im_mq)
    target_link_libraries(chat_server PRIVATE im_mq)
    target_compile_definitions(chat_server PRIVATE IM_WITH_KAFKA=1)
endif()

if(HIREDIS_TARGET)
    target_link_libraries(chat_server PRIVATE ${HIREDIS_TARGET})
elseif(UNIX)
    # Linux 下直接链接系统的 hiredis 库
    target_link_libraries(chat_server PRIVATE hiredis)
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

# curl (used by AIAgentIntegration)
find_package(CURL REQUIRED)
target_link_libraries(chat_server PRIVATE CURL::libcurl)
