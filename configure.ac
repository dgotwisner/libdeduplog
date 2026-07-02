AC_INIT([libdeduplog], [1.0.0], [dgotwisner@comcast.net])
AC_CONFIG_SRCDIR([deduplog.cpp])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])
AM_SILENT_RULES([yes])

# C++ and C++11
AC_PROG_CXX
AC_LANG([C++])
#AX_CXX_COMPILE_STDCXX([11], [noext], [mandatory])

# Required for old (CentOS 7) libtool
AM_PROG_AR

CXXFLAGS="$CXXFLAGS -std=c++11 -O3 -Wall -Wextra -Werror -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64"

# Libtool for shared/static libraries
LT_INIT([disable-static])

# Check for headers (optional features)
AC_CHECK_HEADERS([syslog.h sys/uio.h])

# systemd journal support (optional but recommended on Linux)
AC_CHECK_HEADER([systemd/sd-journal.h],
    [AC_CHECK_LIB([systemd], [sd_journal_send],
        [AC_DEFINE([HAVE_SYSTEMD], [1], [Define if libsystemd available])
         SYSTEMD_LIBS="-lsystemd"])],
    [])

# Check for writev (signal-handler safe output
AC_CHECK_FUNCS([writev])

AC_SUBST([SYSTEMD_LIBS])

AC_CONFIG_HEADERS([config.h])

AC_CHECK_PROG([HAVE_ASTYLE], [astyle], [yes], [no])
AM_CONDITIONAL([HAVE_ASTYLE], [test "x$HAVE_ASTYLE" = "xyes"])

AC_CHECK_PROG([HAVE_DOXYGEN], [doxygen], [yes], [no])
AM_CONDITIONAL([HAVE_DOXYGEN], [test "x$HAVE_DOXYGEN" = "xyes"])

AC_CHECK_PROG([HAVE_CPPCHECK], [cppcheck], [yes], [no])
AM_CONDITIONAL([HAVE_CPPCHECK], [test "x$HAVE_CPPCHECK" = "xyes"])

AC_CONFIG_FILES([Makefile example/Makefile deduplog.pc])
AC_OUTPUT
