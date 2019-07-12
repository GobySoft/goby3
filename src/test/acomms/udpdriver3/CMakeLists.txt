protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ../dccl3/test.proto ../dccl3/header.proto)

add_executable(goby_test_udpdriver3 test.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_udpdriver3 goby)

add_test(goby_test_udpdriver3 ${goby_BIN_DIR}/goby_test_udpdriver3)
  
