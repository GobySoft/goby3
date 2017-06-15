add_executable(goby_hdf5
  hdf5.cpp)


target_link_libraries(goby_hdf5
  ${Boost_LIBRARIES}
  ${HDF5_LIBRARIES}
  ${PROTOBUF_LIBRARIES}
  goby_common
)
