add_executable(goby_test_amac1 test.cpp)
target_link_libraries(goby_test_amac1 goby)

if(enable_testing_asio)
  add_test(goby_test_amac1 ${goby_BIN_DIR}/goby_test_amac1)
endif()
