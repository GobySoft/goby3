protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS
  test_tool_config.proto
  )

add_executable(goby_test_tool
  test_tool.cpp
  test_file.cpp
  fixture.cpp
  visualize.cpp
  ${PROTO_SRCS} ${PROTO_HDRS}
  )
target_link_libraries(goby_test_tool goby ${CMAKE_DL_LIBS})

# zeromq endpoints only when the transport is built; udpm is always available
if(build_zeromq)
  target_link_libraries(goby_test_tool goby_zeromq)
  target_compile_definitions(goby_test_tool PRIVATE GOBY_TEST_TOOL_HAS_ZEROMQ)
endif()

if(enable_yaml)
  target_link_libraries(goby_test_tool yaml-cpp)
endif()
