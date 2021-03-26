add_definitions(-DGOBY_LIB_DIR="${goby_LIB_DIR}")

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test2.proto)

add_library(goby_hdf5test SHARED test-plugin.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_hdf5test goby dccl goby_test_proto_messages)

add_executable(goby_test_hdf5 test.cpp)
target_link_libraries(goby_test_hdf5 goby)
add_test(goby_test_hdf5 ${goby_BIN_DIR}/goby_test_hdf5)
