add_executable(goby_logger logger.cpp)
target_link_libraries(goby_logger goby goby_zeromq)
generate_middleware_interfaces(goby_logger)


add_executable(goby_playback playback.cpp)
target_link_libraries(goby_playback goby goby_zeromq)
