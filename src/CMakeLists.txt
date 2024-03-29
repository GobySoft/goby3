## set up function for external libraries errors
macro(goby_find_required_package package_name)
  find_package(${package_name} ${ARGN})
  string(TOUPPER ${package_name} package_name_upper)
  if(NOT ${package_name_upper}_FOUND AND NOT ${package_name}_FOUND)
    message(FATAL_ERROR "missing required external library: ${package_name}. Please run/read the goby/DEPENDENCIES script to resolve.")
  endif()
endmacro()

## find external packages, libraries, and headers for the entire goby project
#mandatory
goby_find_required_package(ProtobufGoby)
include_directories(${PROTOBUF_INCLUDE_DIRS})

string(STRIP ${PROTOC_VERSION} PROTOC_VERSION_CLEAN)
message(STATUS "Using Protocol Buffers ${PROTOC_VERSION_CLEAN}: ${PROTOBUF_INCLUDE_DIRS}")

if(${PROTOC_VERSION} VERSION_LESS 3.0.0) 
  message(STATUS "\tlibprotobuf < 3.0.0, removing syntax= string")
  set(PROTOBUF_HAS_SYNTAX_VERSION FALSE PARENT_SCOPE)
else()
  set(PROTOBUF_HAS_SYNTAX_VERSION TRUE PARENT_SCOPE)
endif()

#dccl
goby_find_required_package(DCCL)
message(STATUS "Using DCCL in ${DCCL_DIR}")

if(NOT DEFINED DCCL_INCLUDE_DIR)
  #for DCCL 3.0.3 and newer
  get_target_property(DCCL_INCLUDE_DIR dccl INTERFACE_INCLUDE_DIRECTORIES)
endif()

if(DCCL_INCLUDE_DIR)
  message(STATUS "\tIncluding DCCL protobuf dir: ${DCCL_INCLUDE_DIR}")
  include_directories("${DCCL_INCLUDE_DIR}")
  protobuf_include_dirs("${DCCL_INCLUDE_DIR}")
endif()

#proj
goby_find_required_package(Proj)

#shared library suffix
add_definitions(-DSHARED_LIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")

#optional
## ncurses
set(CURSES_USE_NCURSES TRUE)
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses QUIET)
set(CURSES_DOC_STRING "Enable NCurses GUI components (requires libncurses-dev)")
if(CURSES_FOUND)
  option(enable_ncurses ${CURSES_DOC_STRING} ON)
else()
  option(enable_ncurses ${CURSES_DOC_STRING} OFF)
  message(">> setting enable_ncurses to OFF ... if you need this functionality: 1) install libncurses-dev; 2) run cmake -Denable_ncurses=ON")
endif()

if(enable_ncurses)
  goby_find_required_package(Curses)  
  add_definitions(-DHAS_NCURSES)
  include_directories(${CURSES_INCLUDE_DIR})
endif()
mark_as_advanced(CURSES_CURSES_H_PATH CURSES_FORM_LIBRARY CURSES_HAVE_CURSES_H)


#GMP
find_package(GMP QUIET)
set(GMP_DOC_STRING "Use GMP instead of boost::multiprecision::cpp_int (faster, but requires libgmp-dev)")
if(GMP_FOUND)
  option(enable_gmp ${GMP_DOC_STRING} ON)
else()
  option(enable_gmp ${GMP_DOC_STRING} OFF)
  message(">> setting enable_gmp to OFF ... if you need this functionality: 1) install libgmp-dev; 2) run cmake -Denable_gmp=ON")
endif()

if(enable_gmp)
  goby_find_required_package(GMP)  
  add_definitions(-DHAS_GMP)
endif()

## Sqlite3
find_package(Sqlite3 QUIET)
set(SQLITE_DOC_STRING "Enable SQLite3 database components (requires libsqlite3-dev: http://www.sqlite.org)")
if(SQLITE3_FOUND)
  option(enable_sqlite ${SQLITE_DOC_STRING} ON)
else()
  option(enable_sqlite ${SQLITE_DOC_STRING} OFF)
  message(">> setting enable_sqlite to OFF ... if you need this functionality: 1) install libsqlite3-dev; 2) run cmake -Denable_sqlite=ON")
endif()

## HDF5
find_package(HDF5 QUIET COMPONENTS C CXX)
set(HDF5_DOC_STRING "Enable HDF5 file format tools (requires libhdf5-dev: https://www.hdfgroup.org/HDF5)")
if(HDF5_FOUND)
  option(enable_hdf5 ${HDF5_DOC_STRING} ON)
  include_directories(SYSTEM ${HDF5_INCLUDE_DIRS})
  add_definitions(${HDF5_DEFINITIONS})
else()
  option(enable_hdf5 ${HDF5_DOC_STRING} OFF)
  message(">> setting enable_hdf5 to OFF ... if you need this functionality: 1) install libhdf5-dev; 2) run cmake -Denable_hdf5=ON")
endif()

if(enable_hdf5)
  goby_find_required_package(HDF5)  
  add_definitions(-DHAS_HDF5)
endif()

# OpenSSL
find_package(OpenSSL QUIET)
set(OPENSSL_DOC_STRING "Enable OpenSSL components")
if(OPENSSL_FOUND)
  option(enable_openssl ${OPENSSL_DOC_STRING} ON)
else()
  option(enable_openssl ${OPENSSL_DOC_STRING} OFF)
  message(">> setting enable_openssl to OFF ... if you need this functionality: 1) install libopenssl-dev; 2) run cmake -Denable_openssl=ON")
endif()

if(enable_openssl)
  goby_find_required_package(OpenSSL)
  add_definitions(-DHAS_OPENSSL)
endif()

## Wt
find_package(WtGoby QUIET)
set(WT_DOC_STRING "Enable Wt web browser GUI components (requires libwt-dev, libwtdbo-dev, libwtdbosqlite-dev, and libwthttp-dev: http://www.webtoolkit.eu/wt)")
#find resources directory
find_path(Wt_RESOURCES_DIR_BASE share/Wt/resources/plus.gif /usr /usr/local)
if(WTGOBY_FOUND AND Wt_RESOURCES_DIR_BASE)
  option(enable_wt ${WT_DOC_STRING} ON)
else()
  option(enable_wt ${WT_DOC_STRING} OFF)
  message(">> setting enable_wt to OFF ... if you need this functionality: 1) install libwt-dev, libwtdbo-dev, libwtdbosqlite-dev, and libwthttp-dev; 2) run cmake -Denable_wt=ON")
endif()

if(enable_wt)
  goby_find_required_package(WtGoby)    
  include_directories(${Wt_INCLUDE_DIR})
  set(Wt_RESOURCES_DIR "${Wt_RESOURCES_DIR_BASE}/share/Wt/resources")
  add_custom_command(OUTPUT WtResourcesLink COMMAND ${CMAKE_COMMAND} -E create_symlink ${Wt_RESOURCES_DIR} ${goby_SHARE_DIR}/goby/liaison/resources)
  add_custom_target(wt_link ALL DEPENDS WtResourcesLink)
endif()  

## Mavlink
set(MAVLINK_DOC_STRING "Build the MAVLink marshalling language support library (requires MavLink C++11 v2.0 headers, libmavlink-dev)")
find_package(MAVLINK QUIET)
if(MAVLINK_FOUND)
  include_directories(SYSTEM ${MAVLINK_INCLUDE_DIR})
  message(STATUS "Using MAVLink in ${MAVLINK_INCLUDE_DIR}")
  option(enable_mavlink ${MAVLINK_DOC_STRING} ON)
else()
  option(enable_mavlink ${MAVLINK_DOC_STRING} OFF)
  message(">> setting enable_mavlink to OFF ... if you need this functionality: 1) install MAVLink v2.0 C++11 headers (\"apt install libmavlink-dev\" from packages.gobysoft.org); 2) run cmake -Denable_mavlink=ON")
endif()

## AIS library
set(AIS_DOC_STRING "Build the Automatic Identification System (AIS) parsing components (requires libais-dev)")
find_package(AIS QUIET)
if(AIS_FOUND)
  include_directories(SYSTEM ${AIS_INCLUDE_DIR})
  message(STATUS "Using libais in ${AIS_INCLUDE_DIR}")
  option(enable_ais ${AIS_DOC_STRING} ON)
else()
  option(enable_ais ${AIS_DOC_STRING} OFF)
  message(">> setting enable_ais to OFF ... if you need this functionality: 1) install libais (\"apt install libais-dev\" from packages.gobysoft.org or from source at https://github.com/GobySoft/libais); 2) run cmake -Denable_ais=ON")
endif()

## LLVM (for goby_clang_tool)
include(FindLLVMForGoby)

macro(generate_middleware_interfaces target) 
  if(enable_llvm)
    goby_export_interface(${target} ${goby_INTERFACES_DIR} ${target}_YML_OUT)
    add_custom_target(${target}_interface ALL DEPENDS ${${target}_YML_OUT})
  endif()
endmacro()

## Mysql (for GEOV)
set(MYSQL_DOC_STRING "Build the MySQL components (requires libmysqlclient-dev)")
find_package(MYSQL QUIET)
if(MYSQL_FOUND)
  include_directories(SYSTEM ${MYSQL_INCLUDE_DIR})
  message(STATUS "Using libmysqlclient in ${MYSQL_INCLUDE_DIR}")
  option(enable_mysql ${MYSQL_DOC_STRING} ON)
else()
  message(">> setting enable_mysql to OFF ... if you need this functionality: 1) install libmysqlclient-dev (\"apt install libmysqlclient-dev\"; 2) run cmake -Denable_mysql=ON")
  option(enable_mysql ${MYSQL_DOC_STRING} OFF)
endif()

## set flags
macro(goby_install_lib target_lib component)
  set_property(TARGET ${target_lib} APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    $<BUILD_INTERFACE:${goby_INC_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>  # <prefix>/include
    )

  set(config_target "goby_${component}-config")
  string(TOUPPER ${component} component_upper)
  set(library_list "GOBY_${component_upper}_LIBRARY_LIST")
  
  install(TARGETS ${target_lib} EXPORT ${config_target}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  list(APPEND ${library_list} ${target_lib})
  set(${library_list} ${${library_list}} CACHE INTERNAL "${library_list}")
endmacro()

## boost
goby_find_required_package(Boost 1.58.0 COMPONENTS system date_time program_options filesystem)
include_directories(${Boost_INCLUDE_DIRS})

protobuf_generate_cpp(BASE_PROTO_SRCS BASE_PROTO_HDRS 
  protobuf/option_extensions.proto
  )


include(acomms/src.cmake)
include(util/src.cmake)
include(time/src.cmake)
include(middleware/src.cmake)

add_library(goby
  ${BASE_PROTO_SRCS} ${BASE_PROTO_HDRS}
  ${ACOMMS_SRC}
  ${UTIL_SRC}
  ${TIME_SRC}
  ${MIDDLEWARE_SRC}
  )

target_link_libraries(goby
  dccl
  dccl_arithmetic
  ${Boost_LIBRARIES}
  ${PROTOBUF_LIBRARY}
  ${PROJ_LIBRARY}
  )

if(enable_gmp)
  target_link_libraries(goby ${GMP_LIBRARIES})
endif()

if(enable_openssl)
  target_link_libraries(goby ${OPENSSL_LIBRARIES})
endif()

if(enable_ncurses)
  target_link_libraries(goby ${CURSES_LIBRARIES})
endif()

if(enable_ais)
  target_link_libraries(goby ${AIS_LIBRARIES})
endif()


if(enable_hdf5)
  target_link_libraries(goby ${HDF5_CXX_LIBRARIES})
endif()

set_target_properties(goby PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

goby_install_lib(goby core)

## zeromq
find_package(ZeroMQ QUIET)
set(ZEROMQ_DOC_STRING "Enable ZeroMQ Transport Layer components (requires libzmq3-dev: http://www.zeromq.org/)")
if(ZEROMQ_FOUND)
  option(build_zeromq ${ZEROMQ_DOC_STRING} ON)
else()
  option(build_zeromq ${ZEROMQ_DOC_STRING} OFF)
  message(">> setting build_zeromq to OFF ... if you need this functionality: 1) install libzmq-dev; 2) run cmake -Dbuild_zeromq=ON")
endif()

if(build_zeromq)
  goby_find_required_package(ZeroMQ)
  include_directories(${ZeroMQ_INCLUDE_DIRS})

  add_subdirectory(zeromq)
  
  #export config for goby_zeromq libraries
  install(EXPORT goby_zeromq-config DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/goby)
  #configuration export - local build version
  export(TARGETS ${GOBY_ZEROMQ_LIBRARY_LIST} FILE ${CMAKE_BINARY_DIR}/goby_zeromq-config.cmake)
endif()

## MOOS 
#first try to find v10 MOOS
find_package(MOOS 10 QUIET NO_MODULE)
set(MOOS_DOC_STRING "Build the Acoustic Communications MOOS apps (pAcommsHandler, etc.)")
if(MOOS_FOUND)
  #new MOOS 10
  set(MOOS_LIBRARIES MOOS)
  set(MOOS_LIBRARY_HEADER "MOOS/libMOOS/MOOSLib.h" PARENT_SCOPE)
  include_directories(${MOOS_INCLUDE_DIRS})
  option(build_moos ${MOOS_DOC_STRING} ON)

else()
  option(build_moos ${MOOS_DOC_STRING} OFF)
  message(">> setting build_moos to OFF ... if you need this functionality: 1) install MOOS and libproj-dev; 2) run cmake -Dbuild_moos=ON")
endif()


if(build_moos)
  goby_find_required_package(MOOS 10 NO_MODULE)

  add_subdirectory(moos)

  #export config for goby_moos libraries
  install(EXPORT goby_moos-config DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/goby)
  #configuration export - local build version
  export(TARGETS ${GOBY_MOOS_LIBRARY_LIST} FILE ${CMAKE_BINARY_DIR}/goby_moos-config.cmake)
endif(build_moos)

#latex, for documentation
find_package(LATEX QUIET)
set(LATEX_DOC_STRING "Build documentation (requires Doxygen, LaTeX, XeLaTeX, and certain fonts)")
option(build_doc ${LATEX_DOC_STRING} OFF)

if(build_doc)
  add_subdirectory(doc)
endif()

if(enable_testing)
  add_subdirectory(test)
endif()

option(build_apps "Build applications (as well as libraries)" ON)

if(build_apps)
  add_subdirectory(apps)
endif()
