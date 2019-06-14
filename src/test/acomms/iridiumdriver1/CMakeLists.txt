add_executable(goby_test_iridiumdriver1 test.cpp ../../acomms/driver_tester/driver_tester.cpp)
target_link_libraries(goby_test_iridiumdriver1 goby)

# Use with ATDuck (can't be run automatically with build)
# add_test(goby_test_iridiumdriver1 ${goby_BIN_DIR}/goby_test_iridiumdriver1)

