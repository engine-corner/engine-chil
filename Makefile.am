AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

pkglib_LTLIBRARIES = chil.la

chil_la_SOURCES = e_chil.c
chil_la_LDFLAGS = -module -avoid-version
chil_la_LDLADD = $(LIBLTDL)
chil_la_CPPFLAGS = $(LTDLINCL)
chil_la_DEPENDENCIES = $(LTDLDEPS)

# Override the usual and make sure to install in OpenSSL's default engine store
pkglibdir = $(libdir)/engines
