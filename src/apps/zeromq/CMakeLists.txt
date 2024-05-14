add_subdirectory(gobyd)
add_subdirectory(logger)
add_subdirectory(terminate)
add_subdirectory(coroner)
add_subdirectory(frontseat_interface)
add_subdirectory(zeromq_tool)

if(enable_ais)
  add_subdirectory(opencpn)
endif()

if(enable_wt)
  add_subdirectory(liaison)
endif()

if(enable_mavlink)
  add_subdirectory(mavlink_gateway)
endif()

add_subdirectory(gpsd_client)

if(enable_mysql)
  add_subdirectory(geov)
endif()

add_subdirectory(acomms)
