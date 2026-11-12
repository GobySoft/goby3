if(build_apps AND enable_yaml)
  add_test(NAME goby_test_middleware_interface_tool
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh ${goby_BIN_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
endif()
