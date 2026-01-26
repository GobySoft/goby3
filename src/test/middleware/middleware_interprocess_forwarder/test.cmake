function(create_middleware_interprocess_forwarder_test interprocess_impl link_libraries)
  protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/test.proto)
  set(TEST goby_test_middleware_interprocess_forwarder_${interprocess_impl})
  
  add_executable(${TEST} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/test.cpp  ${PROTO_SRCS} ${PROTO_HDRS})
  target_compile_definitions(${TEST} PRIVATE test_for_${interprocess_impl})
  
  target_link_libraries(${TEST} goby ${link_libraries})
  add_test(${TEST} ${goby_BIN_DIR}/${TEST})
endfunction()
