# tells CMake to generate the *.pb.h and *.pb.cc files from the *.proto
protobuf_include_dirs(${CMAKE_SOURCE_DIR}/acomms/dccl/dccl_simple)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ../../dccl/dccl_simple/simple.proto)

# include the CMake current build ("binary") directory for the generated simple.pb.*
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(queue_multimessage multimessage.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(queue_multimessage goby ${Boost_LIBRARIES} ${PROTOBUF_LIBRARIES})
install(TARGETS queue_multimessage RUNTIME DESTINATION bin)
