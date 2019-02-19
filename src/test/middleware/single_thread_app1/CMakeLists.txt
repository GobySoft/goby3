protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS test.proto)

add_executable(goby_test_single_thread_app1 test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(goby_test_single_thread_app1 goby_middleware goby_common dccl)

add_test(goby_test_single_thread_app1 ${goby_BIN_DIR}/goby_test_single_thread_app1)

# Disable the test when building with ThreadSanitizer
# https://github.com/GobySoft/goby3/issues/68
if(CMAKE_CXX_FLAGS MATCHES "(^| )-fsanitize=thread( |$)")
  set_tests_properties(goby_test_single_thread_app1 PROPERTIES DISABLED true)
endif()
