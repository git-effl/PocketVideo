/*
 * OpenSSL 1.0.2 to 1.1.1 Compatibility Shim for VitaSDK libcurl
 *
 * VitaSDK libcurl.a references legacy OpenSSL 1.0.2 function names
 * (EVP_MD_CTX_create, SSLeay, SSLv23_client_method, sk_*).
 * In OpenSSL 1.1.1, these map directly to the corresponding OPENSSL_*
 * and EVP_MD_CTX_new/free functions.
 */

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

void *EVP_MD_CTX_create(void) {
    return EVP_MD_CTX_new();
}

void EVP_MD_CTX_destroy(void *ctx) {
    EVP_MD_CTX_free(ctx);
}

unsigned long SSLeay(void) {
    return OpenSSL_version_num();
}

const void *SSLv23_client_method(void) {
    return TLS_client_method();
}

const void *UI_OpenSSL(void) {
    return UI_null();
}
