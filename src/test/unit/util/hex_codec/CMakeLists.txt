add_executable(goby_test_hex_codec hex_codec.cpp)
add_test(goby_test_hex_codec ${goby_BIN_DIR}/goby_test_hex_codec)
add_dependencies(goby_test_hex_codec goby)

