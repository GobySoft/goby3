add_executable(goby_test_cobs cobs_test.cpp)
add_test(goby_test_cobs ${goby_BIN_DIR}/goby_test_cobs)
add_dependencies(goby_test_cobs goby)
