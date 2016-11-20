file(GLOB PROTOS protobuf/*.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

set(SRC
  serialize_parse.cpp
  transport-interthread.cpp
  transport-interprocess.cpp
  zeromq_service.cpp

  ${PROTO_SRCS} ${PROTO_HDRS} 
)

add_library(goby_middleware ${SRC})


target_link_libraries(goby_middleware ${PROTOBUF_LIBRARY} goby_acomms)
set_target_properties(goby_middleware PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

goby_install_lib(goby_middleware)
