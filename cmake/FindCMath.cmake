# FindCMath
#
# Find the native CMath includes and library.
#
# IMPORTED Targets
#
# This module defines `IMPORTED` target ``CMath::CMath``, if
# CMath has been found.
#
# Result Variables
#
# This module defines the following variables:
#
#  CMath_INCLUDE_DIRS   - Where to find math.h
#  CMath_LIBRARIES      - List of libraries when using CMath.
#  CMath_FOUND          - True if CMath found.

find_path(MATH_INCLUDE_DIR math.h)

SET(MATH_NAMES ${MATH_NAMES} m)
FIND_LIBRARY(MATH_LIBRARY NAMES ${MATH_NAMES} )

# handle the QUIETLY and REQUIRED arguments and set MATH_FOUND to TRUE if
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(CMath DEFAULT_MSG MATH_LIBRARY MATH_INCLUDE_DIR)

IF(CMath_FOUND)
    set( CMath_LIBRARIES ${MATH_LIBRARY} )
    set( CMath_INCLUDE_DIRS ${MATH_INCLUDE_DIR} )
    add_library(CMath::CMath UNKNOWN IMPORTED)
    set_target_properties(CMath::CMath PROPERTIES IMPORTED_LOCATION "${MATH_LIBRARY}")

ENDIF(CMath_FOUND)
