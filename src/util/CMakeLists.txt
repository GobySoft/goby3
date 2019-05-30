protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS 
  protobuf/linebasedcomms.proto
  protobuf/debug_logger.proto
  )

set(SRC
  linebasedcomms/interface.cpp
  linebasedcomms/nmea_sentence.cpp
  linebasedcomms/serial_client.cpp
  linebasedcomms/tcp_client.cpp
  linebasedcomms/tcp_server.cpp
  geodesy.cpp
  debug_logger/flex_ostreambuf.cpp 
  debug_logger/flex_ostream.cpp 
  debug_logger/logger_manipulators.cpp 
  debug_logger/term_color.cpp
  ${PROTO_SRCS} ${PROTO_HDRS}
  )


if(enable_ncurses)
  set(SRC
    ${SRC}
    debug_logger/flex_ncurses.cpp
    )
endif()

add_library(goby_util ${SRC})
target_link_libraries(goby_util
  goby_common
  ${Boost_LIBRARIES}
  proj
  dccl)


if(enable_ncurses)
  target_link_libraries(goby_util ${CURSES_LIBRARIES})
endif()

set_target_properties(goby_util PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")
goby_install_lib(goby_util core)
