protobuf_generate_cpp(MIDDLEWARE_PROTO_SRCS MIDDLEWARE_PROTO_HDRS
  middleware/protobuf/app_config.proto
  middleware/protobuf/hdf5.proto
  middleware/protobuf/serializer_transporter.proto
  middleware/protobuf/transporter_config.proto
  middleware/protobuf/intervehicle.proto
  middleware/protobuf/intervehicle_transporter_config.proto
  middleware/protobuf/log_tool_config.proto
  middleware/protobuf/terminate.proto
  middleware/protobuf/io.proto
  middleware/protobuf/serial_config.proto
  )

set(MIDDLEWARE_SRC
  middleware/marshalling/interface.cpp
  middleware/marshalling/dccl.cpp 
  middleware/transport/interthread.cpp
  middleware/intervehicle/driver-thread.cpp
  middleware/application/configuration_reader.cpp
  middleware/log/log_entry.cpp
  ${MIDDLEWARE_PROTO_SRCS} ${MIDDLEWARE_PROTO_HDRS} 
  )

if(enable_mavlink)
  set(MIDDLEWARE_SRC
    middleware/marshalling/mavlink.cpp
    ${MIDDLEWARE_SRC}
    )
endif()
