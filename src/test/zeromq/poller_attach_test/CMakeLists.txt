protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_poller_attach_test test.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_poller_attach_test goby goby_zeromq)

add_test(goby_test_poller_attach_test ${goby_BIN_DIR}/goby_test_poller_attach_test)
set_tests_properties(goby_test_poller_attach_test PROPERTIES TIMEOUT 120)
