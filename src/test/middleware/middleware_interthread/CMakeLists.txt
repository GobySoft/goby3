protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_middleware_interthread test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_middleware_interthread goby dccl)

add_test(goby_test_middleware_interthread ${goby_BIN_DIR}/goby_test_middleware_interthread)

