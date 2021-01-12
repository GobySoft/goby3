add_executable(goby_test_queue2 test.cpp)
target_link_libraries(goby_test_queue2 goby goby_test_proto_messages)

add_test(goby_test_queue2 ${goby_BIN_DIR}/goby_test_queue2)
