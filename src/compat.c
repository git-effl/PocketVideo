/*
 * OpenSSL 1.0.2 to 1.1.1 Compatibility Shim for VitaSDK libcurl
 *
 * VitaSDK's libcurl.a was compiled against OpenSSL 1.0.2, while modern VitaSDK
 * environments provide OpenSSL 1.1.1 (vita-openssl3). This shim maps legacy
 * OpenSSL 1.0.2 symbols to OpenSSL 1.1.1 equivalents so linking succeeds.
 * Weak references (weakref) are used so that OpenSSL 1.0.2 toolchains link cleanly
 * without requiring OpenSSL 1.1.1 function exports.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#if __has_include(<openssl/crypto.h>)
#include <openssl/crypto.h>
#elif __has_include(<openssl/opensslv.h>)
#include <openssl/opensslv.h>
#endif

#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L

typedef void OPENSSL_STACK;

static int (*p_OPENSSL_sk_num)(const OPENSSL_STACK *) __attribute__((weakref("OPENSSL_sk_num")));
static void *(*p_OPENSSL_sk_value)(const OPENSSL_STACK *, int) __attribute__((weakref("OPENSSL_sk_value")));
static void *(*p_OPENSSL_sk_pop)(OPENSSL_STACK *) __attribute__((weakref("OPENSSL_sk_pop")));
static void (*p_OPENSSL_sk_pop_free)(OPENSSL_STACK *, void (*)(void *)) __attribute__((weakref("OPENSSL_sk_pop_free")));

static void *(*p_EVP_MD_CTX_new)(void) __attribute__((weakref("EVP_MD_CTX_new")));
static void (*p_EVP_MD_CTX_free)(void *) __attribute__((weakref("EVP_MD_CTX_free")));
static unsigned long (*p_OpenSSL_version_num)(void) __attribute__((weakref("OpenSSL_version_num")));
static const void *(*p_TLS_client_method)(void) __attribute__((weakref("TLS_client_method")));
static const void *(*p_UI_null)(void) __attribute__((weakref("UI_null")));

static int (*p_OPENSSL_init_ssl)(uint64_t, const void *) __attribute__((weakref("OPENSSL_init_ssl")));
static int (*p_OPENSSL_init_crypto)(uint64_t, const void *) __attribute__((weakref("OPENSSL_init_crypto")));

/* Stack functions */
int __attribute__((weak)) sk_num(const void *st) {
    if (p_OPENSSL_sk_num) return p_OPENSSL_sk_num((const OPENSSL_STACK *)st);
    return 0;
}

void * __attribute__((weak)) sk_value(const void *st, int i) {
    if (p_OPENSSL_sk_value) return p_OPENSSL_sk_value((const OPENSSL_STACK *)st, i);
    return NULL;
}

void * __attribute__((weak)) sk_pop(void *st) {
    if (p_OPENSSL_sk_pop) return p_OPENSSL_sk_pop((OPENSSL_STACK *)st);
    return NULL;
}

void __attribute__((weak)) sk_pop_free(void *st, void (*free_func)(void *)) {
    if (p_OPENSSL_sk_pop_free) {
        p_OPENSSL_sk_pop_free((OPENSSL_STACK *)st, free_func);
    }
}

/* Digest / Context functions */
void * __attribute__((weak)) EVP_MD_CTX_create(void) {
    if (p_EVP_MD_CTX_new) return p_EVP_MD_CTX_new();
    return NULL;
}

void __attribute__((weak)) EVP_MD_CTX_destroy(void *ctx) {
    if (p_EVP_MD_CTX_free && ctx) p_EVP_MD_CTX_free(ctx);
}

/* Version & Methods */
unsigned long __attribute__((weak)) SSLeay(void) {
    if (p_OpenSSL_version_num) return p_OpenSSL_version_num();
    return 0x1000200fL;
}

const void * __attribute__((weak)) SSLv23_client_method(void) {
    if (p_TLS_client_method) return p_TLS_client_method();
    return NULL;
}

const void * __attribute__((weak)) UI_OpenSSL(void) {
    if (p_UI_null) return p_UI_null();
    return NULL;
}

#define OPENSSL_INIT_LOAD_CONFIG       0x00000040ULL
#define OPENSSL_INIT_ADD_ALL_CIPHERS   0x00000004ULL
#define OPENSSL_INIT_ADD_ALL_DIGESTS   0x00000008ULL
#define OPENSSL_INIT_LOAD_CRYPTO_STRINGS 0x00000002ULL
#define OPENSSL_INIT_LOAD_SSL_STRINGS  0x00200000ULL

/* Legacy Initialization functions */
int __attribute__((weak)) SSL_library_init(void) {
    if (p_OPENSSL_init_ssl) {
        return p_OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
    }
    return 1;
}

void __attribute__((weak)) SSL_load_error_strings(void) {
    if (p_OPENSSL_init_ssl) {
        p_OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    }
}

void __attribute__((weak)) OPENSSL_add_all_algorithms_noconf(void) {
    if (p_OPENSSL_init_crypto) {
        p_OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
    }
}

/* Legacy Cleanup functions (no-ops in OpenSSL 1.1.0+) */
void __attribute__((weak)) EVP_cleanup(void) {}
void __attribute__((weak)) ENGINE_cleanup(void) {}
void __attribute__((weak)) ERR_free_strings(void) {}
void __attribute__((weak)) CONF_modules_free(void) {}
void __attribute__((weak)) SSL_COMP_free_compression_methods(void) {}

#endif /* OPENSSL_VERSION_NUMBER >= 0x10100000L */

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
