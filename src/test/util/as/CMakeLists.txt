add_executable(goby_test_as as.cpp)
add_test(goby_test_as ${goby_BIN_DIR}/goby_test_as)
add_dependencies(goby_test_as goby)

