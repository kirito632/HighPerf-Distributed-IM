find_package(PkgConfig REQUIRED)
pkg_check_modules(RDKAFKA QUIET IMPORTED_TARGET rdkafka++)
if(NOT RDKAFKA_FOUND)
    message(WARNING "librdkafka++ not found; im_mq/mq_smoke will not be built")
    return()
endif()

add_library(im_mq STATIC
    EventEnvelope.cpp
    KafkaProducer.cpp
    KafkaConsumer.cpp
)
target_include_directories(im_mq PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(im_mq PUBLIC PkgConfig::RDKAFKA)
if(JSONCPP_TARGET)
    target_link_libraries(im_mq PUBLIC ${JSONCPP_TARGET})
endif()
