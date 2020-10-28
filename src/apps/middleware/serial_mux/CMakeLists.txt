protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS config.proto)

add_executable(goby_serial_mux mux.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_serial_mux goby)
