file(GLOB PROTOS protobuf/*.proto)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

set(SRC
  time.cpp
  time3.cpp
  logger/flex_ostreambuf.cpp 
  logger/flex_ostream.cpp 
  logger/logger_manipulators.cpp 
  logger/term_color.cpp
  configuration_reader.cpp
  application_base.cpp
  ${PROTO_SRCS} ${PROTO_HDRS} 
)

if(enable_zeromq)
  set(SRC
    ${SRC}
    zeromq_service.cpp
    )
endif()

if(enable_ncurses)
  set(SRC
    ${SRC}
    logger/flex_ncurses.cpp
    )
endif()

add_library(goby_common ${SRC})

if(enable_zeromq)
  target_link_libraries(goby_common ${ZeroMQ_LIBRARIES})
endif()

if(enable_ncurses)
  target_link_libraries(goby_common ${CURSES_LIBRARIES})
endif()


target_link_libraries(goby_common ${PROTOBUF_LIBRARY} ${Boost_LIBRARIES} pthread ${CMAKE_DL_LIBS})
set_target_properties(goby_common PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

goby_install_lib(goby_common)
