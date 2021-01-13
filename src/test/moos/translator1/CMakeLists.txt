get_filename_component(translator_test_dir ./ ABSOLUTE)
add_definitions(-DTRANSLATOR_TEST_DIR="${translator_test_dir}")

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS basic_node_report.proto)

add_executable(goby_test_translator1 test.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_translator1 goby_moos goby_test_proto_messages)

add_test(goby_test_translator1 ${goby_BIN_DIR}/goby_test_translator1)
