add_subdirectory(gobyd)
add_subdirectory(logger)
add_subdirectory(terminate)

if(enable_wt)
  add_subdirectory(liaison)
endif()

if(enable_mavlink)
  add_subdirectory(mavlink_gateway)
endif()
