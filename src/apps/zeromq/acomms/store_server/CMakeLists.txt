add_executable(goby_store_server goby_store_server.cpp)
target_link_libraries(goby_store_server ${SQLITE3_LIBRARIES} goby goby_zeromq)
