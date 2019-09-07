The CHIL engine from OpenSSL
============================

This is an export of the `chil` engine from OpenSSL, adapted to be
built using GNU auto* tools and make.

Requirements
------------

- GNU autoconf, automake, aclocal, libtool
- GNU make
- A C compiler
- OpenSSL development files, version 1.1.0 or newer.
- Autoconf archive

Quick instructions
------------------

If you've checked out the source from git, you need to prepare your
checkout as follows:

    autoreconf -i

This is a one time command.

Then, you configure, build and install as follows:

    ./configure && make && make install

`chil.so` is installed in `$(libdir)/engines`, which defaults to
`usr/local/lib/engines`.  You can change the prefix (default
`usr/local` as follows:

    ./configure --prefix=/usr

... or `libdir` itself:

    ./configure --libdir=/usr/lib

Known configuration failures
----------------------------

Sometimes, the `autoreconf` command fails, complaining about
`AC_MSG_FAILURE` not being defined, like this:

    $ autoreconf -i
    aclocal: warning: couldn't open directory 'm4': No such file or directory
    configure.ac:18: error: possibly undefined macro: AC_MSG_FAILURE
          If this token and others are legitimate, please use m4_pattern_allow.
          See the Autoconf documentation.
    autoreconf: /usr/bin/autoconf failed with exit status: 1

Unfortunately, `configure` is sometimes unclear on what's wrong.  In
this particular case, it's not `AC_MSG_FAILURE` that's missing, but
`AX_CHECK_OPENSSL`, which means that the Autoconf archive is missing
or is installed in a place `autoreconf` can't find.
