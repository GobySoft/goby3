file(GLOB PROTOS *.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

add_executable(pGobyMOOSAppTemplate
  pGobyMOOSAppTemplate.cpp
  ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(pGobyMOOSAppTemplate
  goby_moos
  ${MOOS_LIBRARIES}
  ${Boost_LIBRARIES}
  ${PROTOBUF_LIBRARY}
  ${CMAKE_DL_LIBS})
