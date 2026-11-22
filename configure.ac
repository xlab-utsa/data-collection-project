#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#
# The build layout (util/ -> video/ -> app/) is inherited from Ringmaster
# (https://github.com/microsoft/ringmaster), which this project extends with
# live V4L2 capture and on-disk recording.

AC_PREREQ([2.69])
AC_INIT([data-collect-system], [2.1])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_HEADERS([config.h])

CXX17_FLAGS="-std=c++17 -pthread"
PICKY_CXXFLAGS="-pedantic -Wall -Wextra -Werror -Weffc++"
AC_SUBST([CXX17_FLAGS])
AC_SUBST([PICKY_CXXFLAGS])

# Checks for programs.
AC_PROG_CXX
AC_PROG_RANLIB

# Checks for libraries.
PKG_CHECK_MODULES([VPX], [vpx])                      # VP9 encode/decode
PKG_CHECK_MODULES([SDL], [sdl2])                     # preview / display windows
PKG_CHECK_MODULES([AV], [libavutil libswscale])      # YUYV -> I420 / RGB565 conversion

AC_CONFIG_FILES([
    Makefile
    src/Makefile
    src/util/Makefile
    src/video/Makefile
    src/app/Makefile
])

AC_OUTPUT
