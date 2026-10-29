# requires the gobyd and goby_zeromq_tool binaries
if(build_apps)
  add_test(NAME goby_test_zeromq_pubsub_test_tool
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh ${goby_BIN_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(goby_test_zeromq_pubsub_test_tool PROPERTIES TIMEOUT 120)
endif()
