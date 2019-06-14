protobuf_generate_cpp(UTIL_PROTO_SRCS UTIL_PROTO_HDRS 
  util/protobuf/linebasedcomms.proto
  util/protobuf/debug_logger.proto
  )

set(UTIL_SRC
  util/base_convert.cpp
  util/linebasedcomms/interface.cpp
  util/linebasedcomms/nmea_sentence.cpp
  util/linebasedcomms/serial_client.cpp
  util/linebasedcomms/tcp_client.cpp
  util/linebasedcomms/tcp_server.cpp
  util/geodesy.cpp
  util/debug_logger/flex_ostreambuf.cpp 
  util/debug_logger/flex_ostream.cpp 
  util/debug_logger/logger_manipulators.cpp 
  util/debug_logger/term_color.cpp
  ${UTIL_PROTO_SRCS} ${UTIL_PROTO_HDRS}
  )


if(enable_ncurses)
  set(UTIL_SRC
    ${UTIL_SRC}
    util/debug_logger/flex_ncurses.cpp
    )
endif()
