#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *data;
    size_t size;
    long http_status;
    char error_msg[256];
} HttpResponse;

/**
 * Initialize PlayStation Vita network subsystem (sceNet, sceSsl, sceHttp, curl).
 * Must be called before making any network requests.
 * Returns 0 on success, negative error code on failure.
 */
int net_init(void);

/**
 * Terminate network subsystem and release memory pools.
 */
void net_term(void);

/**
 * Perform an HTTP GET request to the given URL using libcurl.
 * Handles timeouts, SSL verification / CA bundle fallback, and dynamic buffer allocation.
 *
 * @param url Full target URL (e.g. "https://invidious.f5.si/api/v1/trending")
 * @param out_response Pointer to HttpResponse structure to populate
 * @return 0 on HTTP 200 success, non-zero on network or HTTP error.
 */
int http_get(const char *url, HttpResponse *out_response);

/**
 * Free memory buffers associated with an HttpResponse.
 */
void http_response_free(HttpResponse *res);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */
