## set up function for external libraries errors
# e.g. Boost libboost-dev 1.36 REQUIRED ... 
macro(goby_find_required_package package_name)
  find_package(${package_name} ${ARGN})
  string(TOUPPER ${package_name} package_name_upper)
  if(NOT ${package_name_upper}_FOUND AND NOT ${package_name}_FOUND)
    message(FATAL_ERROR "missing required external library: ${package_name}. Please read the goby/DEPENDENCIES file to resolve.")
  endif()
endmacro()
  
macro(missing_optional_library lib_name)
  message(WARNING "missing optional external library: ${lib_name}. Goby will function without the support of this library, but some functionality will be not be available.")
endmacro()

## find external packages, libraries, and headers for the entire goby project
# mandatory

goby_find_required_package(ProtobufGoby)
include_directories(${PROTOBUF_INCLUDE_DIRS})


if(${PROTOC_VERSION} VERSION_LESS 2.5.0})
  message("libprotobuf < 2.5.0")
else()
  message("libprotobuf >= 2.5.0")
  set(PROTOBUF_ALLOW_ALIAS "option allow_alias = true;" PARENT_SCOPE)
endif()


# dccl
goby_find_required_package(DCCL)
message("Using DCCL in ${DCCL_DIR}")

if(NOT DEFINED DCCL_INCLUDE_DIR)
  # for DCCL 3.0.3 and newer
  get_target_property(DCCL_INCLUDE_DIR dccl INTERFACE_INCLUDE_DIRECTORIES)
endif()

if(DCCL_INCLUDE_DIR)
  message("Including protobuf dir: ${DCCL_INCLUDE_DIR}")
  include_directories("${DCCL_INCLUDE_DIR}")
  protobuf_include_dirs("${DCCL_INCLUDE_DIR}")
endif()


# shared library suffix
add_definitions(-DSHARED_LIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")

# optional
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

## zeromq
find_package(ZeroMQ QUIET)
set(ZEROMQ_DOC_STRING "Enable ZeroMQ Transport Layer components (requires libzmq-dev: http://www.zeromq.org/)")
if(ZEROMQ_FOUND)
  option(enable_zeromq ${ZEROMQ_DOC_STRING} ON)
else()
  option(enable_zeromq ${ZEROMQ_DOC_STRING} OFF)
  message(">> setting enable_zeromq to OFF ... if you need this functionality: 1) install libzmq-dev; 2) run cmake -Denable_zeromq=ON")
endif()

if(enable_zeromq)
  goby_find_required_package(ZeroMQ)
  include_directories(${ZeroMQ_INCLUDE_DIRS})
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
  include_directories(${HDF5_INCLUDE_DIRS})
else()
  option(enable_hdf5 ${HDF5_DOC_STRING} OFF)
  message(">> setting enable_hdf5 to OFF ... if you need this functionality: 1) install libhdf5-dev; 2) run cmake -Denable_hdf5=ON")
endif()

## Wt
find_package(WtGoby QUIET)
set(WT_DOC_STRING "Enable Wt web browser GUI components (requires libwt-dev, libwtdbo-dev, libwtdbosqlite-dev, and libwthttp-dev: http://www.webtoolkit.eu/wt)")
# find resources directory
find_path(Wt_RESOURCES_DIR_BASE share/Wt/resources/plus.gif /usr /usr/local)
if(WTGOBY_FOUND AND Wt_RESOURCES_DIR_BASE)
  option(enable_wt ${WT_DOC_STRING} ON)
   goby_find_required_package(Boost 1.40.0 COMPONENTS signals)
else()
  option(enable_wt ${WT_DOC_STRING} OFF)
  message(">> setting enable_wt to OFF ... if you need this functionality: 1) install libwt-dev, libwtdbo-dev, libwtdbosqlite-dev, and libwthttp-dev; 2) run cmake -Denable_wt=ON")
endif()

if(enable_wt)
  goby_find_required_package(WtGoby)    
  include_directories(${Wt_INCLUDE_DIR})
  set(Wt_RESOURCES_DIR "${Wt_RESOURCES_DIR_BASE}/share/Wt/resources")
  add_custom_command(OUTPUT WtResourcesLink COMMAND ${CMAKE_COMMAND} -E create_symlink ${Wt_RESOURCES_DIR} ${goby_SHARE_DIR}/liaison/resources)
  add_custom_target(wt_link ALL DEPENDS WtResourcesLink)
  add_definitions(-DBOOST_SIGNALS_NO_DEPRECATION_WARNING)
endif()  

# Xerces
find_package(Xerces QUIET)
set(TRANSITIONAL_DOC_STRING "Compile in MOOS support for Goby version 1 XML files (requires libxerces-c-dev")
if(Xerces_FOUND)
  option(enable_v1_transitional ${TRANSITIONAL_DOC_STRING} ON)
else()
  option(enable_v1_transitional ${TRANSITIONAL_DOC_STRING} OFF)
  message(">> setting enable_v1_transitional to OFF ... if you need this functionality: 1) install libxerces-c-dev; 2) run cmake -Denable_v1_transitional=ON")
endif()

if(enable_v1_transitional)
  goby_find_required_package(Xerces)
  include_directories(${Xerces_INCLUDE_DIRS})
  add_definitions(-DENABLE_GOBY_V1_TRANSITIONAL_SUPPORT)
endif()  

# GMP
find_package(GMP QUIET)
set(GMP_DOC_STRING "Enable Gmp3 database components (requires libgmp-dev: http://gmplib.org")
# find resources directory
if(GMP_FOUND)
  option(enable_gmp ${GMP_DOC_STRING} ON)
else()
  option(enable_gmp ${GMP_DOC_STRING} OFF)
  message(">> setting enable_gmp to OFF ... if you need this functionality: 1) install libgmp-dev; 2) run cmake -Denable_gmp=ON")
endif()


## Kernel
option(disable_epoll "Apply workarounds for kernels that don't support epoll (before Linux 2.6). Leave OFF for newer kernels" OFF)
if(disable_epoll)
  add_definitions(-DBOOST_ASIO_DISABLE_EPOLL)
endif()


## set flags
option(build_common "Build the Goby Common library" ON)  



macro(goby_install_lib target_lib)
  set_property(TARGET ${target_lib} APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    $<BUILD_INTERFACE:${goby_INC_DIR}>
    $<INSTALL_INTERFACE:include>  # <prefix>/include
    )

  install(TARGETS ${target_lib} EXPORT goby-config
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib) 
  list(APPEND GOBY_LIBRARY_LIST ${target_lib})
  set(GOBY_LIBRARY_LIST ${GOBY_LIBRARY_LIST} CACHE INTERNAL "goby_library_list")
endmacro()


# module dependencies
# you can optionally build up to any point in this chain
# common -> util -> acomms -> moos
#                          -> middleware

if(build_common)

   ## boost
   goby_find_required_package(Boost 1.40.0 COMPONENTS system date_time thread program_options filesystem regex)
   include_directories(${Boost_INCLUDE_DIRS})

  add_subdirectory(common)

  option(build_util "Build the Goby Utility library and applications (requires goby-common)" ON)
  if(build_util)    
    add_subdirectory(util)

    option(build_acomms "Build the Goby Acoustic Communications library and applications (requires goby-util)" ON)
    if(build_acomms)     
      add_subdirectory(acomms)


      if(enable_zeromq)
        option(build_middleware "Build the Goby Middleware" ON)
        add_subdirectory(middleware)
      endif()
      
      if(enable_zeromq AND enable_gmp)
        option(build_pb "Build the Goby implementation in Protobuf/C++ (PB) (requires goby-acomms)" ON)
        if(build_pb)
          add_subdirectory(pb)
        endif(build_pb)
      endif()


      # first try to find v10 MOOS
      find_package(MOOS 10 QUIET NO_MODULE)
      set(MOOS_DOC_STRING "Build the Acoustic Communications MOOS apps (pAcommsHandler, etc.)")
      if(MOOS_FOUND)
        # new MOOS 10
        set(MOOS_LIBRARIES MOOS)
        set(MOOS_LIBRARY_HEADER "MOOS/libMOOS/MOOSLib.h" PARENT_SCOPE)
      else()
        message(">> Could not find MOOS v10, trying to find MOOS pre-v10.")
        # try to find old MOOS
        find_package(MOOS QUIET)
        set(MOOS_LIBRARY_HEADER "MOOSLIB/MOOSLib.h" PARENT_SCOPE)
      endif()

      find_package(Proj QUIET)
      if(MOOS_FOUND AND PROJ_FOUND)
        include_directories(${MOOS_INCLUDE_DIRS})
        option(build_moos ${MOOS_DOC_STRING} ON)
      else()
        option(build_moos ${MOOS_DOC_STRING} OFF)
        message(">> setting build_moos to OFF ... if you need this functionality: 1) install MOOS and libproj-dev; 2) run cmake -Dbuild_moos=ON")
      endif()

      if(build_moos)
        add_subdirectory(moos)
      endif(build_moos)
    endif(build_acomms)
  endif(build_util)
endif(build_common)

# latex, for documentation
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

