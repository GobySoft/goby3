add_executable(goby_test_seawater seawater.cpp)
add_test(goby_test_seawater ${goby_BIN_DIR}/goby_test_seawater)
add_dependencies(goby_test_seawater goby)

