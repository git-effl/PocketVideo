/*
 * OpenSSL 1.0.2 to 1.1.1 Compatibility Shim for VitaSDK libcurl
 *
 * VitaSDK's libcurl.a was compiled against OpenSSL 1.0.2, while modern VitaSDK
 * environments provide OpenSSL 1.1.1 (vita-openssl3). This shim maps legacy
 * OpenSSL 1.0.2 symbols to OpenSSL 1.1.1 equivalents using standard OpenSSL headers.
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

#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/stack.h>
#include <openssl/ui.h>

#define WEAK_SYM __attribute__((weak))

/* Stack functions */
WEAK_SYM int sk_num(const void *st) {
    return OPENSSL_sk_num((const OPENSSL_STACK *)st);
}

WEAK_SYM void *sk_value(const void *st, int i) {
    return OPENSSL_sk_value((const OPENSSL_STACK *)st, i);
}

WEAK_SYM void *sk_pop(void *st) {
    return OPENSSL_sk_pop((OPENSSL_STACK *)st);
}

WEAK_SYM void sk_pop_free(void *st, void (*free_func)(void *)) {
    OPENSSL_sk_pop_free((OPENSSL_STACK *)st, (OPENSSL_sk_freefunc)free_func);
}

/* Digest / Context functions */
WEAK_SYM void *EVP_MD_CTX_create(void) {
    return EVP_MD_CTX_new();
}

WEAK_SYM void EVP_MD_CTX_destroy(void *ctx) {
    if (ctx) EVP_MD_CTX_free(ctx);
}

/* Version & Methods */
WEAK_SYM unsigned long SSLeay(void) {
    return OpenSSL_version_num();
}

WEAK_SYM const void *SSLv23_client_method(void) {
    return TLS_client_method();
}

WEAK_SYM const void *UI_OpenSSL(void) {
    return UI_null();
}

/* Legacy Initialization functions */
WEAK_SYM int SSL_library_init(void) {
    return OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
}

WEAK_SYM void SSL_load_error_strings(void) {
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
}

WEAK_SYM void OPENSSL_add_all_algorithms_noconf(void) {
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
}

/* Legacy Cleanup functions (no-ops in OpenSSL 1.1.0+) */
WEAK_SYM void EVP_cleanup(void) {}
WEAK_SYM void ENGINE_cleanup(void) {}
WEAK_SYM void ERR_free_strings(void) {}
WEAK_SYM void CONF_modules_free(void) {}
WEAK_SYM void SSL_COMP_free_compression_methods(void) {}

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
