add_executable(goby_test_sci sci.cpp)
add_test(goby_test_sci ${goby_BIN_DIR}/goby_test_sci)
target_link_libraries(goby_test_sci goby)

