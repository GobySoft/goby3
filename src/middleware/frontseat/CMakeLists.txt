function(generate_specialized_frontseat_apps target shortname)
  file(WRITE goby_frontseat_interface_${shortname}
    "#!/bin/bash\nLD_LIBRARY_PATH=${goby_LIB_DIR}:\$\{LD_LIBRARY_PATH\} FRONTSEAT_DRIVER_LIBRARY=lib${target}.so.${GOBY_SOVERSION} exec goby_frontseat_interface \$@")
  # copy to get executable permissions set
  file(COPY goby_frontseat_interface_${shortname}
    DESTINATION ${goby_BIN_DIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  # remove temporary files
  file(REMOVE goby_frontseat_interface_${shortname})

  if(build_iFrontSeat)
    file(WRITE iFrontSeat_${shortname}
      "#!/bin/bash\nLD_LIBRARY_PATH=${goby_LIB_DIR}:\$\{LD_LIBRARY_PATH\} IFRONTSEAT_DRIVER_LIBRARY=lib${target}.so.${GOBY_SOVERSION} exec iFrontSeat \$@")
    file(COPY iFrontSeat_${shortname}
      DESTINATION ${goby_BIN_DIR}
      FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    file(REMOVE iFrontSeat_${shortname})
  endif()  

endfunction()

add_subdirectory(bluefin)
add_subdirectory(waveglider)
add_subdirectory(iver)
add_subdirectory(simulator)
