set(PROTOS iver_driver.proto iver_driver_config.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

set(SRC iver_driver.cpp)

add_library(goby_frontseat_iver SHARED ${SRC} ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(goby_frontseat_iver
  goby
  ${PROTOBUF_LIBRARIES})

set_target_properties(goby_frontseat_iver PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

goby_register_frontseat_plugin(goby_frontseat_iver iver)
goby_install_lib(goby_frontseat_iver core)
