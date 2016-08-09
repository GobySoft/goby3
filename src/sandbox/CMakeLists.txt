file(GLOB PROTOS protobuf/*.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

set(SRC
  serialize_parse.cpp
  transport-interthread.cpp
  transport-interprocess.cpp
  ${PROTO_SRCS} ${PROTO_HDRS} 
)

add_library(goby_sandbox ${SRC})


target_link_libraries(goby_sandbox ${PROTOBUF_LIBRARY})
set_target_properties(goby_sandbox PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

goby_install_lib(goby_sandbox)
