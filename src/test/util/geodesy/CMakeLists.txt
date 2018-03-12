add_executable(goby_test_geodesy test.cpp)
target_link_libraries(goby_test_geodesy goby_util)

add_test(goby_test_geodesy ${goby_BIN_DIR}/goby_test_geodesy)
