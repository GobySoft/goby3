add_subdirectory(acomms)
add_subdirectory(common)
add_subdirectory(util)
add_subdirectory(middleware)

if(build_moos)
  add_subdirectory(moos)
endif()
