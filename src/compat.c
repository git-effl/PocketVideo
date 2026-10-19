/*
 * OpenSSL 1.0.2 to 1.1.1 Compatibility Shim for VitaSDK libcurl
 *
 * VitaSDK libcurl.a references legacy OpenSSL 1.0.2 function names
 * (EVP_MD_CTX_create, SSLeay, SSLv23_client_method, sk_*, SSL_library_init, etc.).
 * In OpenSSL 1.1.1, these map directly to the corresponding OPENSSL_*
 * and EVP_MD_CTX_new/free functions or are handled automatically.
 *
 * In addition, sets OPENSSL_armcap=0 and OPENSSL_armcap_P=0 to disable ARM NEON
 * assembly extensions in OpenSSL, forcing pure portable C arithmetic.
 * This completely avoids unimplemented NEON instructions on the Vita3K CPU recompiler.
 */

#include <stdlib.h>

typedef void OPENSSL_STACK;
typedef void (*OPENSSL_sk_freefunc)(void *);

extern int OPENSSL_sk_num(const OPENSSL_STACK *);
extern void *OPENSSL_sk_value(const OPENSSL_STACK *, int);
extern void *OPENSSL_sk_pop(OPENSSL_STACK *);
extern void OPENSSL_sk_pop_free(OPENSSL_STACK *, OPENSSL_sk_freefunc);

extern void *EVP_MD_CTX_new(void);
extern void EVP_MD_CTX_free(void *);
extern unsigned long OpenSSL_version_num(void);
extern const void *TLS_client_method(void);
extern const void *UI_null(void);

extern int OPENSSL_init_ssl(unsigned long long opts, const void *settings);
extern int OPENSSL_init_crypto(unsigned long long opts, const void *settings);

/* Exported OpenSSL ARM capability vector */
extern unsigned int OPENSSL_armcap_P;

/*
 * Force OpenSSL to use pure software C cryptography (OPENSSL_armcap_P = 0).
 * Vita3K's Dynarmic JIT lacks complete 32-bit ARM NEON instruction emulation;
 * setting armcap to 0 ensures safe, portable software crypto without crashes.
 */
__attribute__((constructor)) static void init_openssl_armcap_env(void) {
    setenv("OPENSSL_armcap", "0", 1);
    setenv("OPENSSL_CONF", "app0:assets/openssl.cnf", 1);
    setenv("SSL_CERT_FILE", "app0:assets/cacert.pem", 1);
    OPENSSL_armcap_P = 0;
}

/* Stack functions */
int sk_num(const void *st) {
    return OPENSSL_sk_num((const OPENSSL_STACK *)st);
}

void *sk_value(const void *st, int i) {
    return OPENSSL_sk_value((const OPENSSL_STACK *)st, i);
}

void *sk_pop(void *st) {
    return OPENSSL_sk_pop((OPENSSL_STACK *)st);
}

void sk_pop_free(void *st, void (*free_func)(void *)) {
    OPENSSL_sk_pop_free((OPENSSL_STACK *)st, (OPENSSL_sk_freefunc)free_func);
}

/* Digest / Context functions */
void *EVP_MD_CTX_create(void) {
    return EVP_MD_CTX_new();
}

void EVP_MD_CTX_destroy(void *ctx) {
    EVP_MD_CTX_free(ctx);
}

/* Version & Methods */
unsigned long SSLeay(void) {
    return OpenSSL_version_num();
}

const void *SSLv23_client_method(void) {
    return TLS_client_method();
}

const void *UI_OpenSSL(void) {
    return UI_null();
}

/* Legacy Initialization functions */
int SSL_library_init(void) {
    setenv("OPENSSL_armcap", "0", 1);
    setenv("OPENSSL_CONF", "app0:assets/openssl.cnf", 1);
    setenv("SSL_CERT_FILE", "app0:assets/cacert.pem", 1);
    OPENSSL_armcap_P = 0;
    OPENSSL_init_ssl(0, (void *)0);
    return 1;
}

void SSL_load_error_strings(void) {
    setenv("OPENSSL_armcap", "0", 1);
    OPENSSL_armcap_P = 0;
    OPENSSL_init_ssl(0x00200000ULL | 0x00000002ULL | 0x00000080ULL, (void *)0);
}

void OPENSSL_add_all_algorithms_noconf(void) {
    setenv("OPENSSL_armcap", "0", 1);
    OPENSSL_armcap_P = 0;
    OPENSSL_init_crypto(0x00000004ULL | 0x00000008ULL | 0x00000080ULL, (void *)0);
}

/* Legacy Cleanup functions (no-ops in OpenSSL 1.1.0+) */
void EVP_cleanup(void) {
}

void ENGINE_cleanup(void) {
}

void ERR_free_strings(void) {
}

void CONF_modules_free(void) {
}

void SSL_COMP_free_compression_methods(void) {
}
