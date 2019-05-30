add_subdirectory(gobyd)
add_subdirectory(logger)
add_subdirectory(terminate)

if(enable_wt)
  add_subdirectory(liaison)
endif()

if(enable_hdf5)
  add_subdirectory(goby_hdf5)
endif()
