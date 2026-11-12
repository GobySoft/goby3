add_executable(goby_test_sci sci.cpp)
add_test(goby_test_sci ${goby_BIN_DIR}/goby_test_sci)
add_dependencies(goby_test_sci goby)
target_compile_options(goby_test_sci PRIVATE "-Wno-deprecated-declarations")


