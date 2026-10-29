add_executable(goby_zeromq_tool zeromq_tool.cpp)
target_link_libraries(goby_zeromq_tool goby goby_zeromq)

if(enable_yaml)
  target_link_libraries(goby_zeromq_tool yaml-cpp)
endif()
