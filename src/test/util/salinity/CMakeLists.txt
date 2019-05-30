add_executable(goby_test_salinity salinity.cpp)
add_test(goby_test_salinity ${goby_BIN_DIR}/goby_test_salinity)
add_dependencies(goby_test_salinity goby)

