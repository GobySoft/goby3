protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_middleware_basic test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_middleware_basic goby goby_zeromq)

add_test(goby_test_middleware_basic ${goby_BIN_DIR}/goby_test_middleware_basic)
set_tests_properties(goby_test_middleware_basic PROPERTIES TIMEOUT 30)

