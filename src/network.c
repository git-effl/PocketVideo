#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>

#define NET_PARAM_MEM_SIZE (1 * 1024 * 1024)
static char net_memory[NET_PARAM_MEM_SIZE];
#endif

typedef struct {
    char *memory;
    size_t size;
} MemoryChunk;

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryChunk *mem = (MemoryChunk *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        printf("[Network] ERROR: Out of memory reallocating buffer (%zu bytes)\n", mem->size + realsize + 1);
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

int net_init(void) {
#ifdef __vita__
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);

    SceNetInitParam net_param;
    memset(&net_param, 0, sizeof(net_param));
    net_param.memory = net_memory;
    net_param.size = sizeof(net_memory);
    net_param.flags = 0;

    int ret = sceNetInit(&net_param);
    if (ret < 0) {
        printf("[Network] sceNetInit failed: 0x%08X\n", ret);
        return ret;
    }

    ret = sceNetCtlInit();
    if (ret < 0) {
        printf("[Network] sceNetCtlInit failed: 0x%08X\n", ret);
        return ret;
    }

    int state = 0;
    sceNetCtlInetGetState(&state);
    printf("[Network] NetCtl State: %d (Connected: %s)\n", 
           state, (state == SCE_NETCTL_STATE_CONNECTED) ? "YES" : "NO");
#endif

    CURLcode curl_ret = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_ret != CURLE_OK) {
        printf("[Network] curl_global_init failed: %s\n", curl_easy_strerror(curl_ret));
        return -1;
    }

    printf("[Network] Network subsystem initialized successfully.\n");
    return 0;
}

void net_term(void) {
    curl_global_cleanup();
#ifdef __vita__
    sceNetCtlTerm();
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
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

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "PocketVideo/1.0");
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 6L);

    FILE *ca_test = fopen("ux0:data/pocketvideo/cacert.pem", "r");
    if (ca_test) {
        fclose(ca_test);
        curl_easy_setopt(curl_handle, CURLOPT_CAINFO, "ux0:data/pocketvideo/cacert.pem");
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        snprintf(out_response->error_msg, sizeof(out_response->error_msg),
                 "cURL Error [%d]: %s", res, curl_easy_strerror(res));
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
