add_subdirectory(middleware1)
add_subdirectory(middleware2)
add_subdirectory(middleware3)
add_subdirectory(middleware4)
add_subdirectory(middleware5)
add_subdirectory(middleware6)
add_subdirectory(middleware_regex)

add_subdirectory(single_thread_app1)
add_subdirectory(multi_thread_app1)
add_subdirectory(multi_thread_app2)

add_subdirectory(log)

if(enable_hdf5)
  add_subdirectory(hdf5)
endif()
