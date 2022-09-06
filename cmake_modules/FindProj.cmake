find_path(PROJ_OLD_INCLUDE_DIR proj_api.h)
find_path(PROJ_INCLUDE_DIR proj.h)

if(NOT PROJ_INCLUDE_DIR)
  add_definitions("-DUSE_PROJ4")
  set(PROJ_INCLUDE_DIR ${PROJ_OLD_INCLUDE_DIR})
  message(STATUS "Using old Proj4 proj_api.h API as new proj.h not found")
endif()

find_library(PROJ_LIBRARY NAMES proj)

mark_as_advanced( PROJ_INCLUDE_DIR PROJ_LIBRARY )

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Proj DEFAULT_MSG
  PROJ_LIBRARY PROJ_INCLUDE_DIR)

if ( PROJ_FOUND )
  set(PROJ_LIBRARIES ${PROJ_LIBRARY} )
  set(PROJ_INCLUDE_DIRS ${PROJ_INCLUDE_DIR} )
endif ()
