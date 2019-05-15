add_subdirectory(abc_modem_simulator)


# for now, we need to be able to link in UDPDriver and PBDriver
# at some point, make these dynamically loadable
#if(build_pb AND enable_zeromq)
#  add_subdirectory(goby_bridge)
#  add_subdirectory(goby_file_transfer)
#  add_subdirectory(goby_modemdriver)
#  add_subdirectory(goby_mosh_relay)
#  if(enable_sqlite)
#    add_subdirectory(goby_store_server)
#  endif()
#  add_subdirectory(goby_ip_gateway)
#endif()

