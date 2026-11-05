add_executable(goby_test_nmea nmea.cpp)
target_link_libraries(goby_test_nmea goby)
add_test(goby_test_nmea ${goby_BIN_DIR}/goby_test_nmea)
