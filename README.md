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

Then, you configure, and build as follows:

    ./configure && make

`chil.so` is installed in `$(libdir)/engines`, which defaults to
`usr/local/lib/engines`.  Some linux distros prefer other locations such as
`/usr/lib/x86_64-linux-gnu/engines-1.1/`  You can change the prefix (default
`usr/local` as follows:

    ./configure --prefix=/usr

... or `libdir` itself:

    ./configure --libdir=/usr/lib

Before installing you can test that CHIL works:

    LD_LIBRARY_PATH=/opt/nfast/toolkits/hwcrhk OPENSSL_ENGINES=./.libs openssl speed -elapsed -seconds 5 -multi 5 -engine chil rsa1024

This should fork 5 workers and perform basic RSA offload using the attached
nShield HSM.  Usually this will show output similar to:

```
$ LD_LIBRARY_PATH=/opt/nfast/toolkits/hwcrhk OPENSSL_ENGINES=./.libs openssl speed -elapsed -seconds 5 -multi 5 -engine chil rsa1024
Forked child 0
Forked child 1
Forked child 2
Forked child 3
Forked child 4
engine "chil" set.
engine "chil" set.
engine "chil" set.
engine "chil" set.
engine "chil" set.
+DTP:1024:private:rsa:5
+DTP:1024:private:rsa:5
+DTP:1024:private:rsa:5
+DTP:1024:private:rsa:5
+DTP:1024:private:rsa:5
OpenSSL 1.1.1b  26 Feb 2019
built on: Mon Aug 12 12:11:14 2019 UTC
options:bn(64,64) rc4(16x,int) des(int) aes(partial) idea(int) blowfish(ptr)
compiler: /opt/gcc/4.9/bin/gcc -fPIC -pthread -m64 -Wa,--noexecstack -Wall -O3
-fPIC -fstack-check -DOPENSSL_USE_NODELETE -DL_ENDIAN -DOPENSSL_PIC
-DOPENSSL_CPUID_OBJ -DOPENSSL_IA32_SSE2 -DOPENSSL_BN_ASM_MONT
-DOPENSSL_BN_ASM_MONT5 -DOPENSSL_BN_ASM_GF2m -DSHA1_ASM -DSHA256_ASM
-DSHA512_ASM -DKECCAK1600_ASM -DRC4_ASM -DMD5_ASM -DAES_ASM -DVPAES_ASM
-DBSAES_ASM -DGHASH_ASM -DECP_NISTZ256_ASM -DX25519_ASM -DPADLOCK_ASM
-DPOLY1305_ASM -DNDEBUG -DOPENSSL_NO_RSAX -DOPENSSL_NO_RDRAND
-DOPENSSL_NO_HW_4758_CCA -DOPENSSL_NO_HW_AEP -DOPENSSL_NO_HW_ATALLA
-DOPENSSL_NO_HW_CSWIFT -DOPENSSL_NO_HW_NURON -DOPENSSL_NO_HW_SUREWARE
-DOPENSSL_NO_HW_UBSEC -DOPENSSL_NO_HW_PADLOCK -DOPENSSL_NO_GMP
-DOPENSSL_NO_CAPIENG -Itest
                  sign    verify    sign/s verify/s
rsa 1024 bits 0.007751s 0.009944s    129.0    100.6
```


Once everything is built and working you can install the engine as root:

    make install

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
