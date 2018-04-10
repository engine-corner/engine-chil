AC_PREREQ([2.69])
AC_INIT([engine-chil], [1.1])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

dnl m4_include([m4/ax_check_openssl.m4])

AM_INIT_AUTOMAKE
LT_INIT([disable-static pic-only])
LTDL_INIT

AC_PROG_CC
AC_LANG(C)

AX_CHECK_OPENSSL(
  [have_openssl=yes],
  [AC_MSG_FAILURE([could not locate OpenSSL])]
)

if test "x$have_openssl" = "xyes"; then
  LIBS="$LIBS $OPENSSL_LIBS"
  CFLAGS="$CFLAGS $OPENSSL_INCLUDES"
  LDFLAGS="$LDFLAGS $OPENSSL_LDFLAGS"
fi

AC_CHECK_HEADERS([openssl/engine.h])
# Check out certain features we use that we know after specific to
# OpenSSL 1.1.0 or later
AC_CHECK_TYPE(CRYPTO_RWLOCK,[],
              [AC_MSG_FAILURE([You need OpenSSL 1.1.0 or later])],
              [#include <openssl/crypto.h>])
AC_CHECK_FUNCS([RSA_PKCS1_OpenSSL RSA_meth_new DH_meth_new])

AC_C_BIGENDIAN(
  AC_DEFINE(B_ENDIAN, 1, [machine is big-endian]),
  AC_DEFINE(L_ENDIAN, 1, [machine is little-endian]),
  AC_MSG_ERROR(unknown endianess),
  AC_MSG_ERROR(universial endianess not supported)
)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
