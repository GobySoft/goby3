find_path( PROJ_INCLUDE_DIR proj_api.h PATHS /usr/include/ )

find_library( PROJ_LIBRARY NAMES proj PATHS /usr/lib/ )

mark_as_advanced( PROJ_INCLUDE_DIR PROJ_LIBRARY )

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Proj DEFAULT_MSG
  PROJ_LIBRARY PROJ_INCLUDE_DIR)

if ( PROJ_FOUND )
  set(PROJ_LIBRARIES ${PROJ_LIBRARY} )
  set(PROJ_INCLUDE_DIRS ${PROJ_INCLUDE_DIR} )
endif ()
