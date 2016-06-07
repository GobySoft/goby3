set(CMAKE_SKIP_RPATH OFF)

option(enable_testing_asio "Enable running of tests that use Boost ASIO (normally leave this ON - disabled for Ubuntu PPA building on quantal and raring because of Boost ASIO bug 7012)" ON)

option(enable_testing_zmq "Enable running of tests that use Boost ASIO (normally leave this ON - disabled for Ubuntu PPA building on saucy" ON)

if(build_acomms)
  add_subdirectory(acomms)
endif()

if(build_common)
  add_subdirectory(common)
  add_subdirectory(sandbox)
endif()

if(build_util)
  add_subdirectory(util)
endif()

if(build_moos)
  add_subdirectory(moos)
endif()

if(build_pb)
  add_subdirectory(pb)
endif()
