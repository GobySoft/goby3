if(enable_hdf5)
  add_subdirectory(hdf5)
endif()

add_subdirectory(log_tool)
add_subdirectory(clang_tool)
add_subdirectory(basic_frontseat_simulator)

add_subdirectory(serial_mux)

add_subdirectory(goby_tool)
