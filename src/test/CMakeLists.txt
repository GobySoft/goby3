set(CMAKE_SKIP_RPATH OFF)

if(build_acomms)
  add_subdirectory(acomms)
endif()

if(build_common)
  add_subdirectory(common)
endif()

if(build_util)
  add_subdirectory(util)
endif()

if(build_middleware)
  add_subdirectory(middleware)
endif()

if(build_moos)
  add_subdirectory(moos)
endif()

if(build_pb)
  add_subdirectory(pb)
endif()
