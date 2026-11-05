# Component tests: 'goby test' files run against real daemons and applications, so they need the
# goby binary
set_property(DIRECTORY PROPERTY LABELS component)

if(build_apps AND build_zeromq)
  add_subdirectory(intervehicle)
endif()
