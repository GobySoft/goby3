protobuf_generate_cpp_dccl(UDPM_PROTO_SRCS UDPM_PROTO_HDRS
  udpm/protobuf/interprocess_config.proto
  udpm/protobuf/tool_config.proto
  )

set(UDPM_SRC
  udpm/transport/interprocess.cpp
  
  ${UDPM_PROTO_SRCS} ${UDPM_PROTO_HDRS} 
  )
