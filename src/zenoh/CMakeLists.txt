protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS
  protobuf/interprocess_config.proto
  protobuf/tool_config.proto
  )

set(SRC
  transport/interprocess.cpp
)

add_library(goby_zenoh ${SRC} ${PROTO_SRCS} ${PROTO_HDRS})
goby_install_lib(goby_zenoh zenoh)

target_link_libraries(goby_zenoh
  goby
  zenohcxx::zenohc
  zenohc::lib
)

set_target_properties(goby_zenoh PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")
