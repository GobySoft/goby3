protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
  goby_store_server_config.proto)


add_executable(goby_store_server
  goby_store_server.cpp
  ${PROTO_HDRS}
  ${PROTO_SRCS}
  )

target_link_libraries(
  goby_store_server 
  ${Boost_LIBRARIES}
  goby
  ${SQLITE3_LIBRARIES}
  )
