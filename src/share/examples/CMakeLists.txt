cmake_minimum_required(VERSION 3.5.1 FATAL_ERROR)
project(goby-examples)


set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake_modules")
set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/../..;${CMAKE_PREFIX_PATH}")

find_package(ProtobufGobyExamples REQUIRED)

# DCCL
find_package(DCCL REQUIRED)
get_target_property(DCCL_INCLUDE_DIR dccl INTERFACE_INCLUDE_DIRECTORIES)

message("Using DCCL in ${DCCL_INCLUDE_DIR}")
include_directories("${DCCL_INCLUDE_DIR}")
protobuf_include_dirs("${DCCL_INCLUDE_DIR}")

# GOBY
find_package(GOBY REQUIRED)
get_target_property(GOBY_INCLUDE_DIR goby_common INTERFACE_INCLUDE_DIRECTORIES)

message("Using Goby in ${GOBY_INCLUDE_DIR}")
include_directories("${GOBY_INCLUDE_DIR}")
protobuf_include_dirs("${GOBY_INCLUDE_DIR}")


set(CURSES_NEED_NCURSES TRUE)
find_package(Curses)
if(CURSES_FOUND)
  add_definitions(-DHAS_NCURSES)
endif()

find_package(Boost 1.36.0 COMPONENTS system date_time thread program_options filesystem signals)

add_subdirectory(acomms)
add_subdirectory(util)
add_subdirectory(moos)
