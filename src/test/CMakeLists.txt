add_subdirectory(time)
add_subdirectory(acomms)
add_subdirectory(util)
add_subdirectory(middleware)
add_subdirectory(udpm)

if(build_python)
  add_subdirectory(python)
endif()

# The two generators read the same interface.yml format, so they must accept and reject the same
# corpus. Only the pure Python goby package is involved, so this does not wait on build_python.
# Goby.jl is taken from the build tree: that is the copy whose dependencies are installed, and
# the source copy's Project.toml still holds unexpanded version placeholders.
# the interpreter alone: Python3_FOUND also wants Development.Module, which is for building the
# extension module and has nothing to do with running the generator
if(build_julia AND Python3_Interpreter_FOUND)
  add_test(NAME goby_test_interface_conformance
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/conformance.py
            --julia ${JULIA}
            --goby-jl ${GOBY_JULIA_DIR}
            --corpus ${goby_SRC_DIR}/share/interface/test)
  set_tests_properties(goby_test_interface_conformance PROPERTIES
    TIMEOUT 600
    ENVIRONMENT "PYTHONPATH=${GOBY_PYTHON_SOURCE_DIR}")
elseif(build_julia)
  message(STATUS "python3 not found: skipping the interface.yml cross-language conformance test")
endif()

# the test application is a zeromq one, as the Julia examples are
if(build_julia AND build_zeromq)
  add_subdirectory(julia)
endif()

if(build_zeromq)
  add_subdirectory(zeromq)
endif()

if(build_moos)
  add_subdirectory(moos)
endif()
