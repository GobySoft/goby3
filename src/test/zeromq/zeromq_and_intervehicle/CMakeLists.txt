protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_zeromq_and_intervehicle test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_zeromq_and_intervehicle goby goby_zeromq)

add_test(goby_test_zeromq_and_intervehicle ${goby_BIN_DIR}/goby_test_zeromq_and_intervehicle)

