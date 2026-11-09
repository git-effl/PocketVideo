/*
 * OpenSSL 1.0.2 to 1.1.1 Compatibility Shim for VitaSDK libcurl
 *
 * VitaSDK's libcurl.a was compiled against OpenSSL 1.0.2, while modern VitaSDK
 * environments provide OpenSSL 1.1.1 (vita-openssl3). This shim maps legacy
 * OpenSSL 1.0.2 symbols to OpenSSL 1.1.1 equivalents so linking succeeds.
 * It also provides a userspace implementation of sceKernelGetRandomNumber
 * which Vita3K's kernel HLE does not implement.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#if __has_include(<openssl/opensslv.h>)
#include <openssl/opensslv.h>
#endif

#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L

typedef void OPENSSL_STACK;
typedef void (*OPENSSL_sk_freefunc)(void *);

extern int OPENSSL_sk_num(const OPENSSL_STACK *) __attribute__((weak));
extern void *OPENSSL_sk_value(const OPENSSL_STACK *, int) __attribute__((weak));
extern void *OPENSSL_sk_pop(OPENSSL_STACK *) __attribute__((weak));
extern void OPENSSL_sk_pop_free(OPENSSL_STACK *, OPENSSL_sk_freefunc) __attribute__((weak));

extern void *EVP_MD_CTX_new(void) __attribute__((weak));
extern void EVP_MD_CTX_free(void *) __attribute__((weak));
extern unsigned long OpenSSL_version_num(void) __attribute__((weak));
extern const void *TLS_client_method(void) __attribute__((weak));
extern const void *UI_null(void) __attribute__((weak));

extern int OPENSSL_init_ssl(uint64_t opts, const void *settings) __attribute__((weak));
extern int OPENSSL_init_crypto(uint64_t opts, const void *settings) __attribute__((weak));

/* Stack functions */
int __attribute__((weak)) sk_num(const void *st) {
    if (OPENSSL_sk_num) return OPENSSL_sk_num((const OPENSSL_STACK *)st);
    return 0;
}

void * __attribute__((weak)) sk_value(const void *st, int i) {
    if (OPENSSL_sk_value) return OPENSSL_sk_value((const OPENSSL_STACK *)st, i);
    return NULL;
}

void * __attribute__((weak)) sk_pop(void *st) {
    if (OPENSSL_sk_pop) return OPENSSL_sk_pop((OPENSSL_STACK *)st);
    return NULL;
}

void __attribute__((weak)) sk_pop_free(void *st, void (*free_func)(void *)) {
    if (OPENSSL_sk_pop_free) {
        OPENSSL_sk_pop_free((OPENSSL_STACK *)st, (OPENSSL_sk_freefunc)free_func);
    }
}

/* Digest / Context functions */
void * __attribute__((weak)) EVP_MD_CTX_create(void) {
    if (EVP_MD_CTX_new) return EVP_MD_CTX_new();
    return NULL;
}

void __attribute__((weak)) EVP_MD_CTX_destroy(void *ctx) {
    if (EVP_MD_CTX_free && ctx) EVP_MD_CTX_free(ctx);
}

/* Version & Methods */
unsigned long __attribute__((weak)) SSLeay(void) {
    if (OpenSSL_version_num) return OpenSSL_version_num();
    return 0x1000200fL;
}

const void * __attribute__((weak)) SSLv23_client_method(void) {
    if (TLS_client_method) return TLS_client_method();
    return NULL;
}

const void * __attribute__((weak)) UI_OpenSSL(void) {
    if (UI_null) return UI_null();
    return NULL;
}

#define OPENSSL_INIT_LOAD_CONFIG       0x00000040ULL
#define OPENSSL_INIT_ADD_ALL_CIPHERS   0x00000004ULL
#define OPENSSL_INIT_ADD_ALL_DIGESTS   0x00000008ULL
#define OPENSSL_INIT_LOAD_CRYPTO_STRINGS 0x00000002ULL
#define OPENSSL_INIT_LOAD_SSL_STRINGS  0x00200000ULL

/* Legacy Initialization functions */
int __attribute__((weak)) SSL_library_init(void) {
    if (OPENSSL_init_ssl) {
        return OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
    }
    return 1;
}

void __attribute__((weak)) SSL_load_error_strings(void) {
    if (OPENSSL_init_ssl) {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    }
}

void __attribute__((weak)) OPENSSL_add_all_algorithms_noconf(void) {
    if (OPENSSL_init_crypto) {
        OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
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
