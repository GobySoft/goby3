add_executable(iFrontSeat
  iFrontSeat.cpp
  legacy_translator.cpp)

target_link_libraries(iFrontSeat 
  goby_moos
  goby_frontseat_bluefin
  ${MOOS_LIBRARIES}
  ${Boost_LIBRARIES}
  ${PROTOBUF_LIBRARIES}
  ${CMAKE_DL_LIBS}
)


# write a specialized script for each plugin to simplify usage
list(LENGTH GOBY_FRONTSEAT_PLUGIN_TARGET_LIST count)
math(EXPR count "${count}-1")
foreach(i RANGE ${count})
  list(GET GOBY_FRONTSEAT_PLUGIN_TARGET_LIST ${i} target)
  list(GET GOBY_FRONTSEAT_PLUGIN_SHORTNAME_LIST ${i} shortname)

  file(WRITE iFrontSeat_${shortname}
    "#!/bin/bash\nLD_LIBRARY_PATH=${goby_LIB_DIR}:\$\{LD_LIBRARY_PATH\} IFRONTSEAT_DRIVER_LIBRARY=lib${target}.so.${GOBY_SOVERSION} exec iFrontSeat \$@")
  file(COPY iFrontSeat_${shortname}
    DESTINATION ${goby_BIN_DIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  file(REMOVE iFrontSeat_${shortname})
endforeach()
