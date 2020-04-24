add_custom_command(OUTPUT ${goby_SHARE_DIR}/goby/doc/goby-user.pdf
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build_user_manual.sh
  ARGS "full" ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${goby_SHARE_DIR}/goby/doc/goby-user.pdf
)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/goby-user-quick.pdf
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build_user_manual.sh
  ARGS "quick" ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/goby-user-quick.pdf
)

file(RELATIVE_PATH RELATIVE_CMAKE_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR})
file(RELATIVE_PATH RELATIVE_CMAKE_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB LATEX_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.tex *.bib *.bst *.sty)
foreach(I ${LATEX_FILES})
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${I} ${CMAKE_CURRENT_BINARY_DIR}/${I} @ONLY)
endforeach()


add_custom_target(user-pdf ALL DEPENDS ${goby_SHARE_DIR}/goby/doc/goby-user.pdf)

add_custom_target(user-quick DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/goby-user-quick.pdf)
