# tells CMake to generate the *.pb.h and *.pb.cc files from the *.proto
file(GLOB PROTOS *.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

# add these executables to the project
add_executable(goby_bridge bridge.cpp ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(goby_bridge goby_common goby_pb goby_acomms goby_util)

