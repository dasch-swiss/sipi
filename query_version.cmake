#
# query_version.cmake
#
# This script is used to query the value for the version of the project.
# It starts by trying to retrieve the git tag of the project. If the tag
# is the same as the version found in version.txt, then the this version
# is used. Otherwise, the version is set to the tag plus
#

# Assuming this script is located in /my_project/query_variable.cmake
cmake_minimum_required(VERSION 3.28)


# get version from git
#
if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/.git)
    find_package(Git QUIET)
    if (Git_FOUND)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} describe --tag --dirty --abbrev=7
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE BUILD_SCM_TAG
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE BUILD_SCM_REVISION
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)
    else (Git_FOUND)
        SET(BUILD_SCM_REVISION 0)
        SET(BUILD_SCM_TAG 0)
    endif (Git_FOUND)
endif (EXISTS ${CMAKE_CURRENT_LIST_DIR}/.git)

# Output the variable of interest
message("${BUILD_SCM_TAG}")
