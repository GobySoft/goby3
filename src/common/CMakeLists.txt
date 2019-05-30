protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
  protobuf/option_extensions.proto)

set(SRC
  ../time/simulation.cpp
  ${PROTO_SRCS} ${PROTO_HDRS} 
)


add_library(goby_common ${SRC})



target_link_libraries(goby_common ${PROTOBUF_LIBRARY} ${Boost_LIBRARIES} pthread ${CMAKE_DL_LIBS})
set_target_properties(goby_common PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

goby_install_lib(goby_common core)
