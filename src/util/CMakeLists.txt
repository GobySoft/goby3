file(GLOB PROTOS protobuf/*.proto)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS 
  ${PROTOS}
  )

set(SRC
  linebasedcomms/interface.cpp
  linebasedcomms/nmea_sentence.cpp
  linebasedcomms/serial_client.cpp
  linebasedcomms/tcp_client.cpp
  linebasedcomms/tcp_server.cpp
  geodesy.cpp
  ${PROTO_SRCS} ${PROTO_HDRS}
  )

add_library(goby_util ${SRC})
target_link_libraries(goby_util
  goby_common
  ${Boost_LIBRARIES}
  proj
  dccl)
set_target_properties(goby_util PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")
goby_install_lib(goby_util core)
