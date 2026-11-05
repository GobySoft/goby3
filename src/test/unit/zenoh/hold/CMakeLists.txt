add_executable(goby_test_zenoh_hold test.cpp)
target_link_libraries(goby_test_zenoh_hold goby goby_zenoh)
add_test(goby_test_zenoh_hold ${goby_BIN_DIR}/goby_test_zenoh_hold)
