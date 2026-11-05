#include "invidious.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <psp2/kernel/clib.h>
#define inv_log(fmt, ...) do { sceClibPrintf(fmt, ##__VA_ARGS__); printf(fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define inv_log(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

#if defined(__has_include)
  #if __has_include("cJSON.h")
    #include "cJSON.h"
  #elif __has_include(<cjson/cJSON.h>)
    #include <cjson/cJSON.h>
  #elif __has_include(<cJSON.h>)
    #include <cJSON.h>
  #else
    #include "cJSON.h"
  #endif
#else
  #include "cJSON.h"
#endif

void format_duration(int seconds, char *buffer, size_t max_len) {
    if (!buffer || max_len == 0) return;
    if (seconds < 0) seconds = 0;
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0) {
        snprintf(buffer, max_len, "%d:%02d:%02d", h, m, s);
    } else {
        snprintf(buffer, max_len, "%02d:%02d", m, s);
    }
}

static void parse_video_object(cJSON *item, VideoItem *v) {
    memset(v, 0, sizeof(VideoItem));

    cJSON *vid = cJSON_GetObjectItemCaseSensitive(item, "videoId");
    if (cJSON_IsString(vid) && vid->valuestring) {
        snprintf(v->id, sizeof(v->id), "%s", vid->valuestring);
    }

    cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
    if (cJSON_IsString(title) && title->valuestring) {
        snprintf(v->title, sizeof(v->title), "%s", title->valuestring);
    } else {
        snprintf(v->title, sizeof(v->title), "Untitled Video");
    }

    cJSON *author = cJSON_GetObjectItemCaseSensitive(item, "author");
    if (cJSON_IsString(author) && author->valuestring) {
        snprintf(v->author, sizeof(v->author), "%s", author->valuestring);
    }

    cJSON *author_id = cJSON_GetObjectItemCaseSensitive(item, "authorId");
    if (cJSON_IsString(author_id) && author_id->valuestring) {
        snprintf(v->author_id, sizeof(v->author_id), "%s", author_id->valuestring);
    }

    cJSON *author_url = cJSON_GetObjectItemCaseSensitive(item, "authorUrl");
    if (cJSON_IsString(author_url) && author_url->valuestring) {
        snprintf(v->author_url, sizeof(v->author_url), "%s", author_url->valuestring);
    }

    cJSON *desc = cJSON_GetObjectItemCaseSensitive(item, "description");
    if (cJSON_IsString(desc) && desc->valuestring && desc->valuestring[0] != '\0') {
        strncpy(v->description, desc->valuestring, sizeof(v->description) - 1);
        v->description[sizeof(v->description) - 1] = '\0';
    } else {
        snprintf(v->description, sizeof(v->description), "No description provided for this video.");
    }

    cJSON *length = cJSON_GetObjectItemCaseSensitive(item, "lengthSeconds");
    if (cJSON_IsNumber(length)) {
        v->length_seconds = length->valueint;
    }

    cJSON *views = cJSON_GetObjectItemCaseSensitive(item, "viewCount");
    if (cJSON_IsNumber(views)) {
        v->view_count = (long long)views->valuedouble;
    }

    cJSON *pub = cJSON_GetObjectItemCaseSensitive(item, "publishedText");
    if (cJSON_IsString(pub) && pub->valuestring) {
        snprintf(v->published_text, sizeof(v->published_text), "%s", pub->valuestring);
    }

    cJSON *thumbs = cJSON_GetObjectItemCaseSensitive(item, "videoThumbnails");
    if (cJSON_IsArray(thumbs) && cJSON_GetArraySize(thumbs) > 0) {
        cJSON *first_thumb = cJSON_GetArrayItem(thumbs, 0);
        cJSON *thumb_url = cJSON_GetObjectItemCaseSensitive(first_thumb, "url");
        if (cJSON_IsString(thumb_url) && thumb_url->valuestring) {
            snprintf(v->thumbnail_url, sizeof(v->thumbnail_url), "%s", thumb_url->valuestring);
        }
    }
}

int invidious_fetch_trending(const char *instance, VideoList *out_list) {
    if (!out_list) return -1;
    memset(out_list, 0, sizeof(VideoList));

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    snprintf(out_list->instance, sizeof(out_list->instance), "%s", base_url);

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/trending", base_url);
    inv_log("[Invidious] Requesting trending feed: %s", endpoint);

    HttpResponse resp;
    int net_status = http_get(endpoint, &resp);

    if (net_status != 0) {
        inv_log("[Invidious] Network request failed! Status: %d, Error: %s", net_status, resp.error_msg);
        http_response_free(&resp);
        return net_status;
    }

    if (!resp.data || resp.size == 0) {
        inv_log("[Invidious] Received empty response payload.");
        http_response_free(&resp);
        return -4;
    }

    cJSON *root = cJSON_Parse(resp.data);
    http_response_free(&resp);

    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        char err_snippet[48];
        if (error_ptr) {
            strncpy(err_snippet, error_ptr, sizeof(err_snippet) - 1);
            err_snippet[sizeof(err_snippet) - 1] = '\0';
        } else {
            snprintf(err_snippet, sizeof(err_snippet), "unknown");
        }
        inv_log("[Invidious] JSON parsing failed! Error near: %s", err_snippet);
        return -5;
    }

    if (!cJSON_IsArray(root)) {
        cJSON *err_item = cJSON_GetObjectItemCaseSensitive(root, "error");
        if (cJSON_IsString(err_item) && err_item->valuestring) {
            inv_log("[Invidious] Instance returned API error: %s", err_item->valuestring);
        }
        cJSON_Delete(root);
        return -6;
    }

    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (count >= MAX_VIDEOS) break;

        cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        if (type && cJSON_IsString(type) && strcmp(type->valuestring, "video") != 0) {
            continue;
        }

        parse_video_object(item, &out_list->items[count]);
        count++;
    }

    cJSON_Delete(root);
    out_list->count = count;

    inv_log("[Invidious] Successfully parsed %d trending videos.", count);
    return count;
}

static void url_encode_query(const char *src, char *dst, size_t max_len) {
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;
    if (!src || !dst || max_len == 0) return;

    while (*src && pos + 4 < max_len) {
        unsigned char c = (unsigned char)*src++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[pos++] = (char)c;
        } else if (c == ' ') {
            dst[pos++] = '+';
        } else {
            dst[pos++] = '%';
            dst[pos++] = hex[(c >> 4) & 0x0F];
            dst[pos++] = hex[c & 0x0F];
        }
    }
    dst[pos] = '\0';
}

int invidious_search(const char *instance, const char *query, VideoList *out_list) {
    if (!query || !out_list) return -1;
    memset(out_list, 0, sizeof(VideoList));

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    snprintf(out_list->instance, sizeof(out_list->instance), "%s", base_url);

    char encoded_q[256];
    url_encode_query(query, encoded_q, sizeof(encoded_q));

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/search?q=%s&type=video", base_url, encoded_q);
    inv_log("[Invidious] Searching: %s", endpoint);

    HttpResponse resp;
    int net_status = http_get(endpoint, &resp);
    if (net_status != 0) {
        inv_log("[Invidious] Search failed: %d (%s)", net_status, resp.error_msg);
        http_response_free(&resp);
        return net_status;
    }

    if (!resp.data || resp.size == 0) {
        http_response_free(&resp);
        return -4;
    }

    cJSON *root = cJSON_Parse(resp.data);
    http_response_free(&resp);
    if (!root) return -5;

    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return -6;
    }

    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (count >= MAX_VIDEOS) break;
        cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        if (type && cJSON_IsString(type) && strcmp(type->valuestring, "video") != 0) {
            continue;
        }
        parse_video_object(item, &out_list->items[count]);
        count++;
    }

    cJSON_Delete(root);
    out_list->count = count;
    inv_log("[Invidious] Search parsed %d results.", count);
    return count;
}

int invidious_fetch_channel_videos(const char *instance, const char *channel_id, VideoList *out_list) {
    if (!channel_id || !out_list) return -1;
    memset(out_list, 0, sizeof(VideoList));

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    snprintf(out_list->instance, sizeof(out_list->instance), "%s", base_url);

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/channels/%s/videos", base_url, channel_id);
    inv_log("[Invidious] Requesting channel videos: %s", endpoint);

    HttpResponse resp;
    int net_status = http_get(endpoint, &resp);
    if (net_status != 0) {
        inv_log("[Invidious] Channel fetch failed: %d", net_status);
        http_response_free(&resp);
        return net_status;
    }

    cJSON *root = cJSON_Parse(resp.data);
    http_response_free(&resp);
    if (!root) return -5;

    cJSON *videos_arr = root;
    if (cJSON_IsObject(root)) {
        videos_arr = cJSON_GetObjectItemCaseSensitive(root, "videos");
    }

    if (!videos_arr || !cJSON_IsArray(videos_arr)) {
        cJSON_Delete(root);
        return 0;
    }

    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, videos_arr) {
        if (count >= MAX_VIDEOS) break;
        parse_video_object(item, &out_list->items[count]);
        count++;
    }

    cJSON_Delete(root);
    out_list->count = count;
    inv_log("[Invidious] Loaded %d videos from channel.", count);
    return count;
}
