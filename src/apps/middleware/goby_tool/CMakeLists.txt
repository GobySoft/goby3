protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS
  marshalling/protobuf.proto
  tool.proto
  log.proto
  test.proto
  )

add_executable(goby_tool
  goby_tool.cpp
  unified_log_tool.cpp
  test_tool.cpp
  marshalling/protobuf.cpp
  ${PROTO_SRCS} ${PROTO_HDRS}
  )
target_link_libraries(goby_tool goby)
set_target_properties(goby_tool PROPERTIES OUTPUT_NAME goby)

# 'goby test' drives zeromq endpoints only when the transport is built
if(build_zeromq)
  target_link_libraries(goby_tool goby_zeromq)
  target_compile_definitions(goby_tool PRIVATE GOBY_TEST_TOOL_HAS_ZEROMQ)
endif()

if(enable_yaml)
  target_link_libraries(goby_tool yaml-cpp)
endif()
