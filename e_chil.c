/*
 * Written by Richard Levitte (richard@levitte.org), Geoff Thorpe
 * (geoff@geoffthorpe.net) and Dr Stephen N Henson (steve@openssl.org) for
 * the OpenSSL project 2000.
 * Updated by Peter Botha (peterb@striata.com) for OpenSSL 1.1
 */
/* ====================================================================
 * Copyright (c) 1999-2001 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ltdl.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/engine.h>
#include <openssl/ui.h>
#include <openssl/rand.h>
#ifndef OPENSSL_NO_RSA
# include <openssl/rsa.h>
#endif
#ifndef OPENSSL_NO_DH
# include <openssl/dh.h>
#endif
#include <openssl/bn.h>

#include "config.h"

/*-
 * Attribution notice: nCipher have said several times that it's OK for
 * us to implement a general interface to their boxes, and recently declared
 * their HWCryptoHook to be public, and therefore available for us to use.
 * Thanks, nCipher.
 *
 * The hwcryptohook.h included here is from May 2000.
 * [Richard Levitte]
 */
#include "vendor_defns/hwcryptohook.h"

#define HWCRHK_LIB_NAME "CHIL engine"
#include "e_chil_err.c"

static CRYPTO_RWLOCK *chil_lock;

static int hwcrhk_destroy(ENGINE *e);
static int hwcrhk_init(ENGINE *e);
static int hwcrhk_finish(ENGINE *e);
static int hwcrhk_ctrl(ENGINE *e, int cmd, long i, void *p, void (*f) (void));

/* Functions to handle mutexes */
static int hwcrhk_mutex_init(HWCryptoHook_Mutex *,
                             HWCryptoHook_CallerContext *);
static int hwcrhk_mutex_lock(HWCryptoHook_Mutex *);
static void hwcrhk_mutex_unlock(HWCryptoHook_Mutex *);
static void hwcrhk_mutex_destroy(HWCryptoHook_Mutex *);

/* BIGNUM stuff */
static int hwcrhk_bn_mod_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
                          const BIGNUM *m, BN_CTX *ctx);

#ifndef OPENSSL_NO_RSA
/* RSA stuff */
static int hwcrhk_rsa_mod_exp(BIGNUM *r, const BIGNUM *I, RSA *rsa,
                              BN_CTX *ctx);
/* This function is aliased to mod_exp (with the mont stuff dropped). */
static int hwcrhk_rsa_bn_mod_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
                               const BIGNUM *m, BN_CTX *ctx,
                               BN_MONT_CTX *m_ctx);
static int hwcrhk_rsa_finish(RSA *rsa);
#endif

#ifndef OPENSSL_NO_DH
/* DH stuff */
/* This function is alised to mod_exp (with the DH and mont dropped). */
static int hwcrhk_dh_bn_mod_exp(const DH *dh, BIGNUM *r,
                             const BIGNUM *a, const BIGNUM *p,
                             const BIGNUM *m, BN_CTX *ctx,
                             BN_MONT_CTX *m_ctx);
#endif

/* RAND stuff */
static int hwcrhk_rand_bytes(unsigned char *buf, int num);
static int hwcrhk_rand_status(void);

/* KM stuff */
static EVP_PKEY *hwcrhk_load_privkey(ENGINE *eng, const char *key_id,
                                     UI_METHOD *ui_method,
                                     void *callback_data);
static EVP_PKEY *hwcrhk_load_pubkey(ENGINE *eng, const char *key_id,
                                    UI_METHOD *ui_method,
                                    void *callback_data);

/* Interaction stuff */
static int hwcrhk_insert_card(const char *prompt_info,
                              const char *wrong_info,
                              HWCryptoHook_PassphraseContext * ppctx,
                              HWCryptoHook_CallerContext * cactx);
static int hwcrhk_get_pass(const char *prompt_info,
                           int *len_io, char *buf,
                           HWCryptoHook_PassphraseContext * ppctx,
                           HWCryptoHook_CallerContext * cactx);
static void hwcrhk_log_message(void *logstr, const char *message);

/* MPI stuff */
#define HWCRHK_MPI_DEFAULT_ALLOC_SIZE 64    /* = 512 bits */
#define HWCRHK_MPI_RSA_ALLOC_SIZE 1024   /* = 8192 bits */
static HWCryptoHook_MPI *hwcrhk_mpi_new();
static HWCryptoHook_MPI *hwcrhk_mpi_alloc(size_t size);
static HWCryptoHook_MPI *hwcrhk_mpi_resize(HWCryptoHook_MPI *mpi, size_t size);
static void hwcrhk_mpi_free(HWCryptoHook_MPI *mpi);
static HWCryptoHook_MPI *hwcrhk_mpi_bn2mpi(const BIGNUM *bn);
static BIGNUM *hwcrhk_mpi_mpi2bn(const HWCryptoHook_MPI *mpi, BIGNUM *ret);


/* The definitions for control commands specific to this engine */
#define HWCRHK_CMD_SO_PATH              ENGINE_CMD_BASE
#define HWCRHK_CMD_FORK_CHECK           (ENGINE_CMD_BASE + 1)
#define HWCRHK_CMD_THREAD_LOCKING       (ENGINE_CMD_BASE + 2)
#define HWCRHK_CMD_SET_USER_INTERFACE   (ENGINE_CMD_BASE + 3)
#define HWCRHK_CMD_SET_CALLBACK_DATA    (ENGINE_CMD_BASE + 4)
static const ENGINE_CMD_DEFN hwcrhk_cmd_defns[] = {
    {HWCRHK_CMD_SO_PATH,
     "SO_PATH",
     "Specifies the path to the 'hwcrhk' shared library",
     ENGINE_CMD_FLAG_STRING},
    {HWCRHK_CMD_FORK_CHECK,
     "FORK_CHECK",
     "Turns fork() checking on (non-zero) or off (zero)",
     ENGINE_CMD_FLAG_NUMERIC},
    {HWCRHK_CMD_THREAD_LOCKING,
     "THREAD_LOCKING",
     "Turns thread-safe locking on (zero) or off (non-zero)",
     ENGINE_CMD_FLAG_NUMERIC},
    {HWCRHK_CMD_SET_USER_INTERFACE,
     "SET_USER_INTERFACE",
     "Set the global user interface (internal)",
     ENGINE_CMD_FLAG_INTERNAL},
    {HWCRHK_CMD_SET_CALLBACK_DATA,
     "SET_CALLBACK_DATA",
     "Set the global user interface extra data (internal)",
     ENGINE_CMD_FLAG_INTERNAL},
    {0, NULL, NULL, 0}
};

#ifndef OPENSSL_NO_RSA
static RSA_METHOD *hwcrhk_rsa = NULL;
#endif
#ifndef OPENSSL_NO_DH
static DH_METHOD *hwcrhk_dh = NULL;
#endif

static RAND_METHOD hwcrhk_rand = {
    /* "CHIL RAND method", */
    NULL,
    hwcrhk_rand_bytes,
    NULL,
    NULL,
    hwcrhk_rand_bytes,
    hwcrhk_rand_status,
};

/* Constants used when creating the ENGINE */
static const char *engine_hwcrhk_id = "chil";
static const char *engine_hwcrhk_name = "CHIL hardware engine support";
/* Compatibility hack, the dynamic library uses this form in the path */
static const char *engine_hwcrhk_id_alt = "ncipher";

/* Internal stuff for HWCryptoHook */

/* Some structures needed for proper use of thread locks */
/*
 * hwcryptohook.h has some typedefs that turn struct HWCryptoHook_MutexValue
 * into HWCryptoHook_Mutex
 */
struct HWCryptoHook_MutexValue {
    CRYPTO_RWLOCK *lock;
};

/*
 * hwcryptohook.h has some typedefs that turn struct
 * HWCryptoHook_PassphraseContextValue into HWCryptoHook_PassphraseContext
 */
struct HWCryptoHook_PassphraseContextValue {
    UI_METHOD *ui_method;
    void *callback_data;
};

/*
 * hwcryptohook.h has some typedefs that turn struct
 * HWCryptoHook_CallerContextValue into HWCryptoHook_CallerContext
 */
struct HWCryptoHook_CallerContextValue {
    pem_password_cb *password_callback; /* Deprecated! Only present for
                                         * backward compatibility! */
    UI_METHOD *ui_method;
    void *callback_data;
};

static BIO *logstream = NULL;
static int disable_mutex_callbacks = 0;

/*
 * One might wonder why these are needed, since one can pass down at least a
 * UI_METHOD and a pointer to callback data to the key-loading functions. The
 * thing is that the ModExp and RSAImmed functions can load keys as well, if
 * the data they get is in a special, nCipher-defined format (hint: if you
 * look at the private exponent of the RSA data as a string, you'll see this
 * string: "nCipher KM tool key id", followed by some bytes, followed a key
 * identity string, followed by more bytes.  This happens when you use
 * "embed" keys instead of "hwcrhk" keys).  Unfortunately, those functions do
 * not take any passphrase or caller context, and our functions can't really
 * take any callback data either.  Still, the "insert_card" and
 * "get_passphrase" callbacks may be called down the line, and will need to
 * know what user interface callbacks to call, and having callback data from
 * the application may be a nice thing as well, so we need to keep track of
 * that globally.
 */
static HWCryptoHook_CallerContext password_context = { NULL, NULL, NULL };

/* Stuff to pass to the HWCryptoHook library */
static HWCryptoHook_InitInfo hwcrhk_globals = {
    HWCryptoHook_InitFlags_SimpleForkCheck, /* Flags */
    &logstream,                 /* logstream */
    sizeof(BN_ULONG),           /* limbsize */
    0,                          /* mslimb first: false for BNs */
    -1,                         /* msbyte first: use native */
    0,                          /* Max mutexes, 0 = no small limit */
    0,                          /* Max simultaneous, 0 = default */

    /*
     * The next few are mutex stuff: we write wrapper functions around the OS
     * mutex functions.  We initialise them to 0 here, and change that to
     * actual function pointers in hwcrhk_init() if dynamic locks are
     * supported (that is, if the application programmer has made sure of
     * setting up callbacks bafore starting this engine) *and* if
     * disable_mutex_callbacks hasn't been set by a call to
     * ENGINE_ctrl(ENGINE_CTRL_CHIL_NO_LOCKING).
     */
    sizeof(HWCryptoHook_Mutex),
    0,
    0,
    0,
    0,

    /*
     * The next few are condvar stuff: we write wrapper functions round the
     * OS functions.  Currently not implemented and not and absolute
     * necessity even in threaded programs, therefore 0'ed.  Will hopefully
     * be implemented some day, since it enhances the efficiency of
     * HWCryptoHook.
     */
    0,                          /* sizeof(HWCryptoHook_CondVar), */
    0,                          /* hwcrhk_cv_init, */
    0,                          /* hwcrhk_cv_wait, */
    0,                          /* hwcrhk_cv_signal, */
    0,                          /* hwcrhk_cv_broadcast, */
    0,                          /* hwcrhk_cv_destroy, */

    hwcrhk_get_pass,            /* pass phrase */
    hwcrhk_insert_card,         /* insert a card */
    hwcrhk_log_message          /* Log message */
};

/* Now, to our own code */

/*
 * This internal function is used by ENGINE_chil() and possibly by the
 * "dynamic" ENGINE support too
 */
static int bind_helper(ENGINE *e)
{
#ifndef OPENSSL_NO_RSA
    const RSA_METHOD *ossl_rsa_meth;
#endif
#ifndef OPENSSL_NO_DH
    const DH_METHOD *ossl_dh_meth;
#endif

    chil_lock = CRYPTO_THREAD_lock_new();
    if (chil_lock == NULL)
        goto err;

#ifndef OPENSSL_NO_RSA
    /* Setup RSA_METHOD */
    hwcrhk_rsa = RSA_meth_new("CHIL RSA method", 0);
    if (hwcrhk_rsa == NULL)
        goto err;

    /*
     * We know that the "PKCS1_OpenSSL()" functions hook properly to the
     * cswift-specific mod_exp and mod_exp_crt so we use those functions. NB:
     * We don't use ENGINE_openssl() or anything "more generic" because
     * something like the RSAref code may not hook properly, and if you own
     * one of these cards then you have the right to do RSA operations on it
     * anyway!
     */
    ossl_rsa_meth = RSA_PKCS1_OpenSSL();
    if (   !RSA_meth_set_pub_enc(hwcrhk_rsa,
                                 RSA_meth_get_pub_enc(ossl_rsa_meth))
        || !RSA_meth_set_pub_dec(hwcrhk_rsa,
                                 RSA_meth_get_pub_dec(ossl_rsa_meth))
        || !RSA_meth_set_priv_enc(hwcrhk_rsa,
                                 RSA_meth_get_priv_enc(ossl_rsa_meth))
        || !RSA_meth_set_priv_dec(hwcrhk_rsa,
                                 RSA_meth_get_priv_dec(ossl_rsa_meth))
        || !RSA_meth_set_mod_exp(hwcrhk_rsa, hwcrhk_rsa_mod_exp)
        || !RSA_meth_set_bn_mod_exp(hwcrhk_rsa, hwcrhk_rsa_bn_mod_exp)
        || !RSA_meth_set_finish(hwcrhk_rsa, hwcrhk_rsa_finish)) {
        goto err;
    }
#endif

#ifndef OPENSSL_NO_DH
    /* Setup DH Method */
    hwcrhk_dh = DH_meth_new("CHIL DH method", 0);
    if (hwcrhk_dh == NULL)
        goto err;

    /* Much the same for Diffie-Hellman */
    ossl_dh_meth = DH_OpenSSL();
    if (   !DH_meth_set_generate_key(hwcrhk_dh,
                                DH_meth_get_generate_key(ossl_dh_meth))
        || !DH_meth_set_compute_key(hwcrhk_dh,
                                DH_meth_get_compute_key(ossl_dh_meth))
        || !DH_meth_set_bn_mod_exp(hwcrhk_dh, hwcrhk_dh_bn_mod_exp)) {
        goto err;
    }

#endif

    if (!ENGINE_set_id(e, engine_hwcrhk_id) ||
        !ENGINE_set_name(e, engine_hwcrhk_name) ||
        !ENGINE_set_flags(e, ENGINE_FLAGS_NO_REGISTER_ALL) ||
#ifndef OPENSSL_NO_RSA
        !ENGINE_set_RSA(e, hwcrhk_rsa) ||
#endif
#ifndef OPENSSL_NO_DH
        !ENGINE_set_DH(e, hwcrhk_dh) ||
#endif
        !ENGINE_set_RAND(e, &hwcrhk_rand) ||
        !ENGINE_set_destroy_function(e, hwcrhk_destroy) ||
        !ENGINE_set_init_function(e, hwcrhk_init) ||
        !ENGINE_set_finish_function(e, hwcrhk_finish) ||
        !ENGINE_set_ctrl_function(e, hwcrhk_ctrl) ||
        !ENGINE_set_load_privkey_function(e, hwcrhk_load_privkey) ||
        !ENGINE_set_load_pubkey_function(e, hwcrhk_load_pubkey) ||
        !ENGINE_set_cmd_defns(e, hwcrhk_cmd_defns)) {
        goto err;
    }

    /* Ensure the hwcrhk error handling is set up */
    ERR_load_HWCRHK_strings();

    return 1;

 err:
    HWCRHKerr(HWCRHK_F_BIND_HELPER, ERR_R_MALLOC_FAILURE);

    CRYPTO_THREAD_lock_free(chil_lock);
    chil_lock = NULL;

#ifndef OPENSSL_NO_RSA
    RSA_meth_free(hwcrhk_rsa);
    hwcrhk_rsa = NULL;
#endif
#ifndef OPENSSL_NO_DH
    DH_meth_free(hwcrhk_dh);
    hwcrhk_dh = NULL;
#endif

    return 0;
}

/*
 * This is a process-global DSO handle used for loading and unloading the
 * HWCryptoHook library. NB: This is only set (or unset) during an init() or
 * finish() call (reference counts permitting) and they're operating with
 * global locks, so this should be thread-safe implicitly.
 */
static lt_dlhandle hwcrhk_dso = NULL;
static HWCryptoHook_ContextHandle hwcrhk_context = 0;
#ifndef OPENSSL_NO_RSA
/* Index for KM handle.  Not really used yet. */
static int hndidx_rsa = -1;
#endif

/*
 * These are the function pointers that are (un)set when the library has
 * successfully (un)loaded.
 */
static HWCryptoHook_Init_t *p_hwcrhk_Init = NULL;
static HWCryptoHook_Finish_t *p_hwcrhk_Finish = NULL;
static HWCryptoHook_ModExp_t *p_hwcrhk_ModExp = NULL;
#ifndef OPENSSL_NO_RSA
static HWCryptoHook_RSA_t *p_hwcrhk_RSA = NULL;
static HWCryptoHook_RSALoadKey_t *p_hwcrhk_RSALoadKey = NULL;
static HWCryptoHook_RSAGetPublicKey_t *p_hwcrhk_RSAGetPublicKey = NULL;
static HWCryptoHook_RSAUnloadKey_t *p_hwcrhk_RSAUnloadKey = NULL;
#endif
static HWCryptoHook_RandomBytes_t *p_hwcrhk_RandomBytes = NULL;
static HWCryptoHook_ModExpCRT_t *p_hwcrhk_ModExpCRT = NULL;

/* Used in the DSO operations. */
static char *HWCRHK_LIBNAME = NULL;
static void free_HWCRHK_LIBNAME(void)
{
    OPENSSL_free(HWCRHK_LIBNAME);
    HWCRHK_LIBNAME = NULL;
}

static const char *get_HWCRHK_LIBNAME(void)
{
    if (HWCRHK_LIBNAME)
        return HWCRHK_LIBNAME;
    return "nfhwcrhk";
}

static long set_HWCRHK_LIBNAME(const char *name)
{
    free_HWCRHK_LIBNAME();
    return (((HWCRHK_LIBNAME = OPENSSL_strdup(name)) != NULL) ? 1 : 0);
}

static const char *n_hwcrhk_Init = "HWCryptoHook_Init";
static const char *n_hwcrhk_Finish = "HWCryptoHook_Finish";
static const char *n_hwcrhk_ModExp = "HWCryptoHook_ModExp";
#ifndef OPENSSL_NO_RSA
static const char *n_hwcrhk_RSA = "HWCryptoHook_RSA";
static const char *n_hwcrhk_RSALoadKey = "HWCryptoHook_RSALoadKey";
static const char *n_hwcrhk_RSAGetPublicKey = "HWCryptoHook_RSAGetPublicKey";
static const char *n_hwcrhk_RSAUnloadKey = "HWCryptoHook_RSAUnloadKey";
#endif
static const char *n_hwcrhk_RandomBytes = "HWCryptoHook_RandomBytes";
static const char *n_hwcrhk_ModExpCRT = "HWCryptoHook_ModExpCRT";

/*
 * HWCryptoHook library functions and mechanics - these are used by the
 * higher-level functions further down. NB: As and where there's no error
 * checking, take a look lower down where these functions are called, the
 * checking and error handling is probably down there.
 */

/* utility function to obtain a context */
static int get_context(HWCryptoHook_ContextHandle * hac,
                       HWCryptoHook_CallerContext * cac)
{
    char tempbuf[1024];
    HWCryptoHook_ErrMsgBuf rmsg;

    rmsg.buf = tempbuf;
    rmsg.size = sizeof(tempbuf);

    *hac = p_hwcrhk_Init(&hwcrhk_globals, sizeof(hwcrhk_globals), &rmsg, cac);
    if (!*hac)
        return 0;
    return 1;
}

/* similarly to release one. */
static void release_context(HWCryptoHook_ContextHandle hac)
{
    p_hwcrhk_Finish(hac);
}

/* Destructor (complements the "ENGINE_chil()" constructor) */
static int hwcrhk_destroy(ENGINE *e)
{
    free_HWCRHK_LIBNAME();
    ERR_unload_HWCRHK_strings();
    CRYPTO_THREAD_lock_free(chil_lock);
    return 1;
}

/* (de)initialisation functions. */
static int hwcrhk_init(ENGINE *e)
{
    HWCryptoHook_Init_t *p1;
    HWCryptoHook_Finish_t *p2;
    HWCryptoHook_ModExp_t *p3;
#ifndef OPENSSL_NO_RSA
    HWCryptoHook_RSA_t *p4;
    HWCryptoHook_RSALoadKey_t *p5;
    HWCryptoHook_RSAGetPublicKey_t *p6;
    HWCryptoHook_RSAUnloadKey_t *p7;
#endif
    HWCryptoHook_RandomBytes_t *p8;
    HWCryptoHook_ModExpCRT_t *p9;
    const char *hwcrhk_name = get_HWCRHK_LIBNAME();
    char *hwcrhk_libname = NULL;

    if (hwcrhk_dso != NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_INIT, HWCRHK_R_ALREADY_LOADED);
        goto err;
    }

    if (lt_dlinit() != 0) {
        HWCRHKerr(HWCRHK_F_HWCRHK_INIT, ERR_R_SYS_LIB);
        ERR_add_error_data(2, "ltdl message: ", lt_dlerror());
        goto err;
    }

    if (strncmp(hwcrhk_name, "lib", 3) != 0) {
        /*
	 * hwcrhk_libname is the same as hwcrhk_name, but with "lib" prefixed.
	 * Make space for it
	 */
        if ((hwcrhk_libname = malloc(strlen(hwcrhk_name) + 4)) == NULL) {
            HWCRHKerr(HWCRHK_F_HWCRHK_INIT, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        strcpy(hwcrhk_libname, "lib");
        strcat(hwcrhk_libname, hwcrhk_name);
    }

    /* Attempt to load libnfhwcrhk.so/nfhwcrhk.dll/whatever. */
    if (hwcrhk_libname != NULL) {
        hwcrhk_dso = lt_dlopenext(hwcrhk_libname);
    }
    if (hwcrhk_dso == NULL && (hwcrhk_dso = lt_dlopenext(hwcrhk_name)) == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_INIT, HWCRHK_R_DSO_FAILURE);
        goto err;
    }
    free(hwcrhk_libname);

#define BINDIT(t, name) (t *)lt_dlsym(hwcrhk_dso, name)
    if ((p1 = BINDIT(HWCryptoHook_Init_t, n_hwcrhk_Init)) == NULL
        || (p2 = BINDIT(HWCryptoHook_Finish_t, n_hwcrhk_Finish)) == NULL
        || (p3 = BINDIT(HWCryptoHook_ModExp_t, n_hwcrhk_ModExp)) == NULL
#ifndef OPENSSL_NO_RSA
        || (p4 = BINDIT(HWCryptoHook_RSA_t, n_hwcrhk_RSA)) == NULL
        || (p5 = BINDIT(HWCryptoHook_RSALoadKey_t, n_hwcrhk_RSALoadKey)) == NULL
        || (p6 = BINDIT(HWCryptoHook_RSAGetPublicKey_t, n_hwcrhk_RSAGetPublicKey)) == NULL
        || (p7 = BINDIT(HWCryptoHook_RSAUnloadKey_t, n_hwcrhk_RSAUnloadKey)) == NULL
#endif
        || (p8 = BINDIT(HWCryptoHook_RandomBytes_t, n_hwcrhk_RandomBytes)) == NULL
        || (p9 = BINDIT(HWCryptoHook_ModExpCRT_t, n_hwcrhk_ModExpCRT)) == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_INIT, HWCRHK_R_DSO_FAILURE);
        goto err;
    }
    /* Copy the pointers */
    p_hwcrhk_Init = p1;
    p_hwcrhk_Finish = p2;
    p_hwcrhk_ModExp = p3;
#ifndef OPENSSL_NO_RSA
    p_hwcrhk_RSA = p4;
    p_hwcrhk_RSALoadKey = p5;
    p_hwcrhk_RSAGetPublicKey = p6;
    p_hwcrhk_RSAUnloadKey = p7;
#endif
    p_hwcrhk_RandomBytes = p8;
    p_hwcrhk_ModExpCRT = p9;

    /*
     * Check if the application decided to support dynamic locks, and if it
     * does, use them.
     */
    if (disable_mutex_callbacks == 0) {
        hwcrhk_globals.mutex_init = hwcrhk_mutex_init;
        hwcrhk_globals.mutex_acquire = hwcrhk_mutex_lock;
        hwcrhk_globals.mutex_release = hwcrhk_mutex_unlock;
        hwcrhk_globals.mutex_destroy = hwcrhk_mutex_destroy;
    }

    /*
     * Try and get a context - if not, we may have a DSO but no accelerator!
     */
    if (!get_context(&hwcrhk_context, &password_context)) {
        HWCRHKerr(HWCRHK_F_HWCRHK_INIT, HWCRHK_R_UNIT_FAILURE);
        goto err;
    }
    /* Everything's fine. */
#ifndef OPENSSL_NO_RSA
    if (hndidx_rsa == -1) {
        hndidx_rsa = RSA_get_ex_new_index(0,
                                          "nFast HWCryptoHook RSA key handle",
                                          NULL, NULL, NULL);
    }
#endif

    return 1;
 err:
    free(hwcrhk_libname);
    if (hwcrhk_dso != NULL) {
        lt_dlclose(hwcrhk_dso);
        hwcrhk_dso = NULL;
    }
    lt_dlexit();
    p_hwcrhk_Init = NULL;
    p_hwcrhk_Finish = NULL;
    p_hwcrhk_ModExp = NULL;
#ifndef OPENSSL_NO_RSA
    p_hwcrhk_RSA = NULL;
    p_hwcrhk_RSALoadKey = NULL;
    p_hwcrhk_RSAGetPublicKey = NULL;
    p_hwcrhk_RSAUnloadKey = NULL;
#endif
    p_hwcrhk_RandomBytes = NULL;
    p_hwcrhk_ModExpCRT = NULL;
    return 0;
}

static int hwcrhk_finish(ENGINE *e)
{
    int to_return = 0;

    free_HWCRHK_LIBNAME();

    if (hwcrhk_dso == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_FINISH, HWCRHK_R_NOT_LOADED);
        goto err;
    }

    release_context(hwcrhk_context);
    if (lt_dlclose(hwcrhk_dso) != 0) {
        HWCRHKerr(HWCRHK_F_HWCRHK_FINISH, HWCRHK_R_DSO_FAILURE);
        goto err;
    }

    to_return = 1;

 err:
    lt_dlexit();
    BIO_free(logstream);
    hwcrhk_dso = NULL;
    p_hwcrhk_Init = NULL;
    p_hwcrhk_Finish = NULL;
    p_hwcrhk_ModExp = NULL;
#ifndef OPENSSL_NO_RSA
    p_hwcrhk_RSA = NULL;
    p_hwcrhk_RSALoadKey = NULL;
    p_hwcrhk_RSAGetPublicKey = NULL;
    p_hwcrhk_RSAUnloadKey = NULL;
#endif
    p_hwcrhk_RandomBytes = NULL;
    p_hwcrhk_ModExpCRT = NULL;
    return to_return;
}

static int hwcrhk_ctrl(ENGINE *e, int cmd, long i, void *p, void (*f) (void))
{
    int to_return = 1;

    switch (cmd) {
    case HWCRHK_CMD_SO_PATH:
        if (hwcrhk_dso) {
            HWCRHKerr(HWCRHK_F_HWCRHK_CTRL, HWCRHK_R_ALREADY_LOADED);
            return 0;
        }
        if (p == NULL) {
            HWCRHKerr(HWCRHK_F_HWCRHK_CTRL, ERR_R_PASSED_NULL_PARAMETER);
            return 0;
        }
        return set_HWCRHK_LIBNAME((const char *)p);
    case ENGINE_CTRL_SET_LOGSTREAM:
        {
            BIO *bio = (BIO *)p;

            CRYPTO_THREAD_write_lock(chil_lock);
            BIO_free(logstream);
            logstream = NULL;
            if (BIO_up_ref(bio))
                logstream = bio;
            else
                HWCRHKerr(HWCRHK_F_HWCRHK_CTRL, HWCRHK_R_BIO_WAS_FREED);
        }
        CRYPTO_THREAD_unlock(chil_lock);
        break;
    case ENGINE_CTRL_SET_PASSWORD_CALLBACK:
        CRYPTO_THREAD_write_lock(chil_lock);
        password_context.password_callback = (pem_password_cb *)f;
        CRYPTO_THREAD_unlock(chil_lock);
        break;
    case ENGINE_CTRL_SET_USER_INTERFACE:
    case HWCRHK_CMD_SET_USER_INTERFACE:
        CRYPTO_THREAD_write_lock(chil_lock);
        password_context.ui_method = (UI_METHOD *)p;
        CRYPTO_THREAD_unlock(chil_lock);
        break;
    case ENGINE_CTRL_SET_CALLBACK_DATA:
    case HWCRHK_CMD_SET_CALLBACK_DATA:
        CRYPTO_THREAD_write_lock(chil_lock);
        password_context.callback_data = p;
        CRYPTO_THREAD_unlock(chil_lock);
        break;
        /*
         * this enables or disables the "SimpleForkCheck" flag used in the
         * initialisation structure.
         */
    case ENGINE_CTRL_CHIL_SET_FORKCHECK:
    case HWCRHK_CMD_FORK_CHECK:
        CRYPTO_THREAD_write_lock(chil_lock);
        if (i)
            hwcrhk_globals.flags |= HWCryptoHook_InitFlags_SimpleForkCheck;
        else
            hwcrhk_globals.flags &= ~HWCryptoHook_InitFlags_SimpleForkCheck;
        CRYPTO_THREAD_unlock(chil_lock);
        break;
        /*
         * This will prevent the initialisation function from "installing"
         * the mutex-handling callbacks, even if they are available from
         * within the library (or were provided to the library from the
         * calling application). This is to remove any baggage for
         * applications not using multithreading.
         */
    case ENGINE_CTRL_CHIL_NO_LOCKING:
        CRYPTO_THREAD_write_lock(chil_lock);
        disable_mutex_callbacks = 1;
        CRYPTO_THREAD_unlock(chil_lock);
        break;
    case HWCRHK_CMD_THREAD_LOCKING:
        CRYPTO_THREAD_write_lock(chil_lock);
        disable_mutex_callbacks = ((i == 0) ? 0 : 1);
        CRYPTO_THREAD_unlock(chil_lock);
        break;

        /* The command isn't understood by this engine */
    default:
        HWCRHKerr(HWCRHK_F_HWCRHK_CTRL,
                  HWCRHK_R_CTRL_COMMAND_NOT_IMPLEMENTED);
        to_return = 0;
        break;
    }

    return to_return;
}


static HWCryptoHook_MPI *hwcrhk_mpi_new(void)
{
    return hwcrhk_mpi_alloc(HWCRHK_MPI_DEFAULT_ALLOC_SIZE);
}

static HWCryptoHook_MPI *hwcrhk_mpi_alloc(size_t size)
{
    HWCryptoHook_MPI *mpi;

    mpi = OPENSSL_malloc(sizeof(HWCryptoHook_MPI) + size);
    if (mpi == NULL) {
        return NULL;
    }

    mpi->buf = ((unsigned char *)mpi) + sizeof(HWCryptoHook_MPI);
    mpi->size = size;

    return mpi;
}

static HWCryptoHook_MPI *hwcrhk_mpi_resize(HWCryptoHook_MPI *mpi, size_t size)
{
    if (mpi == NULL) {
        return NULL;
    }

    if (size <= HWCRHK_MPI_DEFAULT_ALLOC_SIZE) {
        mpi->size = size;
        return mpi;
    }

    mpi = OPENSSL_realloc(mpi, sizeof(HWCryptoHook_MPI) + size);
    if (mpi == NULL) {
        return NULL;
    }

    mpi->buf = ((unsigned char *)mpi) + sizeof(HWCryptoHook_MPI);
    mpi->size = size;

    return mpi;
}

static void hwcrhk_mpi_free(HWCryptoHook_MPI *mpi)
{
    if (mpi != NULL) {
        OPENSSL_free(mpi);
    }
}

static HWCryptoHook_MPI *hwcrhk_mpi_bn2mpi(const BIGNUM *bn)
{
    HWCryptoHook_MPI *mpi = NULL;
    size_t mpi_size;

    if (bn == NULL) {
        return NULL;
    }

    /* round up to the nearest BN_BYTES */
    mpi_size = ((size_t)(BN_num_bytes(bn) + BN_BYTES - 1) / BN_BYTES) * BN_BYTES;
    mpi = hwcrhk_mpi_alloc(mpi_size);

    if (mpi == NULL) {
        goto err;
    }

#ifdef L_ENDIAN
    if (BN_bn2lebinpad(bn, mpi->buf, mpi_size) != mpi_size) {
#else
    if (BN_bn2binpad(bn, mpi->buf, mpi_size) != mpi_size) {
#endif
        goto err;
    }

    return mpi;

 err:
    hwcrhk_mpi_free(mpi);

    return NULL;
}

static BIGNUM *hwcrhk_mpi_mpi2bn(const HWCryptoHook_MPI *mpi, BIGNUM *ret)
{
    if (mpi == NULL || mpi->buf == NULL) {
        return NULL;
    }

#ifdef L_ENDIAN
    return BN_lebin2bn(mpi->buf, mpi->size, ret);
#else
    return BN_bin2bn(mpi->buf, mpi->size, ret);
#endif
}


static EVP_PKEY *hwcrhk_load_privkey(ENGINE *eng, const char *key_id,
                                     UI_METHOD *ui_method,
                                     void *callback_data)
{
    EVP_PKEY *res = NULL;

#ifndef OPENSSL_NO_RSA
    RSA *rtmp = NULL;
    BIGNUM *bn_e = NULL, *bn_n = NULL;
    HWCryptoHook_MPI *e = NULL, *n = NULL;
    HWCryptoHook_RSAKeyHandle *hptr;

    char tempbuf[1024];
    HWCryptoHook_ErrMsgBuf rmsg;
    int ret = 0, attempt;
    HWCryptoHook_PassphraseContext ppctx;

    rmsg.buf = tempbuf;
    rmsg.size = sizeof(tempbuf);
#endif

    if (!hwcrhk_context) {
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY, HWCRHK_R_NOT_INITIALISED);
        goto err;
    }

#ifndef OPENSSL_NO_RSA
    hptr = OPENSSL_malloc(sizeof(*hptr));
    if (hptr == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    ppctx.ui_method = ui_method;
    ppctx.callback_data = callback_data;
    if (p_hwcrhk_RSALoadKey(hwcrhk_context, key_id, hptr, &rmsg, &ppctx)) {
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY, HWCRHK_R_CHIL_ERROR);
        ERR_add_error_data(1, rmsg.buf);
        goto err;
    }

    if (!*hptr) {
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY, HWCRHK_R_NO_KEY);
        goto err;
    }

    /* guess the starting size of n */
    n = hwcrhk_mpi_alloc(HWCRHK_MPI_RSA_ALLOC_SIZE);
    e = hwcrhk_mpi_new();

    if (!n || !e) {
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY,
                  ERR_R_MALLOC_FAILURE);
        goto err;
    }

    for (attempt = 0; attempt < 2; ++attempt) {
        ret = p_hwcrhk_RSAGetPublicKey(*hptr, n, e, &rmsg);

        if (ret != HWCRYPTOHOOK_ERROR_MPISIZE)
            break;

        /* the guess was wrong, so resize and re-attempt */
        n = hwcrhk_mpi_resize(n, n->size);
        e = hwcrhk_mpi_resize(e, e->size);

        if (n == NULL || e == NULL) {
            HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    };

    if (ret < 0) {
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY, HWCRHK_R_CHIL_ERROR);
        ERR_add_error_data(1, rmsg.buf);
        goto err;
    }

    bn_e = hwcrhk_mpi_mpi2bn(e, NULL);
    bn_n = hwcrhk_mpi_mpi2bn(n, NULL);

    hwcrhk_mpi_free(e);
    hwcrhk_mpi_free(n);

    if (bn_e == NULL || bn_n == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    rtmp = RSA_new_method(eng);
    if (rtmp == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    RSA_set_ex_data(rtmp, hndidx_rsa, (char *)hptr);
    RSA_set0_key(rtmp, bn_n, bn_e, NULL);
    RSA_set_flags(rtmp, RSA_FLAG_EXT_PKEY);

    res = EVP_PKEY_new();
    if (res == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    EVP_PKEY_assign_RSA(res, rtmp);
#endif

    if (res == NULL)
        HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PRIVKEY,
                  HWCRHK_R_PRIVATE_KEY_ALGORITHMS_DISABLED);

    return res;

 err:
#ifndef OPENSSL_NO_RSA
    hwcrhk_mpi_free(e);
    hwcrhk_mpi_free(n);
    RSA_free(rtmp);
#endif
    return NULL;
}

static EVP_PKEY *hwcrhk_load_pubkey(ENGINE *eng, const char *key_id,
                                    UI_METHOD *ui_method, void *callback_data)
{
    EVP_PKEY *res = NULL;

#ifndef OPENSSL_NO_RSA
    res = hwcrhk_load_privkey(eng, key_id, ui_method, callback_data);
#endif

    if (res)
        switch (EVP_PKEY_id(res)) {
#ifndef OPENSSL_NO_RSA
        case EVP_PKEY_RSA:
            {
                RSA *rsa = NULL, *rtmp = NULL;
                const BIGNUM *bn_n = NULL, *bn_e = NULL;

                CRYPTO_THREAD_write_lock(chil_lock);

                rtmp = EVP_PKEY_get0_RSA(res);
                RSA_get0_key(rtmp, &bn_n, &bn_e, NULL);

                rsa = RSA_new();
                RSA_set0_key(rsa, BN_dup(bn_n), BN_dup(bn_e), NULL);
                EVP_PKEY_assign_RSA(res, rsa);

                CRYPTO_THREAD_unlock(chil_lock);
            }
            break;
#endif
        default:
            HWCRHKerr(HWCRHK_F_HWCRHK_LOAD_PUBKEY,
                      HWCRHK_R_CTRL_COMMAND_NOT_IMPLEMENTED);
            goto err;
        }

    return res;

 err:
    EVP_PKEY_free(res);
    return NULL;
}

/* A little mod_exp */
static int hwcrhk_bn_mod_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
                          const BIGNUM *m, BN_CTX *ctx)
{
    char tempbuf[1024];
    HWCryptoHook_ErrMsgBuf rmsg;
    /*
     * Since HWCryptoHook_MPI is pretty compatible with BIGNUM's, we use them
     * directly, plus a little macro magic.  We only thing we need to make
     * sure of is that enough space is allocated.
     */
    HWCryptoHook_MPI *m_a = NULL, *m_p = NULL, *m_m = NULL, *m_r = NULL;
    int to_return = 0, ret = 0, attempt;

    rmsg.buf = tempbuf;
    rmsg.size = sizeof(tempbuf);

    if (!hwcrhk_context) {
        HWCRHKerr(HWCRHK_F_HWCRHK_BN_MOD_EXP, HWCRHK_R_NOT_INITIALISED);
        goto err;
    }
    /* Prepare the params */
    m_a = hwcrhk_mpi_bn2mpi(a);
    m_p = hwcrhk_mpi_bn2mpi(p);
    m_m = hwcrhk_mpi_bn2mpi(m);

    /* guess that the result size will be the same size as a */
    m_r = hwcrhk_mpi_alloc(m_a->size);

    if (!m_a || !m_p || !m_m || !m_r) {
        HWCRHKerr(HWCRHK_F_HWCRHK_BN_MOD_EXP,
           ERR_R_MALLOC_FAILURE);
        goto err;
    }

    for (attempt = 0; attempt < 2; ++attempt) {
        ret = p_hwcrhk_ModExp(hwcrhk_context, *m_a, *m_p, *m_m, m_r, &rmsg);

        if (ret != HWCRYPTOHOOK_ERROR_MPISIZE)
            break;

        /* the guess was wrong, and m_r->size is the new size */
        m_r = hwcrhk_mpi_resize(m_r, m_r->size);
        if (m_r == NULL) {
            HWCRHKerr(HWCRHK_F_HWCRHK_BN_MOD_EXP, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    }

    /* Convert the response */
    hwcrhk_mpi_mpi2bn(m_r, r);

    if (ret < 0) {
        /*
         * FIXME: When this error is returned, HWCryptoHook is telling us
         * that falling back to software computation might be a good thing.
         */
        if (ret == HWCRYPTOHOOK_ERROR_FALLBACK) {
            HWCRHKerr(HWCRHK_F_HWCRHK_BN_MOD_EXP, HWCRHK_R_REQUEST_FALLBACK);
        } else {
            HWCRHKerr(HWCRHK_F_HWCRHK_BN_MOD_EXP, HWCRHK_R_REQUEST_FAILED);
        }
        ERR_add_error_data(1, rmsg.buf);
        goto err;
    }

    to_return = 1;

 err:
    hwcrhk_mpi_free(m_a);
    hwcrhk_mpi_free(m_p);
    hwcrhk_mpi_free(m_m);
    hwcrhk_mpi_free(m_r);

    return to_return;
}

#ifndef OPENSSL_NO_RSA
static int hwcrhk_rsa_mod_exp_remote(BIGNUM *r, const BIGNUM *I, RSA *rsa,
                              BN_CTX *ctx, HWCryptoHook_RSAKeyHandle *hptr)
{
    char tempbuf[1024];
    HWCryptoHook_ErrMsgBuf rmsg;
    int to_return = 0, ret = 0, attempt;

    HWCryptoHook_MPI *m_a = NULL, *m_r = NULL;
    const BIGNUM *n = NULL;

    rmsg.buf = tempbuf;
    rmsg.size = sizeof(tempbuf);

    RSA_get0_key(rsa, &n, NULL, NULL);

    if (!n) {
        HWCRHKerr(HWCRHK_F_HWCRHK_RSA_MOD_EXP,
                  HWCRHK_R_MISSING_KEY_COMPONENTS);
        goto err;
    }

    /* Prepare the params */
    m_a = hwcrhk_mpi_bn2mpi(I);

    /* guess that the result size will be the same size as a */
    m_r = hwcrhk_mpi_alloc(m_a->size);

    if (!m_a || !m_r) {
        HWCRHKerr(HWCRHK_F_HWCRHK_RSA_MOD_EXP,
           ERR_R_MALLOC_FAILURE);

        goto err;
    }

    for (attempt = 0; attempt < 2; ++attempt) {
        ret = p_hwcrhk_RSA(*m_a, *hptr, m_r, &rmsg);

        if (ret != HWCRYPTOHOOK_ERROR_MPISIZE)
            break;

        /* the guess was wrong, and m_r->size is the new size */
        m_r = hwcrhk_mpi_resize(m_r, m_r->size);
        if (m_r == NULL) {
            HWCRHKerr(HWCRHK_F_HWCRHK_BN_MOD_EXP, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    }

    /* Convert the response */
    hwcrhk_mpi_mpi2bn(m_r, r);

    if (ret < 0) {
        /*
         * FIXME: When this error is returned, HWCryptoHook is telling us
         * that falling back to software computation might be a good
         * thing.
         */
        if (ret == HWCRYPTOHOOK_ERROR_FALLBACK) {
            HWCRHKerr(HWCRHK_F_HWCRHK_RSA_MOD_EXP,
                      HWCRHK_R_REQUEST_FALLBACK);
        } else {
            HWCRHKerr(HWCRHK_F_HWCRHK_RSA_MOD_EXP,
                      HWCRHK_R_REQUEST_FAILED);
        }
        ERR_add_error_data(1, rmsg.buf);
        goto err;
    }

    to_return = 1;

 err:
    hwcrhk_mpi_free(m_a);
    hwcrhk_mpi_free(m_r);

    return to_return;
}

static int hwcrhk_rsa_mod_exp_local(BIGNUM *r, const BIGNUM *I, RSA *rsa,
                              BN_CTX *ctx)
{
    char tempbuf[1024];
    HWCryptoHook_ErrMsgBuf rmsg;
    int to_return = 0, ret = 0, attempt;

    HWCryptoHook_MPI *m_a = NULL, *m_p = NULL, *m_q = NULL;
    HWCryptoHook_MPI *m_dmp1 = NULL, *m_dmq1 = NULL, *m_iqmp = NULL, *m_r = NULL;
    const BIGNUM *p = NULL, *q = NULL;
    const BIGNUM *dmp1 = NULL, *dmq1 = NULL, *iqmp = NULL;

    rmsg.buf = tempbuf;
    rmsg.size = sizeof(tempbuf);

    RSA_get0_factors(rsa, &p, &q);
    RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);

    if (!p || !q || !dmp1 || !dmq1 || !iqmp) {
        HWCRHKerr(HWCRHK_F_HWCRHK_RSA_MOD_EXP,
                  HWCRHK_R_MISSING_KEY_COMPONENTS);
        goto err;
    }

    /* Prepare the params */
    m_a = hwcrhk_mpi_bn2mpi(I);
    m_p = hwcrhk_mpi_bn2mpi(p);
    m_q = hwcrhk_mpi_bn2mpi(q);
    m_dmp1 = hwcrhk_mpi_bn2mpi(dmp1);
    m_dmq1 = hwcrhk_mpi_bn2mpi(dmq1);
    m_iqmp = hwcrhk_mpi_bn2mpi(iqmp);

    /* guess that the result size will be the same size as a */
    m_r = hwcrhk_mpi_alloc(m_a->size);

    if (!m_a || !m_p || !m_q || !m_dmp1 || !m_dmq1 || !m_iqmp || !m_r) {
        HWCRHKerr(HWCRHK_F_HWCRHK_RSA_MOD_EXP,
                  ERR_R_MALLOC_FAILURE);
        goto err;
    }

    for (attempt = 0; attempt < 2; ++attempt) {
        ret = p_hwcrhk_ModExpCRT(hwcrhk_context, *m_a, *m_p, *m_q,
            *m_dmp1, *m_dmq1, *m_iqmp, m_r, &rmsg);

        if (ret != HWCRYPTOHOOK_ERROR_MPISIZE)
            break;

        /* the guess was wrong, and m_r->size is the new size */
        m_r = hwcrhk_mpi_resize(m_r, m_r->size);
        if (m_r == NULL) {
            HWCRHKerr(HWCRHK_F_HWCRHK_BN_MOD_EXP, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    }

    /* Convert the response */
    hwcrhk_mpi_mpi2bn(m_r, r);

    if (ret < 0) {
        /*
         * FIXME: When this error is returned, HWCryptoHook is telling us
         * that falling back to software computation might be a good
         * thing.
         */
        if (ret == HWCRYPTOHOOK_ERROR_FALLBACK) {
            HWCRHKerr(HWCRHK_F_HWCRHK_RSA_MOD_EXP,
                      HWCRHK_R_REQUEST_FALLBACK);
        } else {
            HWCRHKerr(HWCRHK_F_HWCRHK_RSA_MOD_EXP,
                      HWCRHK_R_REQUEST_FAILED);
        }
        ERR_add_error_data(1, rmsg.buf);
        goto err;
    }

    to_return = 1;

 err:
    hwcrhk_mpi_free(m_a);
    hwcrhk_mpi_free(m_p);
    hwcrhk_mpi_free(m_q);
    hwcrhk_mpi_free(m_dmp1);
    hwcrhk_mpi_free(m_dmq1);
    hwcrhk_mpi_free(m_iqmp);
    hwcrhk_mpi_free(m_r);

    return to_return;
}


static int hwcrhk_rsa_mod_exp(BIGNUM *r, const BIGNUM *I, RSA *rsa,
                              BN_CTX *ctx)
{
    int to_return = 0;
    HWCryptoHook_RSAKeyHandle *hptr;

    if (!hwcrhk_context) {
        HWCRHKerr(HWCRHK_F_HWCRHK_RSA_MOD_EXP, HWCRHK_R_NOT_INITIALISED);
        goto err;
    }

    /*
     * This provides support for nForce keys.  Since that's opaque data all
     * we do is provide a handle to the proper key and let HWCryptoHook take
     * care of the rest.
     */
    if ((hptr = (HWCryptoHook_RSAKeyHandle *) RSA_get_ex_data(rsa, hndidx_rsa))
        != NULL) {
        to_return = hwcrhk_rsa_mod_exp_remote(r, I, rsa, ctx, hptr);
    } else {
        to_return = hwcrhk_rsa_mod_exp_local(r, I, rsa, ctx);
    }

 err:
    return to_return;
}

/* This function is aliased to mod_exp (with the mont stuff dropped). */
static int hwcrhk_rsa_bn_mod_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
                               const BIGNUM *m, BN_CTX *ctx,
                               BN_MONT_CTX *m_ctx)
{
    return hwcrhk_bn_mod_exp(r, a, p, m, ctx);
}

static int hwcrhk_rsa_finish(RSA *rsa)
{
    HWCryptoHook_RSAKeyHandle *hptr;

    hptr = RSA_get_ex_data(rsa, hndidx_rsa);
    if (hptr) {
        p_hwcrhk_RSAUnloadKey(*hptr, NULL);
        OPENSSL_free(hptr);
        RSA_set_ex_data(rsa, hndidx_rsa, NULL);
    }
    return 1;
}

#endif

#ifndef OPENSSL_NO_DH
/* This function is aliased to mod_exp (with the dh and mont dropped). */
static int hwcrhk_dh_bn_mod_exp(const DH *dh, BIGNUM *r,
                             const BIGNUM *a, const BIGNUM *p,
                             const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx)
{
    return hwcrhk_bn_mod_exp(r, a, p, m, ctx);
}
#endif

/* Random bytes are good */
static int hwcrhk_rand_bytes(unsigned char *buf, int num)
{
    char tempbuf[1024];
    HWCryptoHook_ErrMsgBuf rmsg;
    int to_return = 0, ret;

    rmsg.buf = tempbuf;
    rmsg.size = sizeof(tempbuf);

    if (!hwcrhk_context) {
        HWCRHKerr(HWCRHK_F_HWCRHK_RAND_BYTES, HWCRHK_R_NOT_INITIALISED);
        goto err;
    }

    ret = p_hwcrhk_RandomBytes(hwcrhk_context, buf, num, &rmsg);

    if (ret < 0) {
        /*
         * FIXME: When this error is returned, HWCryptoHook is telling us
         * that falling back to software computation might be a good thing.
         */
        if (ret == HWCRYPTOHOOK_ERROR_FALLBACK) {
            HWCRHKerr(HWCRHK_F_HWCRHK_RAND_BYTES, HWCRHK_R_REQUEST_FALLBACK);
        } else {
            HWCRHKerr(HWCRHK_F_HWCRHK_RAND_BYTES, HWCRHK_R_REQUEST_FAILED);
        }
        ERR_add_error_data(1, rmsg.buf);
        goto err;
    }

    to_return = 1;

 err:
    return to_return;
}

static int hwcrhk_rand_status(void)
{
    return 1;
}

/*
 * Mutex calls: since the HWCryptoHook model closely follows the POSIX model
 * these just wrap the POSIX functions and add some logging.
 */

static int hwcrhk_mutex_init(HWCryptoHook_Mutex * mt,
                             HWCryptoHook_CallerContext * cactx)
{
    mt->lock = CRYPTO_THREAD_lock_new();
    if (mt->lock == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_MUTEX_INIT, ERR_R_MALLOC_FAILURE);
        return 1;               /* failure */
    }
    return 0;                   /* success */
}

static int hwcrhk_mutex_lock(HWCryptoHook_Mutex * mt)
{
    CRYPTO_THREAD_write_lock(mt->lock);
    return 0;
}

static void hwcrhk_mutex_unlock(HWCryptoHook_Mutex * mt)
{
    CRYPTO_THREAD_unlock(mt->lock);
}

static void hwcrhk_mutex_destroy(HWCryptoHook_Mutex * mt)
{
    CRYPTO_THREAD_lock_free(mt->lock);
}

static int hwcrhk_get_pass(const char *prompt_info,
                           int *len_io, char *buf,
                           HWCryptoHook_PassphraseContext * ppctx,
                           HWCryptoHook_CallerContext * cactx)
{
    pem_password_cb *callback = NULL;
    void *callback_data = NULL;
    UI_METHOD *ui_method = NULL;
    /*
     * Despite what the documentation says prompt_info can be an empty
     * string.
     */
    if (prompt_info && !*prompt_info)
        prompt_info = NULL;

    if (cactx) {
        if (cactx->ui_method)
            ui_method = cactx->ui_method;
        if (cactx->password_callback)
            callback = cactx->password_callback;
        if (cactx->callback_data)
            callback_data = cactx->callback_data;
    }
    if (ppctx) {
        if (ppctx->ui_method) {
            ui_method = ppctx->ui_method;
            callback = NULL;
        }
        if (ppctx->callback_data)
            callback_data = ppctx->callback_data;
    }
    if (callback == NULL && ui_method == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_GET_PASS, HWCRHK_R_NO_CALLBACK);
        return -1;
    }

    if (ui_method) {
        UI *ui = UI_new_method(ui_method);
        if (ui) {
            int ok;
            char *prompt = UI_construct_prompt(ui,
                                               "pass phrase", prompt_info);

            ok = UI_add_input_string(ui, prompt,
                                     UI_INPUT_FLAG_DEFAULT_PWD,
                                     buf, 0, (*len_io) - 1);
            UI_add_user_data(ui, callback_data);
            UI_ctrl(ui, UI_CTRL_PRINT_ERRORS, 1, 0, 0);

            if (ok >= 0)
                do {
                    ok = UI_process(ui);
                }
                while (ok < 0 && UI_ctrl(ui, UI_CTRL_IS_REDOABLE, 0, 0, 0));

            if (ok >= 0)
                *len_io = strlen(buf);

            UI_free(ui);
            OPENSSL_free(prompt);
        }
    } else {
        *len_io = callback(buf, *len_io, 0, callback_data);
    }
    if (!*len_io)
        return -1;
    return 0;
}

static int hwcrhk_insert_card(const char *prompt_info,
                              const char *wrong_info,
                              HWCryptoHook_PassphraseContext * ppctx,
                              HWCryptoHook_CallerContext * cactx)
{
    int ok = -1;
    UI *ui;
    void *callback_data = NULL;
    UI_METHOD *ui_method = NULL;

    if (cactx) {
        if (cactx->ui_method)
            ui_method = cactx->ui_method;
        if (cactx->callback_data)
            callback_data = cactx->callback_data;
    }
    if (ppctx) {
        if (ppctx->ui_method)
            ui_method = ppctx->ui_method;
        if (ppctx->callback_data)
            callback_data = ppctx->callback_data;
    }
    if (ui_method == NULL) {
        HWCRHKerr(HWCRHK_F_HWCRHK_INSERT_CARD, HWCRHK_R_NO_CALLBACK);
        return -1;
    }

    ui = UI_new_method(ui_method);

    if (ui) {
        char answer = '\0';
        char buf[BUFSIZ];
        /*
         * Despite what the documentation says wrong_info can be an empty
         * string.
         */
        if (wrong_info && *wrong_info)
            BIO_snprintf(buf, sizeof(buf) - 1,
                         "Current card: \"%s\"\n", wrong_info);
        else
            buf[0] = 0;
        ok = UI_dup_info_string(ui, buf);
        if (ok >= 0 && prompt_info) {
            BIO_snprintf(buf, sizeof(buf) - 1,
                         "Insert card \"%s\"", prompt_info);
            ok = UI_dup_input_boolean(ui, buf,
                                      "\n then hit <enter> or C<enter> to cancel\n",
                                      "\r\n", "Cc", UI_INPUT_FLAG_ECHO,
                                      &answer);
        }
        UI_add_user_data(ui, callback_data);

        if (ok >= 0)
            ok = UI_process(ui);
        UI_free(ui);

        if (ok == -2 || (ok >= 0 && answer == 'C'))
            ok = 1;
        else if (ok < 0)
            ok = -1;
        else
            ok = 0;
    }
    return ok;
}

static void hwcrhk_log_message(void *logstr, const char *message)
{
    BIO *lstream = NULL;

    if (logstr)
        lstream = *(BIO **)logstr;
    if (lstream) {
        BIO_printf(lstream, "%s\n", message);
    }
}

/*
 * This stuff is needed to compile this ENGINE into a DSO.
 */
static int bind_fn(ENGINE *e, const char *id)
{
    if (id && (strcmp(id, engine_hwcrhk_id) != 0) &&
        (strcmp(id, engine_hwcrhk_id_alt) != 0))
        return 0;
    if (!bind_helper(e))
        return 0;
    return 1;
}

IMPLEMENT_DYNAMIC_CHECK_FN()
IMPLEMENT_DYNAMIC_BIND_FN(bind_fn)
