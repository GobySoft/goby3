protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_poller_attach test.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_poller_attach goby goby_zeromq)

add_test(goby_test_poller_attach ${goby_BIN_DIR}/goby_test_poller_attach)
set_tests_properties(goby_test_poller_attach PROPERTIES TIMEOUT 120)
