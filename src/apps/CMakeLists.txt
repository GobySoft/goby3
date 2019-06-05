add_subdirectory(acomms)
add_subdirectory(util)
add_subdirectory(middleware)

if(build_zeromq)
  add_subdirectory(zeromq)
endif()

if(build_moos)
  add_subdirectory(moos)
endif()
