add_executable(goby_test_udpdriver3 test.cpp)
target_link_libraries(goby_test_udpdriver3 goby goby_test_proto_messages)

add_test(goby_test_udpdriver3 ${goby_BIN_DIR}/goby_test_udpdriver3)
  
