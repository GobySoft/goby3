function(create_single_thread_app1_test interprocess_impl link_libraries)

  # test.proto is shared by every interprocess_impl; generating it once avoids duplicate build rules
  set(PROTO_LIB goby_test_middleware_single_thread_app1_proto)
  if(NOT TARGET ${PROTO_LIB})
    protobuf_generate_cpp_dccl(SHARED_PROTO_SRCS SHARED_PROTO_HDRS ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/test.proto)
    add_library(${PROTO_LIB} STATIC ${SHARED_PROTO_SRCS} ${SHARED_PROTO_HDRS})
    target_link_libraries(${PROTO_LIB} goby)
  endif()

  protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/${interprocess_impl}.proto)
  set(TEST goby_test_middleware_single_thread_app1_${interprocess_impl})

  add_executable(${TEST} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
  target_compile_definitions(${TEST} PRIVATE test_for_${interprocess_impl})
  target_link_libraries(${TEST} ${PROTO_LIB} goby ${link_libraries})
  add_test(${TEST} ${goby_BIN_DIR}/${TEST} -vvvv) 
endfunction()
