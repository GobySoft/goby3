# requires the goby binary (and gobyd for the zeromq cases)
if(build_apps)
  if(build_zeromq)
    list(APPEND args zeromq)
  endif()
  if(enable_yaml)
    list(APPEND args yaml)
  endif()
  add_test(NAME goby_test_middleware_pubsub_test_tool
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh ${goby_BIN_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${args})
  set_tests_properties(goby_test_middleware_pubsub_test_tool PROPERTIES TIMEOUT 240)

  if(build_zeromq)
    add_test(NAME goby_test_middleware_intervehicle_supersede
      COMMAND ${goby_BIN_DIR}/goby test run ${CMAKE_CURRENT_SOURCE_DIR}/intervehicle_supersede.pb.cfg)
    set_tests_properties(goby_test_middleware_intervehicle_supersede PROPERTIES
      TIMEOUT 60 ENVIRONMENT "PATH=${goby_BIN_DIR}:$ENV{PATH}")

    # the test file loads this library for its DCCL message
    protobuf_generate_cpp_dccl(IV_PROTO_SRCS IV_PROTO_HDRS intervehicle_sample.proto)
    add_library(goby_test_pubsub_intervehicle_sample SHARED ${IV_PROTO_SRCS} ${IV_PROTO_HDRS})
    target_link_libraries(goby_test_pubsub_intervehicle_sample goby)
    add_test(NAME goby_test_middleware_pubsub_intervehicle
      COMMAND ${goby_BIN_DIR}/goby test run ${CMAKE_CURRENT_SOURCE_DIR}/intervehicle.pb.cfg)
    set_tests_properties(goby_test_middleware_pubsub_intervehicle PROPERTIES
      TIMEOUT 90 ENVIRONMENT
      "PATH=${goby_BIN_DIR}:$ENV{PATH};PSTEST_IV_LIBRARY=$<TARGET_FILE:goby_test_pubsub_intervehicle_sample>")
  endif()
endif()
