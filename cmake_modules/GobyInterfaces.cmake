# usage: goby_validate_interfaces(DEPLOYMENT_YAML <deployment.yml>
#                                 [NAME <test name>]
#                                 [INTERFACE_DIRS <dir> ...]
#                                 [ARGS <extra 'goby interface validate' arguments> ...])
# Adds a CTest test that runs 'goby interface validate --strict' on a deployment file, so a
# required subscription left without a publisher fails the build's tests.
# NAME defaults to goby_validate_<deployment file name>; each INTERFACE_DIRS entry is passed as -I.
function(GOBY_VALIDATE_INTERFACES)
  cmake_parse_arguments(args "" "NAME;DEPLOYMENT_YAML" "INTERFACE_DIRS;ARGS" ${ARGN})
  if(NOT args_DEPLOYMENT_YAML)
    message(FATAL_ERROR "goby_validate_interfaces: DEPLOYMENT_YAML is required")
  endif()
  if(NOT args_NAME)
    get_filename_component(DEPLOYMENT_NAME ${args_DEPLOYMENT_YAML} NAME_WE)
    set(args_NAME goby_validate_${DEPLOYMENT_NAME})
  endif()

  if(TARGET goby_tool)
    set(GOBY_TOOL $<TARGET_FILE:goby_tool>)
  elseif(GOBY_EXECUTABLE)
    set(GOBY_TOOL ${GOBY_EXECUTABLE})
  else()
    message(WARNING "goby_validate_interfaces: the goby tool was not found, so ${args_NAME} is not added")
    return()
  endif()

  set(DIR_ARGS)
  foreach(DIR ${args_INTERFACE_DIRS})
    list(APPEND DIR_ARGS -I ${DIR})
  endforeach()

  add_test(NAME ${args_NAME}
    COMMAND ${GOBY_TOOL} interface validate ${args_DEPLOYMENT_YAML} --strict ${DIR_ARGS} ${args_ARGS})
endfunction()
