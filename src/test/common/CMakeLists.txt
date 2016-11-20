add_subdirectory(log)

if(enable_hdf5)
  add_subdirectory(hdf5)
endif()

if(enable_zeromq)
  # this test fails to pass during Ubuntu automated builds
  # since Goby 2 doesn't use PGM, this is ok for now
  #add_subdirectory(zero_mq_node1)
  add_subdirectory(zero_mq_node2)
  add_subdirectory(zero_mq_node3)
  # there's a problem with this test failing based on clock parameters
  #add_subdirectory(zero_mq_node4)
endif()
