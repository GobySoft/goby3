add_executable(goby_test_units units.cpp)
add_test(goby_test_units ${goby_BIN_DIR}/goby_test_units)
add_dependencies(goby_test_units goby)

