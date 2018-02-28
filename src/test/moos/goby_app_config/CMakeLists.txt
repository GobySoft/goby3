protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS config.proto)

add_executable(goby_test_moos_app_config test.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_moos_app_config goby_moos)

