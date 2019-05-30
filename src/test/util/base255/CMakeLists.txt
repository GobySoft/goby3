add_executable(goby_test_base255 base255.cpp)
target_link_libraries(goby_test_base255 ${GMP_LIBRARIES})
add_dependencies(goby_test_base255 goby)
add_test(goby_test_base255 ${goby_BIN_DIR}/goby_test_base255)

