# tells CMake to generate the *.pb.h and *.pb.cc files from the *.proto
file(GLOB PROTOS *.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

# add these executables to the project
add_executable(moos_gateway_g moos_gateway.cpp ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(moos_gateway_g goby goby_moos)
