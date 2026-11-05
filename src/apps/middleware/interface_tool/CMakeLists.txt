protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS
  interface_tool_config.proto
  )

add_executable(goby_interface_tool
  interface_tool.cpp
  inspect.cpp
  ${PROTO_SRCS} ${PROTO_HDRS}
  )
target_link_libraries(goby_interface_tool goby yaml-cpp)
