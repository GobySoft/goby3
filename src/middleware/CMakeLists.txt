protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
  protobuf/interplatform_config.proto
  protobuf/interprocess_config.proto
  protobuf/interprocess_data.proto
  protobuf/interprocess_zeromq.proto
  protobuf/liaison_config.proto
  protobuf/transporter_config.proto
  )

set(SRC
  serialize_parse.cpp
  transport-interthread.cpp
  transport-interprocess-zeromq.cpp
  transport-intervehicle.cpp
  transport-interfaces.cpp
  log.cpp
  ${PROTO_SRCS} ${PROTO_HDRS} 
)

add_library(goby_middleware ${SRC})


target_link_libraries(goby_middleware ${PROTOBUF_LIBRARY} goby_acomms)
set_target_properties(goby_middleware PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

goby_install_lib(goby_middleware)
