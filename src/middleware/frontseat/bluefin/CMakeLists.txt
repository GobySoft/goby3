set(PROTOS bluefin.proto bluefin_config.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

set(SRC bluefin.cpp bluefin_incoming.cpp)

add_library(goby_frontseat_bluefin SHARED ${SRC} ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(goby_frontseat_bluefin
  goby_moos
  ${PROTOBUF_LIBRARIES})

set_target_properties(goby_frontseat_bluefin PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

configure_file(iFrontSeat_bluefin.in ${goby_BIN_DIR}/iFrontSeat_bluefin @ONLY)
goby_install_lib(goby_frontseat_bluefin moos)
