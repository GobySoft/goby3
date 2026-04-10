protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS
  tool_config.proto
  )

add_executable(goby_udpm_tool
  udpm_tool.cpp
  ${PROTO_SRCS} ${PROTO_HDRS}
  )
target_link_libraries(goby_udpm_tool goby)
