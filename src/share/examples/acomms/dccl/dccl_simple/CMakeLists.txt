# tells CMake to generate the *.pb.h and *.pb.cc files from the *.proto
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS simple.proto)

# include the CMake current build ("binary") directory for the generated simple.pb.*
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# add these executables to the project
add_executable(dccl_simple dccl_simple.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(dccl_simple goby ${Boost_LIBRARIES} ${PROTOBUF_LIBRARIES})

install(TARGETS dccl_simple RUNTIME DESTINATION bin)
