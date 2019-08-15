file(GLOB PROTOS *.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

add_executable(pTranslator
  pTranslatorMain.cpp
  pTranslator.cpp ${PROTO_SRCS} ${PROTO_HDRS})
  
target_link_libraries(pTranslator
  goby_moos
  ${MOOS_LIBRARIES}
  ${Boost_LIBRARIES}
  ${PROTOBUF_LIBRARY}
  ${CMAKE_DL_LIBS})
