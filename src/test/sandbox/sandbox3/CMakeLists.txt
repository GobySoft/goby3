protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_sandbox3 test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_sandbox3 goby_sandbox goby_common dccl)

add_test(goby_test_sandbox3 ${goby_BIN_DIR}/goby_test_sandbox3)

