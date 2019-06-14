protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test_config.proto)

add_executable(goby_test_mmdriver2 test.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_mmdriver2 goby)

# can't really run this test automatically
# add_test(test_mmdriver2 test_mmdriver2)
