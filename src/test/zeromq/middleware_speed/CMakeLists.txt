protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_middleware_speed test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_middleware_speed goby goby_zeromq)

add_test(goby_test_middleware_speed_interthread ${goby_BIN_DIR}/goby_test_middleware_speed 0)
set_tests_properties(goby_test_middleware_speed_interthread PROPERTIES TIMEOUT 30)

add_test(goby_test_middleware_speed_interprocess ${goby_BIN_DIR}/goby_test_middleware_speed 1)
set_tests_properties(goby_test_middleware_speed_interprocess PROPERTIES TIMEOUT 30)
