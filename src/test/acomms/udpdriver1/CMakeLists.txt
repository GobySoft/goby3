add_executable(goby_test_udpdriver1 test.cpp ../driver_tester/driver_tester.cpp)
target_link_libraries(goby_test_udpdriver1 goby)

if(enable_testing_asio)
  add_test(goby_test_udpdriver1 ${goby_BIN_DIR}/goby_test_udpdriver1)
endif()

