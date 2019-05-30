protobuf_generate_cpp(MIDDLEWARE_PROTO_SRCS MIDDLEWARE_PROTO_HDRS
  middleware/protobuf/app_config.proto
  middleware/protobuf/hdf5.proto
  middleware/protobuf/intervehicle_config.proto
  middleware/protobuf/interprocess_config.proto
  middleware/protobuf/interprocess_data.proto
  middleware/protobuf/interprocess_zeromq.proto
  middleware/protobuf/liaison_config.proto
  middleware/protobuf/transporter_config.proto
  middleware/protobuf/intervehicle_status.proto
  middleware/protobuf/gobyd_config.proto
  middleware/protobuf/logger_config.proto
  middleware/protobuf/log_tool_config.proto
  middleware/protobuf/terminate_config.proto
  middleware/protobuf/terminate.proto
  )

set(MIDDLEWARE_SRC
  middleware/serialize_parse.cpp
  middleware/transport-interthread.cpp
  middleware/transport-interprocess-zeromq.cpp
  middleware/transport-intervehicle.cpp
  middleware/configuration_reader.cpp
  middleware/log/log_entry.cpp
  ${MIDDLEWARE_PROTO_SRCS} ${MIDDLEWARE_PROTO_HDRS} 
  )
