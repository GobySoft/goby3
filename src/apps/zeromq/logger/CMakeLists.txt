add_executable(goby_logger logger.cpp)
target_link_libraries(goby_logger goby goby_zeromq)
