
# for now, we need to be able to link in UDPDriver and PBDriver
# at some point, make these dynamically loadable
option(build_pAcommsHandler "Build pAcommsHandler" ON)
option(build_pTranslator "Build pTranslator" ON)
option(build_iFrontSeat "Build iFrontSeat" ON)


if(build_pAcommsHandler)
  add_subdirectory(pAcommsHandler) # boost, xercesc, asio
endif()

if(build_pTranslator)
  add_subdirectory(pTranslator) 
endif()

if(build_iFrontSeat)
  add_subdirectory(iFrontSeat) 
  add_subdirectory(abc_frontseat_simulator)
endif()

option(build_pGobyMOOSAppTemplate "Build pGobyMOOSAppTemplate - example Goby MOOS App" OFF)

if(build_pGobyMOOSAppTemplate)
  add_subdirectory(pGobyMOOSAppTemplate) 
endif()

if(build_zeromq)
  add_subdirectory(goby_moos_gateway)
endif()
