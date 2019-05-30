protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_middleware6 test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_middleware6 goby dccl)

add_test(goby_test_middleware6_interthread ${goby_BIN_DIR}/goby_test_middleware6 0)
add_test(goby_test_middleware6_interprocess ${goby_BIN_DIR}/goby_test_middleware6 1)
