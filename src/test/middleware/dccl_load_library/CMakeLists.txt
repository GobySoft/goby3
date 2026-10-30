if(TARGET dccl_native_protobuf)
  protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS test.proto)

  add_executable(goby_test_middleware_dccl_load_library test.cpp ${PROTO_SRCS} ${PROTO_HDRS})
  target_link_libraries(goby_test_middleware_dccl_load_library goby)
  target_compile_definitions(goby_test_middleware_dccl_load_library PRIVATE
    DCCL_NATIVE_PROTOBUF_LIBRARY="$<TARGET_FILE:dccl_native_protobuf>")

  add_test(goby_test_middleware_dccl_load_library ${goby_BIN_DIR}/goby_test_middleware_dccl_load_library)
endif()
