if(enable_hdf5)
  add_subdirectory(goby_hdf5)
endif()

add_subdirectory(log_tool)

#if(enable_mavlink)
#  add_subdirectory(mavlink_serial_gateway)
#endif()
