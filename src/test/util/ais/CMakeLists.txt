add_executable(goby_test_ais ais.cpp)
add_test(goby_test_ais ${goby_BIN_DIR}/goby_test_ais)
target_link_libraries(goby_test_ais goby)

