add_executable(goby_test_queue4 test.cpp)
target_link_libraries(goby_test_queue4 goby goby_test_proto_messages)

add_test(goby_test_queue4 ${goby_BIN_DIR}/goby_test_queue4)
