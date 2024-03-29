# FindINIH.cmake
find_path(INIH_INCLUDE_DIRS NAMES INIReader.h)
find_library(INIH_LIBRARIES NAMES inih ini)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(INIH DEFAULT_MSG INIH_LIBRARY INIH_INCLUDE_DIR)

if(INIH_FOUND AND NOT TARGET INIH::inih)
    add_library(INIH::inih UNKNOWN IMPORTED)
    set_target_properties(INIH::inih PROPERTIES
            IMPORTED_LOCATION "${INIH_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${INIH_INCLUDE_DIRS}")
endif()
