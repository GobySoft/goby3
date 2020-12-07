protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_zeromq_single_thread_app1 test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_zeromq_single_thread_app1 goby goby_zeromq)

add_test(goby_test_zeromq_single_thread_app1 ${goby_BIN_DIR}/goby_test_zeromq_single_thread_app1 -vvvv) 

