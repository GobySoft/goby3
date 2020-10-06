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
  middleware/protobuf/can_config.proto
  middleware/protobuf/udp_config.proto
  middleware/protobuf/coroner.proto
  middleware/protobuf/layer.proto
  middleware/protobuf/geographic.proto
  middleware/protobuf/frontseat.proto
  middleware/protobuf/frontseat_data.proto
  middleware/protobuf/frontseat_config.proto
  middleware/protobuf/tcp_config.proto
  middleware/protobuf/intermodule.proto
  )

set(MIDDLEWARE_SRC
  middleware/marshalling/interface.cpp
  middleware/marshalling/detail/dccl_serializer_parser.cpp 
  middleware/transport/interthread.cpp
  middleware/transport/intervehicle/driver_thread.cpp
  middleware/application/configuration_reader.cpp
  middleware/log/log_entry.cpp
  middleware/frontseat/interface.cpp
  ${MIDDLEWARE_PROTO_SRCS} ${MIDDLEWARE_PROTO_HDRS} 
  )

if(enable_mavlink)
  set(MIDDLEWARE_SRC
    middleware/marshalling/mavlink.cpp
    ${MIDDLEWARE_SRC}
    )
endif()

if(enable_hdf5)
  set(MIDDLEWARE_SRC
    middleware/log/hdf5/hdf5.cpp
    ${MIDDLEWARE_SRC}
    )
endif()

add_subdirectory(middleware/frontseat)
