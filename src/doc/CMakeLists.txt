
find_package(Doxygen REQUIRED)

find_program(XELATEX_EXECUTABLE xelatex)

find_file(GENTIUM_AI_FONT NAMES GenAI102.ttf GentiumAlt-I.ttf
  PATHS /usr/share/fonts/truetype
  PATH_SUFFIXES ttf-sil-gentium gentium)

find_file(GENTIUM_AR_FONT NAMES GenAR102.ttf GentiumAlt-R.ttf
  PATHS /usr/share/fonts/truetype
  PATH_SUFFIXES ttf-sil-gentium gentium)

find_file(GENTIUM_I_FONT NAMES GenI102.ttf Gentium-I.ttf
  PATHS /usr/share/fonts/truetype
  PATH_SUFFIXES ttf-sil-gentium gentium)

find_file(GENTIUM_R_FONT NAMES GenR102.ttf Gentium-R.ttf
  PATHS /usr/share/fonts/truetype
  PATH_SUFFIXES ttf-sil-gentium gentium)

mark_as_advanced(GENTIUM_AI_FONT AND GENTIUM_AR_FONT AND GENTIUM_I_FONT AND GENTIUM_R_FONT XELATEX_EXECUTABLE)

if(NOT (GENTIUM_AI_FONT AND GENTIUM_AR_FONT AND GENTIUM_I_FONT AND GENTIUM_R_FONT))
  message(FATAL_ERROR "Missing (original) Gentium font: http://scripts.sil.org/cms/scripts/page.php?item_id=Gentium_download")
endif()

add_subdirectory(dev_manual)
#add_subdirectory(user_manual)
#add_subdirectory(whitepaper)
