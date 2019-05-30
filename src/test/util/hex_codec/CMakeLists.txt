add_executable(goby_test_hex_codec hex_codec.cpp)
add_test(goby_test_hex_codec ${goby_BIN_DIR}/goby_test_hex_codec)
target_link_libraries(goby_test_hex_codec goby)
