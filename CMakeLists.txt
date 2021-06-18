# t. schneider tes@mit.edu 
# base CMakeLists.txt file for goby
# for help on CMake, see http://www.cmake.org/cmake/help/documentation.html
# or type > man cmake

# this project is intended to be built out of source by using 
# > ./build.sh

cmake_minimum_required(VERSION 3.5.1 FATAL_ERROR)
project(goby C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## allows us to write custom modules or modifying existing ones
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

# enable sanitizers (e.g., Address Sanitizer)
find_package(Sanitizers)

option(enable_testing "Enable building of tests using CTest (if set to ON, you can run tests with 'make test')" OFF)
if(enable_testing)
  enable_testing()
  include(CTest)
endif()

# Check GCC version
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # require at least gcc 7.2
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.2)
      message(WARNING "Due to a GCC bug (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52036), the GCC version must be at least 7.2 (you have ${CMAKE_CXX_COMPILER_VERSION}). Attempting to use Clang instead")

      find_program(CLANG_C_BINARY clang)
      find_program(CLANG_CXX_BINARY clang++)     

      if(NOT CLANG_C_BINARY OR NOT CLANG_CXX_BINARY)
        message(FATAL_ERROR "Could not find clang. On Debian or Ubuntu use 'apt install clang'")
      else()
        message(STATUS "Found Clang: ${CLANG_C_BINARY}, ${CLANG_CXX_BINARY}")
      endif()
      
      set(CMAKE_C_COMPILER "${CLANG_C_BINARY}")
      set(CMAKE_CXX_COMPILER "${CLANG_CXX_BINARY}")

    endif()
endif()

# If we're using clang, use lld as well
if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  find_program(LLD_BINARY lld)
  if(LLD_BINARY)
    message(STATUS "Using Clang, setting to also use lld")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
  else()
    message(STATUS "Using Clang, but lld is not installed, so using system default (probably ld)")    
  endif()
endif()

set(GOBY_VERSION_MAJOR "3")
set(GOBY_VERSION_MINOR "0")
set(GOBY_VERSION_PATCH "0~beta18")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
   message(STATUS "Compiling in Git source tree.")
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
   set(GOBY_VERSION_PATCH "${GOBY_VERSION_PATCH}+${GOBY_REVS_SINCE_TAG}+g${GOBY_LAST_REV}${GOBY_DIRTY_REV_STRING}")
   
else()
   message(STATUS "Compiling from release tarball (${GOBY_VERSION_MAJOR}.${GOBY_VERSION_MINOR}.${GOBY_VERSION_PATCH}).")
   set(GOBY_VERSION_DATE "2021.06.18")
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


set(GOBY_DEBUG_OPTIONS -Wall -Werror)
add_compile_options("$<$<CONFIG:DEBUG>:${GOBY_DEBUG_OPTIONS}>")
add_compile_options("$<$<CXX_COMPILER_ID:Clang>:-Wdocumentation>")

# disable -Wmisleading-indentation for GCC > 6.0 as this triggers on Protobuf autogenerated code, and isn't meaningful for this project as we're using Clang format to enforce sane style
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_GREATER:$<CXX_COMPILER_VERSION>,6.0~>>:-Wno-misleading-indentation>")

## set type of libraries
option(BUILD_SHARED_LIBS "Build shared libraries (set OFF to build static libraries)." ON)

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
# override library version for debian sbuild cross-builds
if(CMAKE_CROSSCOMPILING)
  if (EXISTS "/etc/debian_version")
    set(CMAKE_INSTALL_LIBDIR "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
  endif()
endif()

include(GNUInstallDirs)

file(MAKE_DIRECTORY ${goby_BIN_DIR})
install(DIRECTORY ${goby_BIN_DIR}/ DESTINATION ${CMAKE_INSTALL_BINDIR}
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE )

install(FILES ${goby_SCRIPTS_DIR}/goby_launch DESTINATION ${CMAKE_INSTALL_BINDIR}
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE )

install(DIRECTORY ${goby_SHARE_DIR}/ DESTINATION ${CMAKE_INSTALL_PREFIX}/share)
install(DIRECTORY ${goby_INC_DIR}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING REGEX .*\\.h|.*\\.proto)

# clear library list
unset(GOBY_CORE_LIBRARY_LIST CACHE)
unset(GOBY_MOOS_LIBRARY_LIST CACHE)
unset(GOBY_ZEROMQ_LIBRARY_LIST CACHE)

## start adding subdirectories
add_subdirectory(src)

## configure to build/include
file(GLOB BASE_INCLUDE_FILES RELATIVE ${goby_SRC_DIR} src/*.h)
file(GLOB_RECURSE INCLUDE_FILES RELATIVE ${goby_SRC_DIR} 
  src/acomms/*.h
  src/middleware/*.h
  src/util/*.h 
  src/util/*.hpp
  src/moos/*.h 
  src/time/*.h
  src/protobuf/*.h
  src/zeromq/*.h
  )

file(GLOB_RECURSE PROTOBUF_FILES RELATIVE ${goby_SRC_DIR}
  src/*.proto
  )

foreach(I ${BASE_INCLUDE_FILES} ${INCLUDE_FILES} ${PROTOBUF_FILES})
  configure_file(${goby_SRC_DIR}/${I} ${goby_INC_DIR}/goby/${I} @ONLY)
endforeach()
configure_file(${goby_SRC_DIR}/goby.h ${goby_INC_DIR}/goby.h @ONLY)

if(NOT PROTOBUF_HAS_SYNTAX_VERSION)
  # erase the proto syntax version if not supported (protobuf before 3.0)
  foreach(I ${PROTOBUF_FILES})
    file(READ ${goby_INC_DIR}/goby/${I} CONTENTS)
    string(REGEX REPLACE "syntax *= *\\\"proto2\\\" *;" "" CONTENTS_OUTPUT "${CONTENTS}")
    file(WRITE ${goby_INC_DIR}/goby/${I} "${CONTENTS_OUTPUT}")
  endforeach()
endif()

## copy to build/share
file(GLOB SHARE_FILES RELATIVE ${goby_SRC_DIR} src/share/*)
foreach(I ${SHARE_FILES})
  file(COPY ${goby_SRC_DIR}/${I} DESTINATION ${goby_SHARE_DIR}/goby)
endforeach()


# configuration export - installed version
include(CMakePackageConfigHelpers)
write_basic_package_version_file(${CMAKE_BINARY_DIR}/goby-config-version.cmake
                                 VERSION ${GOBY_VERSION}
                                 COMPATIBILITY SameMajorVersion )
                               
install(EXPORT goby_core-config DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/goby)
configure_file(${goby_SRC_DIR}/goby-config.cmake.in ${CMAKE_BINARY_DIR}/goby-config.cmake @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/goby-config.cmake ${CMAKE_BINARY_DIR}/goby-config-version.cmake 
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/goby)

# configuration export - local build version
export(TARGETS ${GOBY_CORE_LIBRARY_LIST} FILE ${CMAKE_BINARY_DIR}/goby_core-config.cmake)
