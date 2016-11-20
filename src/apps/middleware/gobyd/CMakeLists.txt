# tells CMake to generate the *.pb.h and *.pb.cc files from the *.proto
file(GLOB PROTOS *.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

# add these executables to the project
add_executable(gobyd gobyd.cpp ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(gobyd goby_common goby_middleware)

