add_subdirectory(gobyd)
add_subdirectory(logger)
add_subdirectory(terminate)

if(enable_wt)
  add_subdirectory(liaison)
endif()
