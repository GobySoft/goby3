add_executable(goby_zenoh_tool
  zenoh_tool.cpp
  )
target_link_libraries(goby_zenoh_tool goby goby_zenoh)
