add_executable(goby_tool goby_tool.cpp unified_log_tool.cpp)
target_link_libraries(goby_tool goby)
set_target_properties(goby_tool PROPERTIES OUTPUT_NAME goby)
