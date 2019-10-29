find_path(Cryptopp_INCLUDE_DIR cryptopp/aes.h)

if(NOT Cryptopp_INCLUDE_DIR)
  find_path(Cryptopp_INCLUDE_DIR crypto++/aes.h)
  if(Cryptopp_INCLUDE_DIR)
    set(CRYPTOPP_PATH_USES_PLUS_SIGN "ON" CACHE INTERNAL "" )
  endif()
endif()

if(CRYPTOPP_PATH_USES_PLUS_SIGN)
  add_definitions(-DCRYPTOPP_PATH_USES_PLUS_SIGN=1)
endif()

find_library(Cryptopp_LIBRARY NAMES cryptopp crypto++
  DOC "The Cryptopp Encryption library")

mark_as_advanced(Cryptopp_INCLUDE_DIR
  Cryptopp_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Cryptopp DEFAULT_MSG
  Cryptopp_LIBRARY Cryptopp_INCLUDE_DIR)

set(Cryptopp_FOUND  ${CRYPTOPP_FOUND})
if(Cryptopp_FOUND)
  set(Cryptopp_INCLUDE_DIRS ${Cryptopp_INCLUDE_DIR})
  set(Cryptopp_LIBRARIES    ${Cryptopp_LIBRARY})
endif()

