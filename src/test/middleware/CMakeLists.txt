add_subdirectory(middleware_interthread)

add_subdirectory(log)

if(enable_hdf5)
  add_subdirectory(hdf5)
endif()

if(enable_mavlink)
  add_subdirectory(mavlink)
endif()
