add_executable(goby_test_benthos_atm900_driver1 test.cpp ../../acomms/driver_tester/driver_tester.cpp)
target_link_libraries(goby_test_benthos_atm900_driver1 goby_acomms)

# Use with Benthos ATM900 test modems (can't be run automatically with build)
# add_test(goby_test_benthos_atm900_driver1 ${goby_BIN_DIR}/goby_test_benthos_atm900_driver1)

