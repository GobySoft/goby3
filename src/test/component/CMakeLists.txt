# Component tests: 'goby test' files run against real daemons and applications, so they need the
# goby binary
set_property(DIRECTORY PROPERTY LABELS component)

if(build_apps)
  add_subdirectory(goby_test_tool)
  if(build_zeromq)
    add_subdirectory(intervehicle)
  endif()
endif()
