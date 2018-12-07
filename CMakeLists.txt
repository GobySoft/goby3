# t. schneider tes@mit.edu 
# base CMakeLists.txt file for goby
# for help on CMake, see http://www.cmake.org/cmake/help/documentation.html
# or type > man cmake

# this project is intended to be built out of source by using 
# > ./build.sh

cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)
cmake_policy(SET CMP0009 NEW)
project(goby)

if(CMAKE_VERSION VERSION_LESS 3.1)
  add_definitions(-std=c++14)
else()
  set(CMAKE_CXX_STANDARD 14)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

## allows us to write custom modules or modifying existing ones
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")


option(enable_testing "Enable building of tests using CTest (if set to ON, you can run tests with 'make test')" OFF)
if(enable_testing)
  enable_testing()
  include(CTest)
endif()


set(GOBY_VERSION_MAJOR "3")
set(GOBY_VERSION_MINOR "0")
set(GOBY_VERSION_PATCH "0~alpha14")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
   message("Compiling in Git source tree.")
   include(today)
   today(GOBY_VERSION_DATE)  
   execute_process(COMMAND git rev-parse --short HEAD
     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
     OUTPUT_VARIABLE GOBY_LAST_REV)
   string(STRIP ${GOBY_LAST_REV} GOBY_LAST_REV)

   string(REPLACE "~" "_" GOBY_GIT_VERSION ${GOBY_VERSION_MAJOR}.${GOBY_VERSION_MINOR}.${GOBY_VERSION_PATCH}) 
   execute_process(COMMAND git rev-list ${GOBY_GIT_VERSION}..HEAD --count
     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
     OUTPUT_VARIABLE GOBY_REVS_SINCE_TAG)
   string(STRIP ${GOBY_REVS_SINCE_TAG} GOBY_REVS_SINCE_TAG)
      
   execute_process(COMMAND git diff-index --quiet HEAD
     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
     RESULT_VARIABLE GOBY_DIRTY_REV)
   if(GOBY_DIRTY_REV EQUAL 0)
     set(GOBY_DIRTY_REV_STRING "")
   else()
     set(GOBY_DIRTY_REV_STRING "-dirty")
   endif()
   set(GOBY_VERSION_PATCH "${GOBY_VERSION_PATCH}+${GOBY_REVS_SINCE_TAG}+git${GOBY_LAST_REV}${GOBY_DIRTY_REV_STRING}")
   
else()
   message("Compiling from release tarball.")
   set(GOBY_VERSION_DATE "2018.12.07")
endif()

set(GOBY_VERSION "${GOBY_VERSION_MAJOR}.${GOBY_VERSION_MINOR}.${GOBY_VERSION_PATCH}")

# give Goby 1 series a few more soversions
set(GOBY_SOVERSION "30")

# create variables for various directories
get_filename_component(goby_SRC_DIR src ABSOLUTE)
get_filename_component(goby_BUILD_DIR ${CMAKE_BINARY_DIR} ABSOLUTE)
get_filename_component(goby_SCRIPTS_DIR scripts ABSOLUTE)

# fetch all the local directories for generated code
get_filename_component(goby_LIB_DIR ${goby_BUILD_DIR}/lib ABSOLUTE)
get_filename_component(goby_BIN_DIR ${goby_BUILD_DIR}/bin ABSOLUTE)
get_filename_component(goby_INC_DIR ${goby_BUILD_DIR}/include ABSOLUTE)
get_filename_component(goby_SHARE_DIR ${goby_BUILD_DIR}/share ABSOLUTE)

# symlink these into the build directory to provide backwards compatibility with the former locations (<= version 2.1.1)
get_filename_component(goby_FORMER_LIB_DIR lib ABSOLUTE)
get_filename_component(goby_FORMER_BIN_DIR bin ABSOLUTE)
get_filename_component(goby_FORMER_INC_DIR include ABSOLUTE)
get_filename_component(goby_FORMER_SHARE_DIR share ABSOLUTE)
add_custom_command(OUTPUT LibLink COMMAND ${CMAKE_COMMAND} -E create_symlink ${goby_LIB_DIR} ${goby_FORMER_LIB_DIR})
add_custom_target(lib_link ALL DEPENDS LibLink)
add_custom_command(OUTPUT BinLink COMMAND ${CMAKE_COMMAND} -E create_symlink ${goby_BIN_DIR} ${goby_FORMER_BIN_DIR})
add_custom_target(bin_link ALL DEPENDS BinLink)
add_custom_command(OUTPUT IncludeLink COMMAND ${CMAKE_COMMAND} -E create_symlink ${goby_INC_DIR} ${goby_FORMER_INC_DIR})
add_custom_target(inc_link ALL DEPENDS IncludeLink)
add_custom_command(OUTPUT ShareLink COMMAND ${CMAKE_COMMAND} -E create_symlink ${goby_SHARE_DIR} ${goby_FORMER_SHARE_DIR})
add_custom_target(share_link ALL DEPENDS ShareLink)


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall") # -fprofile-arcs -ftest-coverage")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall") # -fprofile-arcs -ftest-coverage")
#set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fprofile-arcs -ftest-coverage")

## set type of libraries
option(make_static_libs "Build static libraries instead of shared." OFF)

if(make_static_libs)
  set(BUILD_SHARED_LIBS OFF)
else()
  set(BUILD_SHARED_LIBS ON)
endif()

## build dynamic protobuf manager with ability to read from disk (requires
## boost-filesystem)
option(make_proto_runtime_compile "Allow runtime compilation of .proto files." ON)


## set the cmake defaults for libraries and binaries
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${goby_LIB_DIR} CACHE PATH 
  "Output directory for the dynamic libraries" )
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${goby_BIN_DIR} CACHE PATH
  "Output directory for the binaries" )
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${goby_LIB_DIR} CACHE PATH 
  "Output directory for the static libraries (archives)" )

## let cmake know where the headers are
include_directories(${goby_INC_DIR})

## for MAC OS X 
if(${APPLE})
	if( EXISTS /opt/local/include AND EXISTS /opt/local/lib )
    message(status "Detected use of 'MacPorts' for dependencies")
		include_directories(/opt/local/include)
		link_directories(/opt/local/lib)
	endif()
	if( EXISTS /sw/include AND EXISTS /sw/lib )
    message(status "Detected use of 'Fink' for dependencies")
		include_directories(/sw/include)
		link_directories(/sw/lib)
	endif()
  if( EXISTS /usr/local/Cellar )
    message(status "Detected use of 'Homebrew' for dependencies")
		include_directories(/usr/local/include)
		link_directories(/usr/local/lib)
	endif()
endif()

set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES "${goby_BIN_DIR}" "${goby_LIB_DIR}" "${goby_INC_DIR}" "${goby_SHARE_DIR}")

## set instructions for `make install`
file(MAKE_DIRECTORY ${goby_BIN_DIR})
install(DIRECTORY ${goby_BIN_DIR}/ DESTINATION ${CMAKE_INSTALL_PREFIX}/bin 
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE )

install(FILES ${goby_SCRIPTS_DIR}/analyze_dccl_xml ${goby_SCRIPTS_DIR}/moos_goby_liaison ${goby_SCRIPTS_DIR}/goby_launch DESTINATION ${CMAKE_INSTALL_PREFIX}/bin 
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE )

install(DIRECTORY ${goby_SHARE_DIR}/ DESTINATION ${CMAKE_INSTALL_PREFIX}/share/goby)
install(DIRECTORY ${goby_INC_DIR}/ DESTINATION ${CMAKE_INSTALL_PREFIX}/include
  FILES_MATCHING REGEX .*\\.h|.*\\.proto)

# clear library list
unset(GOBY_LIBRARY_LIST CACHE)

## start adding subdirectories
add_subdirectory(src)

## configure to build/include
file(GLOB BASE_INCLUDE_FILES RELATIVE ${goby_SRC_DIR} src/*.h)
file(GLOB_RECURSE INCLUDE_FILES RELATIVE ${goby_SRC_DIR} 
  src/acomms/*.h
  src/common/*.h 
  src/middleware/*.h
  src/util/*.h 
  src/moos/*.h 
  src/pb/*.h 
  )

file(GLOB_RECURSE PROTOBUF_FILES RELATIVE ${goby_SRC_DIR}
  src/*.proto
  )

foreach(I ${BASE_INCLUDE_FILES} ${INCLUDE_FILES} ${PROTOBUF_FILES})
  configure_file(${goby_SRC_DIR}/${I} ${goby_INC_DIR}/goby/${I} @ONLY)
endforeach()

# write the proto syntax version if required 
foreach(I ${PROTOBUF_FILES})
  file(READ ${goby_INC_DIR}/goby/${I} CONTENTS)
  file(WRITE ${goby_INC_DIR}/goby/${I} "${PROTOBUF_SYNTAX_VERSION}\n${CONTENTS}")
endforeach()


## copy to build/share
file(GLOB SHARE_FILES RELATIVE ${goby_SRC_DIR} src/share/*)
foreach(I ${SHARE_FILES})
  file(COPY ${goby_SRC_DIR}/${I} DESTINATION ${goby_SHARE_DIR})
endforeach()


# configuration export - installed version
install(EXPORT goby-config DESTINATION share/goby/cmake)
# configuration export - local build version
export(TARGETS ${GOBY_LIBRARY_LIST} FILE ${CMAKE_BINARY_DIR}/goby-config.cmake)
