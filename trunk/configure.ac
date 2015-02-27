# Mon Apr  9 16:14:26 CST 2012
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.65])
m4_include([version.m4])
AC_INIT(v804_stream,VERSION_NUMBER)
AM_INIT_AUTOMAKE

AC_CONFIG_SRCDIR([src/v804_stream.c])
AC_CONFIG_HEADERS([config.h])
AC_PROG_LIBTOOL


# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES(ALLLIBS,[libxml-2.0 edk-lib tea])
CFLAGS="$CFLAGS $ALLLIBS_CFLAGS"
LIBS="$LIBS $ALLLIBS_LIBS"

# Checks for header files.
AC_CHECK_HEADERS([netdb.h sys/ioctl.h sys/socket.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT32_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_MMAP
AC_CHECK_FUNCS([munmap])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
