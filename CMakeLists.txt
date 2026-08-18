# t. schneider tes@mit.edu 
# base CMakeLists.txt file for goby
# for help on CMake, see http://www.cmake.org/cmake/help/documentation.html
# or type > man cmake

# this project is intended to be built out of source by using 
# > ./build.sh

cmake_minimum_required(VERSION 3.7...4.2.3 FATAL_ERROR)
project(goby C CXX)

if(((NOT DEFINED CMAKE_CXX_STANDARD) OR ("${CMAKE_CXX_STANDARD}" STREQUAL "03") OR ("${CMAKE_CXX_STANDARD}" STREQUAL "11") OR ("${CMAKE_CXX_STANDARD}" STREQUAL "14")))
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  message ("Setting CMAKE_CXX_STANDARD to 17 as required by Goby. If you wish to use a newer standard, set CMAKE_CXX_STANDARD directly")
endif()

## allows us to write custom modules or modifying existing ones
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

# enable sanitizers (e.g., Address Sanitizer)
find_package(Sanitizers)

option(enable_testing "Enable building of tests using CTest (if set to ON, you can run tests with 'make test')" OFF)
if(enable_testing)
  enable_testing()
  include(CTest)
endif()

## Python language bindings (see src/python and src/middleware/languages/python)
set(GOBY_PYTHON_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/python CACHE PATH "Source directory of the goby Python package")
include(GobyPython)
set(PYTHON_DOC_STRING "Build support for Goby applications written in Python (requires python3-dev and pybind11-dev)")
if(Python3_FOUND AND pybind11_FOUND)
  option(build_python ${PYTHON_DOC_STRING} ON)
else()
  option(build_python ${PYTHON_DOC_STRING} OFF)
  message(">> setting build_python to OFF ... if you need this functionality: 1) install python3-dev and pybind11-dev; 2) run cmake -Dbuild_python=ON")
endif()
option(GOBY_INSTALL_PYTHON_RUNTIME "Install the goby Python package as part of 'make install'. Set OFF when something else installs it, e.g. pybuild when building the python3-goby3 Debian package." ON)

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
set(GOBY_VERSION_MINOR "5")
set(GOBY_VERSION_PATCH "1")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
   message(STATUS "Compiling in Git source tree.")
   include(today)
   today(GOBY_VERSION_DATE)  
   execute_process(COMMAND git rev-parse --short HEAD
     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
     OUTPUT_VARIABLE GOBY_LAST_REV
     RESULT_VARIABLE GOBY_LAST_REV_RESULT
     OUTPUT_STRIP_TRAILING_WHITESPACE)
   if(NOT GOBY_LAST_REV_RESULT EQUAL 0 OR "${GOBY_LAST_REV}" STREQUAL "")
     set(GOBY_LAST_REV "unknown")
   endif()

   string(REPLACE "~" "_" GOBY_GIT_VERSION ${GOBY_VERSION_MAJOR}.${GOBY_VERSION_MINOR}.${GOBY_VERSION_PATCH})
   # this fails (e.g. tag not present) on a shallow clone that hasn't fetched tags,
   # so fall back to a rev count of 0 rather than aborting the configure
   execute_process(COMMAND git rev-list ${GOBY_GIT_VERSION}..HEAD --count
     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
     OUTPUT_VARIABLE GOBY_REVS_SINCE_TAG
     RESULT_VARIABLE GOBY_REVS_SINCE_TAG_RESULT
     ERROR_QUIET
     OUTPUT_STRIP_TRAILING_WHITESPACE)
   if(NOT GOBY_REVS_SINCE_TAG_RESULT EQUAL 0 OR "${GOBY_REVS_SINCE_TAG}" STREQUAL "")
     message(STATUS "Could not determine revisions since tag ${GOBY_GIT_VERSION} (shallow clone without tags?); defaulting to 0")
     set(GOBY_REVS_SINCE_TAG "0")
   endif()
      
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
   set(GOBY_VERSION_DATE "2026.06.04")
endif()

set(GOBY_VERSION "${GOBY_VERSION_MAJOR}.${GOBY_VERSION_MINOR}.${GOBY_VERSION_PATCH}")

# julia can't handle our git version strings - mutate them to conform
## remove the second '+' (combine revision and hash)
string(REGEX REPLACE "\\+(g[0-9a-f]+)" "\\1" GOBY_VERSION_PATCH_JULIA_TMP "${GOBY_VERSION_PATCH}")
## remove remaining '-' (e.g., '-dirty' -> 'dirty')
string(REPLACE "-" "" GOBY_VERSION_PATCH_JULIA "${GOBY_VERSION_PATCH_JULIA_TMP}")

# PEP 440 allows only one '+' (introducing the local version) and no '-' within it
## replace the second '+' with '.' (e.g. '1+12+gabc1234' -> '1+12.gabc1234')
string(REGEX REPLACE "\\+(g[0-9a-f]+)" ".\\1" GOBY_VERSION_PATCH_PYTHON_TMP "${GOBY_VERSION_PATCH}")
## replace remaining '-' with '.' (e.g. '-dirty' -> '.dirty')
string(REPLACE "-" "." GOBY_VERSION_PATCH_PYTHON "${GOBY_VERSION_PATCH_PYTHON_TMP}")

# give Goby 1 series a few more soversions
set(GOBY_SOVERSION "30")

# Increment on changes to internal DCCL messages
set(GOBY_INTERVEHICLE_API_VERSION "1")

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


set(GOBY_DEBUG_OPTIONS -Wall -Werror -Wno-sign-compare)
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

install(
  DIRECTORY ${goby_INC_DIR}/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    REGEX ".*\\.(h|hpp|proto)$"
  PATTERN "*/test/*" EXCLUDE
)

# clear library list
unset(GOBY_CORE_LIBRARY_LIST CACHE)
unset(GOBY_MOOS_LIBRARY_LIST CACHE)
unset(GOBY_ZEROMQ_LIBRARY_LIST CACHE)

## start adding subdirectories
## Python bindings
if(build_python)
  include(GobyPythonPackage)
endif()

## copy to build/share
# ahead of the subdirectories because GobyJulia below reads Goby.jl out of the share directory
file(GLOB SHARE_FILES RELATIVE ${goby_SRC_DIR} src/share/*)
foreach(I ${SHARE_FILES})
  file(COPY ${goby_SRC_DIR}/${I} DESTINATION ${goby_SHARE_DIR}/goby)
endforeach()

# configure Project.toml for Julia version
configure_file(${goby_SRC_DIR}/share/Goby.jl/Project.toml ${goby_SHARE_DIR}/goby/Goby.jl/Project.toml @ONLY)

# the copy above is what the generators run, so editing Goby.jl has to re-trigger it: otherwise
# the tests keep passing against the previous version of the generator
file(GLOB GOBY_JULIA_SOURCES ${goby_SRC_DIR}/share/Goby.jl/src/*.jl)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GOBY_JULIA_SOURCES})

## Julia bindings (see src/share/Goby.jl and src/middleware/languages/julia)
include(GobyJulia)
set(JULIA_DOC_STRING "Build support for Goby applications written in Julia (requires julia and CxxWrap.jl)")
if(GOBY_JULIA_FOUND)
  option(build_julia ${JULIA_DOC_STRING} ON)
else()
  option(build_julia ${JULIA_DOC_STRING} OFF)
  message(">> setting build_julia to OFF ... ${GOBY_JULIA_NOT_FOUND_REASON}")
endif()

if(build_julia)
  if(NOT GOBY_JULIA_FOUND)
    message(FATAL_ERROR "build_julia=ON but ${GOBY_JULIA_NOT_FOUND_REASON}")
  endif()
  # the generators run out of the build tree copy of Goby.jl, so its dependencies have to be
  # installed by the ordinary build rather than only when something declares a Julia application
  add_custom_target(goby_julia ALL)
  add_dependencies(goby_julia goby_julia_pkgs)
endif()

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
  src/udpm/*.h
  )

file(GLOB_RECURSE PROTOBUF_FILES RELATIVE ${goby_SRC_DIR}
  src/*.proto
  )

foreach(I ${BASE_INCLUDE_FILES} ${INCLUDE_FILES} ${PROTOBUF_FILES})
  configure_file(${goby_SRC_DIR}/${I} ${goby_INC_DIR}/goby/${I} @ONLY)
endforeach()
configure_file(${goby_SRC_DIR}/goby.h ${goby_INC_DIR}/goby.h @ONLY)

# usable by downstream projects through find_package(goby)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GobyPython.cmake
              ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GobyJulia.cmake
              ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GobyTarget.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/goby)
# goby-config.cmake looks for them next to itself, which for an uninstalled Goby is the build directory
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GobyPython.cmake
  ${CMAKE_BINARY_DIR}/GobyPython.cmake COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GobyJulia.cmake
  ${CMAKE_BINARY_DIR}/GobyJulia.cmake COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GobyTarget.cmake
  ${CMAKE_BINARY_DIR}/GobyTarget.cmake COPYONLY)

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
