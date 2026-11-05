# goby_clang_tool -gen on a fixture application; it reads the compile command CMake exports for it
if(enable_llvm AND build_apps)
  protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS dynamic_groups.proto)
  add_executable(goby_test_clang_tool_dynamic_groups dynamic_groups.cpp ${PROTO_SRCS} ${PROTO_HDRS})
  target_link_libraries(goby_test_clang_tool_dynamic_groups goby)

  add_test(NAME goby_test_middleware_clang_tool_gen
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh ${goby_BIN_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR})
  set_tests_properties(goby_test_middleware_clang_tool_gen PROPERTIES TIMEOUT 300)
endif()
