protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
  marshalling/protobuf.proto
  tool.proto
  log.proto
  )

add_executable(goby_tool
  goby_tool.cpp
  unified_log_tool.cpp
  marshalling/protobuf.cpp
  ${PROTO_SRCS} ${PROTO_HDRS}
  )
target_link_libraries(goby_tool goby)
set_target_properties(goby_tool PROPERTIES OUTPUT_NAME goby)
