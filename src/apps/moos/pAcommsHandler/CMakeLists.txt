add_executable(pAcommsHandler
  pAcommsHandlerMain.cpp
  pAcommsHandler.cpp)
  
target_link_libraries(pAcommsHandler
  goby
  goby_moos
  ${MOOS_LIBRARIES}
  ${Boost_LIBRARIES}
  ${PROTOBUF_LIBRARY}
  ${CMAKE_DL_LIBS}
)
