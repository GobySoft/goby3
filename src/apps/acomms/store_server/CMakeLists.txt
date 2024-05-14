add_executable(goby_store_server store_server.cpp)
target_link_libraries(goby_store_server ${SQLITE3_LIBRARIES} goby)
