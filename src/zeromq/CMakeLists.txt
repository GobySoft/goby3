protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
  protobuf/interprocess_config.proto
  protobuf/interprocess_zeromq.proto
  protobuf/gobyd_config.proto
  protobuf/gps_config.proto
  protobuf/logger_config.proto
  protobuf/liaison_config.proto
  protobuf/terminate_config.proto
  protobuf/mavlink_gateway_config.proto
  protobuf/coroner_config.proto
  protobuf/frontseat_interface_config.proto
  protobuf/opencpn_config.proto
  )

set(SRC
  transport/interprocess.cpp
)

add_library(goby_zeromq ${SRC} ${PROTO_SRCS} ${PROTO_HDRS})
goby_install_lib(goby_zeromq zeromq)

target_link_libraries(goby_zeromq
  goby
  ${ZeroMQ_LIBRARIES}
)

set_target_properties(goby_zeromq PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")
