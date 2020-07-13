set(PROTOS basic_simulator_frontseat_driver.proto
  basic_simulator_frontseat_driver_config.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

set(SRC basic_simulator_frontseat_driver.cpp)

add_library(goby_frontseat_basic_simulator SHARED ${SRC} ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(goby_frontseat_basic_simulator
  goby
  ${PROTOBUF_LIBRARIES})

set_target_properties(goby_frontseat_basic_simulator PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")
goby_install_lib(goby_frontseat_basic_simulator core)
