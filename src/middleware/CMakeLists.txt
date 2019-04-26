protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
  protobuf/intervehicle_config.proto
  protobuf/interprocess_config.proto
  protobuf/interprocess_data.proto
  protobuf/interprocess_zeromq.proto
  protobuf/liaison_config.proto
  protobuf/transporter_config.proto
  protobuf/intervehicle_status.proto
  protobuf/gobyd_config.proto
  protobuf/logger_config.proto
  protobuf/log_tool_config.proto
  protobuf/terminate_config.proto
  protobuf/terminate.proto
  )

set(SRC
  serialize_parse.cpp
  transport-interthread.cpp
  transport-interprocess-zeromq.cpp
  transport-intervehicle.cpp
  transport-interfaces.cpp
  log/log_entry.cpp
  ${PROTO_SRCS} ${PROTO_HDRS} 
)

add_library(goby_middleware ${SRC})


target_link_libraries(goby_middleware ${PROTOBUF_LIBRARY} goby_acomms)
set_target_properties(goby_middleware PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

goby_install_lib(goby_middleware core)
