#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <reent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <curl/curl.h>

#ifdef __vita__
#include <psp2/types.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>

#define NET_PARAM_MEM_SIZE (4 * 1024 * 1024)
static char net_memory[NET_PARAM_MEM_SIZE] __attribute__((aligned(256)));
static int s_vita_net_inited = 0;
static SceUID s_resolver_id = -1;
static int s_resolver_created = 0;
#endif

static void net_log(const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
#ifdef __vita__
    sceClibPrintf("%s", buf);
#endif
    printf("%s\n", buf);
}

#ifdef __vita__
/*
 * Override VitaSDK newlib's _vita_net_init / _vita_net_term so that
 * sceNetInit + sceNetCtlInit are called strictly ONCE per process,
 * preventing duplicate-init errors (0x80410110 / 0x80412102) when
 * newlib socket/DNS helpers are invoked after net_init().
 */
int _vita_net_init(void) {
    if (s_vita_net_inited) {
        return 0;
    }

    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);

    SceNetInitParam net_param;
    memset(&net_param, 0, sizeof(net_param));
    net_param.memory = net_memory;
    net_param.size = sizeof(net_memory);
    net_param.flags = 0;

    int ret = sceNetInit(&net_param);
    if (ret < 0 && ret != (int)0x80410110) {
        net_log("[Network] sceNetInit failed: 0x%08X", ret);
        return ret;
    }

    ret = sceNetCtlInit();
    if (ret < 0 && ret != (int)0x80412102) {
        net_log("[Network] sceNetCtlInit failed: 0x%08X", ret);
        return ret;
    }

    s_vita_net_inited = 1;
    return 0;
}

void _vita_net_term(void) {
    if (!s_vita_net_inited) {
        return;
    }
    if (s_resolver_created && s_resolver_id >= 0) {
        sceNetResolverDestroy(s_resolver_id);
    }
    s_resolver_id = -1;
    s_resolver_created = 0;
    sceNetCtlTerm();
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    s_vita_net_inited = 0;
}

/*
 * Pure C RFC-compliant inet_pton (IPv4 & IPv6).
 * Avoids calling sceNetInetPton on hostnames like "yt.chocolatemoo53.com",
 * which otherwise logs SCE_NET_ERROR_EINVAL (0x80410116) on every DNS lookup.
 */
static int pv_inet_pton4(const char *src, unsigned char *dst) {
    int saw_digit = 0;
    int octets = 0;
    unsigned char tmp[4] = {0, 0, 0, 0};
    unsigned char *tp = tmp;

    int ch;
    while ((ch = (unsigned char)*src++) != '\0') {
        if (ch >= '0' && ch <= '9') {
            unsigned int val = (unsigned int)(*tp * 10 + (ch - '0'));
            if (saw_digit && *tp == 0) {
                return 0; /* Reject leading zero */
            }
            if (val > 255) {
                return 0;
            }
            *tp = (unsigned char)val;
            if (!saw_digit) {
                if (++octets > 4) {
                    return 0;
                }
                saw_digit = 1;
            }
        } else if (ch == '.' && saw_digit) {
            if (octets == 4) {
                return 0;
            }
            *++tp = 0;
            saw_digit = 0;
        } else {
            return 0;
        }
    }

    if (octets < 4) {
        return 0;
    }
    memcpy(dst, tmp, 4);
    return 1;
}

static int pv_inet_pton6(const char *src, unsigned char *dst) {
    static const char xdigits_l[] = "0123456789abcdef";
    static const char xdigits_u[] = "0123456789ABCDEF";
    unsigned char tmp[16], *tp = tmp, *endp = tmp + 16, *colonp = NULL;
    const char *curtok;
    int ch, seen_xdigits = 0;
    unsigned int val = 0;

    memset(tmp, 0, sizeof(tmp));
    if (*src == ':' && *++src != ':') {
        return 0;
    }
    curtok = src;

    while ((ch = (unsigned char)*src++) != '\0') {
        const char *pch = strchr(xdigits_l, ch);
        if (!pch) {
            pch = strchr(xdigits_u, ch);
        }
        if (pch) {
            unsigned int nib = (unsigned int)(pch - (strchr(xdigits_l, ch) ? xdigits_l : xdigits_u));
            val <<= 4;
            val |= nib;
            if (++seen_xdigits > 4) {
                return 0;
            }
            continue;
        }
        if (ch == ':') {
            curtok = src;
            if (!seen_xdigits) {
                if (colonp) {
                    return 0;
                }
                colonp = tp;
                continue;
            } else if (*src == '\0') {
                return 0;
            }
            if (tp + 2 > endp) {
                return 0;
            }
            *tp++ = (unsigned char)((val >> 8) & 0xff);
            *tp++ = (unsigned char)(val & 0xff);
            seen_xdigits = 0;
            val = 0;
            continue;
        }
        if (ch == '.' && ((tp + 4) <= endp) && pv_inet_pton4(curtok, tp) > 0) {
            tp += 4;
            seen_xdigits = 0;
            break;
        }
        return 0;
    }

    if (seen_xdigits) {
        if (tp + 2 > endp) {
            return 0;
        }
        *tp++ = (unsigned char)((val >> 8) & 0xff);
        *tp++ = (unsigned char)(val & 0xff);
    }
    if (colonp != NULL) {
        size_t n = (size_t)(tp - colonp);
        if (tp == endp) {
            return 0;
        }
        for (size_t i = 1; i <= n; i++) {
            endp[-(int)i] = colonp[n - i];
            colonp[n - i] = 0;
        }
        tp = endp;
    }
    if (tp != endp) {
        return 0;
    }
    memcpy(dst, tmp, 16);
    return 1;
}

int inet_pton(int af, const char *restrict src, void *restrict dst) {
    if (!src || !dst) {
        return 0;
    }
    if (af == AF_INET) {
        return pv_inet_pton4(src, (unsigned char *)dst);
    }
    if (af == AF_INET6) {
        return pv_inet_pton6(src, (unsigned char *)dst);
    }
    errno = EAFNOSUPPORT;
    return -1;
}

/*
 * DNS lookup via sceNetResolverStartNtoa with static caching and smart
 * resolver ID handling (avoids stubbed sceNetResolverCreate/Destroy
 * warnings in Vita3K while supporting real PS Vita hardware fallback).
 */
struct hostent *gethostbyname(const char *name) {
    static struct hostent s_he;
    static char *s_aliases[1] = {NULL};
    static char *s_addr_list[2] = {NULL, NULL};
    static struct in_addr s_addr;
    static char s_hostname[256];
    static char s_cached_name[256];
    static struct in_addr s_cached_addr;

    if (!name || name[0] == '\0') {
        return NULL;
    }

    if (_vita_net_init() < 0) {
        return NULL;
    }

    memset(&s_addr, 0, sizeof(s_addr));

    if (pv_inet_pton4(name, (unsigned char *)&s_addr.s_addr) == 1) {
        /* Numeric IPv4 address */
    } else if (s_cached_name[0] != '\0' && strcmp(s_cached_name, name) == 0) {
        s_addr = s_cached_addr;
    } else {
        SceNetInAddr resolved;
        memset(&resolved, 0, sizeof(resolved));

        int ret = -1;
        if (s_resolver_id >= 0) {
            ret = sceNetResolverStartNtoa(s_resolver_id, name, &resolved, 0, 0, 0);
        } else {
            /* Try rid = 1 directly first (works natively in Vita3K HLE without stub warnings) */
            ret = sceNetResolverStartNtoa(1, name, &resolved, 0, 0, 0);
            if (ret < 0) {
                /* Real PS Vita hardware: create persistent resolver once */
                SceUID created = sceNetResolverCreate("pv_resolver", NULL, 0);
                if (created >= 0) {
                    s_resolver_id = created;
                    s_resolver_created = 1;
                    ret = sceNetResolverStartNtoa(s_resolver_id, name, &resolved, 0, 0, 0);
                }
            }
        }

        if (ret < 0) {
            net_log("[Network] DNS resolution failed for '%s' (0x%08X)", name, ret);
            return NULL;
        }

        s_addr.s_addr = resolved.s_addr;
        snprintf(s_cached_name, sizeof(s_cached_name), "%s", name);
        s_cached_addr = s_addr;
    }

    snprintf(s_hostname, sizeof(s_hostname), "%s", name);
    s_addr_list[0] = (char *)&s_addr;
    s_addr_list[1] = NULL;

    memset(&s_he, 0, sizeof(s_he));
    s_he.h_name = s_hostname;
    s_he.h_aliases = s_aliases;
    s_he.h_addrtype = AF_INET;
    s_he.h_length = sizeof(struct in_addr);
    s_he.h_addr_list = s_addr_list;

    return &s_he;
}

/*
 * POSIX socket wrappers (-Wl,--wrap=...) to prevent EWOULDBLOCK (0x80410123)
 * errors on sceNetConnect and sceNetRecv/sceNetSend in Vita3K.
 */
#define PV_MAX_FDS 512
static volatile unsigned char s_fd_is_socket[PV_MAX_FDS];
static volatile unsigned char s_fd_nonblock[PV_MAX_FDS];
static volatile unsigned char s_fd_connected[PV_MAX_FDS];

extern int __real_socket(int domain, int type, int protocol);
extern int __real__close_r(struct _reent *reent, int fd);
extern int __real_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
extern int __real__fcntl_r(struct _reent *reent, int fd, int cmd, int arg);
extern int __real_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern ssize_t __real_recv(int sockfd, void *buf, size_t len, int flags);
extern _ssize_t __real__read_r(struct _reent *reent, int fd, void *ptr, size_t len);
extern ssize_t __real_send(int sockfd, const void *buf, size_t len, int flags);
extern _ssize_t __real__write_r(struct _reent *reent, int fd, const void *buf, size_t nbytes);

static void pv_set_eagain(struct _reent *reent) {
    errno = EAGAIN;
    if (reent) {
        reent->_errno = EAGAIN;
    }
}

int __wrap_socket(int domain, int type, int protocol) {
    if (_vita_net_init() < 0) {
        errno = ENETDOWN;
        return -1;
    }
    int fd = __real_socket(domain, type, protocol);
    if (fd >= 0 && fd < PV_MAX_FDS) {
        s_fd_is_socket[fd] = 1;
        s_fd_nonblock[fd] = 0;
        s_fd_connected[fd] = 0;
#ifdef SOCK_NONBLOCK
        if (type & SOCK_NONBLOCK) {
            s_fd_nonblock[fd] = 1;
        }
#endif
    }
    return fd;
}

int __wrap__close_r(struct _reent *reent, int fd) {
    if (fd >= 0 && fd < PV_MAX_FDS) {
        s_fd_is_socket[fd] = 0;
        s_fd_nonblock[fd] = 0;
        s_fd_connected[fd] = 0;
    }
    return __real__close_r(reent, fd);
}

int __wrap_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    if (sockfd >= 0 && sockfd < PV_MAX_FDS && s_fd_is_socket[sockfd]) {
#ifdef SO_NONBLOCK
        if (level == SOL_SOCKET && optname == SO_NONBLOCK && optval && optlen >= (socklen_t)sizeof(int)) {
            int nb = (*(const int *)optval) ? 1 : 0;
            s_fd_nonblock[sockfd] = (unsigned char)nb;
            if (!s_fd_connected[sockfd] && nb) {
                /* Defer SCE_NET_SO_NBIO until connect() completes */
                return 0;
            }
        }
#endif
    }
    return __real_setsockopt(sockfd, level, optname, optval, optlen);
}

int __wrap__fcntl_r(struct _reent *reent, int fd, int cmd, int arg) {
    if (fd >= 0 && fd < PV_MAX_FDS && s_fd_is_socket[fd]) {
        if (cmd == F_SETFL) {
            int nb = (arg & O_NONBLOCK) ? 1 : 0;
            s_fd_nonblock[fd] = (unsigned char)nb;
            if (!s_fd_connected[fd] && nb) {
                return __real__fcntl_r(reent, fd, cmd, arg & ~O_NONBLOCK);
            }
        }
    }
    int ret = __real__fcntl_r(reent, fd, cmd, arg);
    if (ret >= 0 && fd >= 0 && fd < PV_MAX_FDS && s_fd_is_socket[fd] && cmd == F_GETFL) {
        if (s_fd_nonblock[fd]) {
            ret |= O_NONBLOCK;
        }
    }
    return ret;
}

int __wrap_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (sockfd >= 0 && sockfd < PV_MAX_FDS && s_fd_is_socket[sockfd] && !s_fd_connected[sockfd]) {
        int want_nb = s_fd_nonblock[sockfd];
        if (want_nb && addr && addr->sa_family == AF_INET) {
            struct timeval tv;
            tv.tv_sec = 8;
            tv.tv_usec = 0;
            __real_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            __real_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            int ret = __real_connect(sockfd, addr, addrlen);

            struct timeval zero_tv = {0, 0};
            __real_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &zero_tv, sizeof(zero_tv));
            __real_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &zero_tv, sizeof(zero_tv));

            if (ret == 0) {
                s_fd_connected[sockfd] = 1;
#ifdef SO_NONBLOCK
                int nb = 1;
                __real_setsockopt(sockfd, SOL_SOCKET, SO_NONBLOCK, &nb, sizeof(nb));
#endif
            }
            return ret;
        }
        int ret = __real_connect(sockfd, addr, addrlen);
        if (ret == 0) {
            s_fd_connected[sockfd] = 1;
        }
        return ret;
    }
    return __real_connect(sockfd, addr, addrlen);
}

ssize_t __wrap_recv(int sockfd, void *buf, size_t len, int flags) {
    if (len > 0 && sockfd >= 0 && sockfd < PV_MAX_FDS && s_fd_is_socket[sockfd] && s_fd_nonblock[sockfd]) {
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 0);
        if (pr == 0) {
            pv_set_eagain(NULL);
            return -1;
        }
    }
    return __real_recv(sockfd, buf, len, flags);
}

_ssize_t __wrap__read_r(struct _reent *reent, int fd, void *ptr, size_t len) {
    if (len > 0 && fd >= 0 && fd < PV_MAX_FDS && s_fd_is_socket[fd] && s_fd_nonblock[fd]) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 0);
        if (pr == 0) {
            pv_set_eagain(reent);
            return -1;
        }
    }
    return __real__read_r(reent, fd, ptr, len);
}

ssize_t __wrap_send(int sockfd, const void *buf, size_t len, int flags) {
    if (len > 0 && sockfd >= 0 && sockfd < PV_MAX_FDS && s_fd_is_socket[sockfd] && s_fd_nonblock[sockfd]) {
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 0);
        if (pr == 0) {
            pv_set_eagain(NULL);
            return -1;
        }
    }
    return __real_send(sockfd, buf, len, flags);
}

_ssize_t __wrap__write_r(struct _reent *reent, int fd, const void *buf, size_t nbytes) {
    if (nbytes > 0 && fd >= 0 && fd < PV_MAX_FDS && s_fd_is_socket[fd] && s_fd_nonblock[fd]) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 0);
        if (pr == 0) {
            pv_set_eagain(reent);
            return -1;
        }
    }
    return __real__write_r(reent, fd, buf, nbytes);
}
#endif /* __vita__ */

typedef struct {
    char *memory;
    size_t size;
} MemoryChunk;

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryChunk *mem = (MemoryChunk *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        net_log("[Network] ERROR: Out of memory reallocating buffer (%lu bytes)",
                (unsigned long)(mem->size + realsize + 1));
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

int net_init(void) {
    setenv("OPENSSL_armcap", "0", 1);
    setenv("OPENSSL_CONF", "app0:assets/openssl.cnf", 1);

#ifdef __vita__
    int ret = _vita_net_init();
    if (ret < 0) {
        return ret;
    }
#endif

    CURLcode curl_ret = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_ret != CURLE_OK) {
        net_log("[Network] curl_global_init failed: %s", curl_easy_strerror(curl_ret));
        return -1;
    }

    net_log("[Network] Network subsystem initialized successfully.");
    return 0;
}

void net_term(void) {
    curl_global_cleanup();
#ifdef __vita__
    _vita_net_term();
#endif
}

int http_get(const char *url, HttpResponse *out_response) {
    if (!url || !out_response) {
        return -1;
    }

    memset(out_response, 0, sizeof(HttpResponse));

    CURL *curl_handle = curl_easy_init();
    if (!curl_handle) {
        snprintf(out_response->error_msg, sizeof(out_response->error_msg), "Failed to initialize cURL handle");
        return -1;
    }

    MemoryChunk chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;
    if (chunk.memory) {
        chunk.memory[0] = '\0';
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "PocketVideo/1.0");
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl_handle, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_CAINFO, NULL);
    curl_easy_setopt(curl_handle, CURLOPT_CAPATH, NULL);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_CIPHER_LIST, "DEFAULT:!aNULL:!eNULL:!MD5");
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Accept-Encoding: identity");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        snprintf(out_response->error_msg, sizeof(out_response->error_msg),
                 "cURL Error [%d]: %s", res, curl_easy_strerror(res));
        net_log("[Network] HTTP GET failed: %s", out_response->error_msg);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl_handle);
        if (chunk.memory) free(chunk.memory);
        return -2;
    }

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &out_response->http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl_handle);

    out_response->data = chunk.memory;
    out_response->size = chunk.size;

    net_log("[Network] HTTP %ld received (%lu bytes) from %s",
            out_response->http_status, (unsigned long)out_response->size, url);

    if (out_response->http_status != 200) {
        snprintf(out_response->error_msg, sizeof(out_response->error_msg),
                 "Server returned HTTP status code %ld", out_response->http_status);
        return -3;
    }

    return 0;
}

void http_response_free(HttpResponse *res) {
    if (res && res->data) {
        free(res->data);
        res->data = NULL;
        res->size = 0;
    }
}
