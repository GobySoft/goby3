add_executable(goby_test_ipcodecs test.cpp)
target_link_libraries(goby_test_ipcodecs goby)

add_test(goby_test_ipcodecs ${goby_BIN_DIR}/goby_test_ipcodecs)
