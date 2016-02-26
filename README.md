The CHIL engine from OpenSSL
============================

This is an export of the `chil` engine from OpenSSL, adapted to be
built using GNU auto* tools and make.

Requirements
------------

- GNU autoconf, automake, aclocal, libtool
- GNU make
- A C compiler
- OpenSSL development files
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

    ./Configure --prefix=/usr

... or `libdir` itself:

    ./Configure --libdir=/usr/lib

