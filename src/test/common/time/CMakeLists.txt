add_executable(goby_test_time time.cpp)
target_link_libraries(goby_test_time goby)
# allows us to test the deprecated functions without getting warnings
target_compile_options(goby_test_time PRIVATE "-Wno-deprecated")
add_test(goby_test_time ${goby_BIN_DIR}/goby_test_time)

add_executable(goby_test_time3 time3.cpp)
target_link_libraries(goby_test_time3 goby)
add_test(goby_test_time3 ${goby_BIN_DIR}/goby_test_time3)
