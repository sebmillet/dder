#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([dder], [0.1], [milletseb@laposte.net])
AC_CONFIG_SRCDIR([src/dder.c])
AC_CONFIG_AUX_DIR([admin])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([subdir-objects])

# Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_CPP

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([limits.h stdlib.h string.h ssl.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([strcasecmp strerror])

AC_ARG_ENABLE(ssl,
			  [  --disable-ssl           compile without the openssl library])
test -z "$enable_ssl" && enable_ssl="yes"
if test "${enable_ssl}" = yes ; then
	AC_CHECK_LIB(ssl, SSL_library_init, , [AC_MSG_ERROR([openssl dev lib not found, use --disable-ssl option if you wish to compile without openssl or install it (libssl-dev for deb, openssl-devel for rpm)])])
	AC_CHECK_LIB(crypto, ERR_error_string_n, , [AC_MSG_ERROR([openssl dev lib not found, use --disable-ssl option if you wish to compile without openssl or install it (libssl-dev for deb, openssl-devel for rpm)])])
	CPPFLAGS="$CPPFLAGS -DHAS_LIB_OPENSSL"
fi

AM_CONDITIONAL(WITH_OPENSSL, [ test "${enable_ssl}" = yes ])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT