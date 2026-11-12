if(build_apps AND enable_yaml)
  add_test(NAME goby_test_middleware_interface_tool
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh ${goby_BIN_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

  include(GobyInterfaces)
  set(GOBY_EXECUTABLE ${goby_BIN_DIR}/goby)
  goby_validate_interfaces(NAME goby_test_middleware_interface_validate
    DEPLOYMENT_YAML ${CMAKE_CURRENT_SOURCE_DIR}/deployments/elsewhere.yml
    INTERFACE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/interfaces)
endif()
