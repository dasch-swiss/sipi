#
# This file needs to be updated whenever a new test is added.
#
# all tests can be run from the build directory with `make test`

include(${PROJECT_SOURCE_DIR}/cmake/googletest.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/approvaltests.cmake)

# Approval tests
add_subdirectory(approval)

# Configuration parsing and tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./configuration/configuration)'
add_subdirectory(unit/configuration)

# SipiImage tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./sipiimage/sipiimage)'
add_subdirectory(unit/sipiimage)

# Handlers tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./handlers/handlers)'
add_subdirectory(unit/handlers)
