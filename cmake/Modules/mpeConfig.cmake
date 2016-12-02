INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(PC_MPE mpe)

FIND_PATH(
    MPE_INCLUDE_DIRS
    NAMES mpe/api.h
    HINTS $ENV{MPE_DIR}/include
        ${PC_MPE_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    MPE_LIBRARIES
    NAMES gnuradio-mpe
    HINTS $ENV{MPE_DIR}/lib
        ${PC_MPE_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(MPE DEFAULT_MSG MPE_LIBRARIES MPE_INCLUDE_DIRS)
MARK_AS_ADVANCED(MPE_LIBRARIES MPE_INCLUDE_DIRS)

