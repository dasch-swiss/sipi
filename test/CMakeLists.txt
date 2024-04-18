#
# This file needs to be updated whenever a new test is added.
#
# all tests can be run from the build directory with `make test`

# Configure dependencies
include(${PROJECT_SOURCE_DIR}/cmake/googletest.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/approvaltests.cmake)

# Approval tests
add_subdirectory(approval)

# Configuration tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./configuration/configuration)'
add_subdirectory(unit/configuration)

# SipiImage tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./sipiimage/sipiimage)'
add_subdirectory(unit/sipi_image)

# Handlers tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./handlers/handlers)'
add_subdirectory(unit/handlers)
