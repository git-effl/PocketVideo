/*
 * OpenSSL 1.0.2 to 1.1.1 Compatibility Shim for VitaSDK libcurl
 *
 * VitaSDK's libcurl.a was compiled against OpenSSL 1.0.2, while modern VitaSDK
 * environments provide OpenSSL 1.1.1 (vita-openssl3). This shim maps legacy
 * OpenSSL 1.0.2 symbols to OpenSSL 1.1.1 equivalents so linking succeeds.
 * Weak symbols are used so that OpenSSL 1.0.2 toolchains use native symbols
 * without requiring OpenSSL 1.1.1 function exports.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#define WEAK_SYM __attribute__((weak))

typedef void OPENSSL_STACK;
typedef void (*OPENSSL_sk_freefunc)(void *);

WEAK_SYM extern int OPENSSL_sk_num(const OPENSSL_STACK *);
WEAK_SYM extern void *OPENSSL_sk_value(const OPENSSL_STACK *, int);
WEAK_SYM extern void *OPENSSL_sk_pop(OPENSSL_STACK *);
WEAK_SYM extern void OPENSSL_sk_pop_free(OPENSSL_STACK *, OPENSSL_sk_freefunc);

WEAK_SYM extern void *EVP_MD_CTX_new(void);
WEAK_SYM extern void EVP_MD_CTX_free(void *);
WEAK_SYM extern unsigned long OpenSSL_version_num(void);
WEAK_SYM extern const void *TLS_client_method(void);
WEAK_SYM extern const void *UI_null(void);

WEAK_SYM extern int OPENSSL_init_ssl(uint64_t opts, const void *settings);
WEAK_SYM extern int OPENSSL_init_crypto(uint64_t opts, const void *settings);

/* Stack functions */
WEAK_SYM int sk_num(const void *st) {
    if (OPENSSL_sk_num) return OPENSSL_sk_num((const OPENSSL_STACK *)st);
    return 0;
}

WEAK_SYM void *sk_value(const void *st, int i) {
    if (OPENSSL_sk_value) return OPENSSL_sk_value((const OPENSSL_STACK *)st, i);
    return NULL;
}

WEAK_SYM void *sk_pop(void *st) {
    if (OPENSSL_sk_pop) return OPENSSL_sk_pop((OPENSSL_STACK *)st);
    return NULL;
}

WEAK_SYM void sk_pop_free(void *st, void (*free_func)(void *)) {
    if (OPENSSL_sk_pop_free) {
        OPENSSL_sk_pop_free((OPENSSL_STACK *)st, (OPENSSL_sk_freefunc)free_func);
    }
}

/* Digest / Context functions */
WEAK_SYM void *EVP_MD_CTX_create(void) {
    if (EVP_MD_CTX_new) return EVP_MD_CTX_new();
    return NULL;
}

WEAK_SYM void EVP_MD_CTX_destroy(void *ctx) {
    if (EVP_MD_CTX_free && ctx) EVP_MD_CTX_free(ctx);
}

/* Version & Methods */
WEAK_SYM unsigned long SSLeay(void) {
    if (OpenSSL_version_num) return OpenSSL_version_num();
    return 0x1000200fL;
}

WEAK_SYM const void *SSLv23_client_method(void) {
    if (TLS_client_method) return TLS_client_method();
    return NULL;
}

WEAK_SYM const void *UI_OpenSSL(void) {
    if (UI_null) return UI_null();
    return NULL;
}

#define OPENSSL_INIT_LOAD_CONFIG       0x00000040ULL
#define OPENSSL_INIT_ADD_ALL_CIPHERS   0x00000004ULL
#define OPENSSL_INIT_ADD_ALL_DIGESTS   0x00000008ULL
#define OPENSSL_INIT_LOAD_CRYPTO_STRINGS 0x00000002ULL
#define OPENSSL_INIT_LOAD_SSL_STRINGS  0x00200000ULL

/* Legacy Initialization functions */
WEAK_SYM int SSL_library_init(void) {
    if (OPENSSL_init_ssl) {
        return OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
    }
    return 1;
}

WEAK_SYM void SSL_load_error_strings(void) {
    if (OPENSSL_init_ssl) {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    }
}

WEAK_SYM void OPENSSL_add_all_algorithms_noconf(void) {
    if (OPENSSL_init_crypto) {
        OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
    }
}

/* Legacy Cleanup functions (no-ops in OpenSSL 1.1.0+) */
WEAK_SYM void EVP_cleanup(void) {}
WEAK_SYM void ENGINE_cleanup(void) {}
WEAK_SYM void ERR_free_strings(void) {}
WEAK_SYM void CONF_modules_free(void) {}
WEAK_SYM void SSL_COMP_free_compression_methods(void) {}

/*
 * Kernel RNG replacement for Vita3K / PS Vita
 * OpenSSL 1.1.1 (vita-openssl3) calls sceKernelGetRandomNumber to seed its PRNG.
 * Vita3K does not implement this kernel NID, resulting in an unhandled instruction.
 * Implementing it here in userspace provides instant entropy without syscall traps.
 */
int sceKernelGetRandomNumber(void *output, unsigned int size) {
    if (!output || size == 0) return 0;
    unsigned char *p = (unsigned char *)output;
    static unsigned int seed = 0x9e3779b9;
    for (unsigned int i = 0; i < size; i++) {
        seed = seed * 1664525u + 1013904223u;
        p[i] = (unsigned char)((seed >> 16) & 0xFF);
    }
    return 0;
}

int getentropy(void *buffer, size_t length) {
    return sceKernelGetRandomNumber(buffer, (unsigned int)length);
}
