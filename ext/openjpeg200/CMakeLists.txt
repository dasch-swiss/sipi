project(liboj2_builder C CXX)
include(ExternalProject)

#
# get openjpeg2000
#
#if(MAKE_SHARED_SIPI)
#    SET(LIBOPT "-DBUILD_SHARED_LIBS:bool=on")
#else ()
#    SET(LIBOPT "-DBUILD_SHARED_LIBS:bool=off")
#endif()
#
#ExternalProject_Add(project_oj2k
#    INSTALL_DIR ${COMMON_LOCAL}
#    URL https://github.com/uclouvain/openjpeg/archive/version.2.1.tar.gz
#    SOURCE_DIR ${COMMON_SRCS}/openjpeg-2.1.0
#    CMAKE_ARGS ${LIBOPT} -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
#)
#ExternalProject_Get_Property(project_oj2k install_dir)
#if(MAKE_SHARED_SIPI)
#    add_library(oj2k SHARED IMPORTED GLOBAL)
#    set_property(TARGET oj2k PROPERTY IMPORTED_LOCATION ${install_dir}/lib/libopenjp2${CMAKE_SHARED_LIBRARY_SUFFIX})
#else()
#    add_library(oj2k STATIC IMPORTED GLOBAL)
#    set_property(TARGET oj2k PROPERTY IMPORTED_LOCATION ${install_dir}/lib/libopenjp2${CMAKE_STATIC_LIBRARY_SUFFIX})
#endif()
#add_dependencies(oj2k project_oj2k)
