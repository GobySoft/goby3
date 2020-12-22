add_subdirectory(gobyd)
add_subdirectory(gpsd_client)
add_subdirectory(logger)
add_subdirectory(terminate)
add_subdirectory(coroner)
add_subdirectory(frontseat_interface)

if(enable_wt)
  add_subdirectory(liaison)
endif()

if(enable_mavlink)
  add_subdirectory(mavlink_gateway)
endif()
