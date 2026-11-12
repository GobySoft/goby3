add_executable(goby_test_mmdriver1 test.cpp ../driver_tester/driver_tester.cpp)
target_link_libraries(goby_test_mmdriver1 goby)

# can't really run this test automatically
# add_test(test_mmdriver1 test_mmdriver1)
