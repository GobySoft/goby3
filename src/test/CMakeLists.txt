add_subdirectory(time)
add_subdirectory(acomms)
add_subdirectory(util)
add_subdirectory(middleware)
add_subdirectory(udpm)

if(build_python)
  add_subdirectory(python)
endif()

if(build_zeromq)
  add_subdirectory(zeromq)
endif()

if(build_moos)
  add_subdirectory(moos)
endif()
