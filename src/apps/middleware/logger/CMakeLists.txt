protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS config.proto)

add_executable(goby_logger logger.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_logger goby_middleware)
