set(PROTOS bluefin.proto bluefin_config.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

set(SRC bluefin.cpp bluefin_incoming.cpp)

add_library(goby_frontseat_bluefin SHARED ${SRC} ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(goby_frontseat_bluefin
  goby
  ${PROTOBUF_LIBRARIES})

set_target_properties(goby_frontseat_bluefin PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

generate_specialized_frontseat_apps(goby_frontseat_bluefin bluefin)

goby_install_lib(goby_frontseat_bluefin core)
