add_executable(goby_test_middleware_json json.cpp)
target_link_libraries(goby_test_middleware_json goby)
add_test(goby_test_middleware_json ${goby_BIN_DIR}/goby_test_middleware_json)

