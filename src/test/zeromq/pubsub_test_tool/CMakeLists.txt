# requires the gobyd and goby_zeromq_tool binaries
if(build_apps)
  if(enable_yaml)
    set(yaml_arg yaml)
  endif()
  add_test(NAME goby_test_zeromq_pubsub_test_tool
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh ${goby_BIN_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${yaml_arg})
  set_tests_properties(goby_test_zeromq_pubsub_test_tool PROPERTIES TIMEOUT 120)
endif()
