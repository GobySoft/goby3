set(SRC frontseat_interface.cpp)

if(build_moos)
  set(SRC
    moos_helm_interface.cpp
    ${SRC})
else()
  set(SRC
    empty_helm_interface.cpp
    ${SRC})
endif()

add_executable(goby_frontseat_interface ${SRC})
if(build_moos)
  target_link_libraries(goby_frontseat_interface goby goby_zeromq goby_moos goby_frontseat_moos_gateway_plugin)
else()
  target_link_libraries(goby_frontseat_interface goby goby_zeromq)
endif()

generate_middleware_interfaces(goby_frontseat_interface)

# write a specialized script for each plugin to simplify usage
list(LENGTH GOBY_FRONTSEAT_PLUGIN_TARGET_LIST count)
math(EXPR count "${count}-1")
foreach(i RANGE ${count})
  list(GET GOBY_FRONTSEAT_PLUGIN_TARGET_LIST ${i} target)
  list(GET GOBY_FRONTSEAT_PLUGIN_SHORTNAME_LIST ${i} shortname)

  file(WRITE goby_frontseat_interface_${shortname}
    "#!/bin/bash\nLD_LIBRARY_PATH=${goby_LIB_DIR}:\$\{LD_LIBRARY_PATH\} FRONTSEAT_DRIVER_LIBRARY=lib${target}.so.${GOBY_SOVERSION} exec goby_frontseat_interface \$@")
  # copy to get executable permissions set
  file(COPY goby_frontseat_interface_${shortname}
    DESTINATION ${goby_BIN_DIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  # remove temporary files
  file(REMOVE goby_frontseat_interface_${shortname})
endforeach()

