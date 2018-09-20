AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = libltdl

pkglib_LTLIBRARIES = chil.la

chil_la_LDFLAGS = -module -avoid-version
chil_la_LIBADD = $(LIBLTDL)
chil_la_CPPFLAGS = $(LTDLINCL)
chil_la_DEPENDENCIES = $(LTDLDEPS)

# We need to hide e_chil_err.c in noinst_HEADERS so automake won't try
# to have it compiled, as it's designed to be used like a header file.
chil_la_SOURCES = e_chil.c vendor_defns/hwcryptohook.h e_chil_err.h
noinst_HEADERS = e_chil_err.c

# Override the usual and make sure to install in OpenSSL's default engine store
pkglibdir = $(libdir)/engines
