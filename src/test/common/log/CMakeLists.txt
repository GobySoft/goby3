add_executable(goby_test_log test.cpp)
target_link_libraries(goby_test_log goby)
add_test(goby_test_log ${goby_BIN_DIR}/goby_test_log)
