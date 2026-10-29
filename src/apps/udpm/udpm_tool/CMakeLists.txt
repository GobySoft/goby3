add_executable(goby_udpm_tool
  udpm_tool.cpp
  )
target_link_libraries(goby_udpm_tool goby)

if(enable_yaml)
  target_link_libraries(goby_udpm_tool yaml-cpp)
endif()
