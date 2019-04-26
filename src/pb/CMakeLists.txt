file(GLOB PROTOS protobuf/*.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

file(GLOB PB_SRC *.cpp)

add_library(goby_pb ${PB_SRC} ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(goby_pb ${Boost_LIBRARIES} pthread goby_common goby_util goby_acomms ${ZeroMQ_LIBRARIES} ${PROTOBUF_LIBRARIES})

if(NOT ${APPLE})
  target_link_libraries(goby_pb rt) 
endif()

set_target_properties(goby_pb PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")
goby_install_lib(goby_pb core)
