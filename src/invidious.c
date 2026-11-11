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
    if (seconds <= 0) {
        snprintf(buffer, max_len, "LIVE");
        return;
    }
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0) {
        snprintf(buffer, max_len, "%d:%02d:%02d", h, m, s);
    } else {
        snprintf(buffer, max_len, "%02d:%02d", m, s);
    }
}

void sanitize_utf8_text(const char *src, char *dst, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) { dst[0] = '\0'; return; }

    size_t out = 0;
    const unsigned char *p = (const unsigned char *)src;

    while (*p && out + 1 < max_len) {
        unsigned char b = *p;
        int len = 0;

        if (b < 0x80) {
            len = 1;
        } else if ((b & 0xE0) == 0xC0) {
            if ((p[1] & 0xC0) == 0x80) len = 2; else { p++; continue; }
        } else if ((b & 0xF0) == 0xE0) {
            if ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) len = 3; else { p++; continue; }
        } else if ((b & 0xF8) == 0xF0) {
            if ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) len = 4; else { p++; continue; }
        } else {
            p++;
            continue;
        }

        if (b < 0x80) {
            if (b == '\r') { p += len; continue; }
            if (b == '\t') b = ' ';
            if (out == 0 && b == ' ') { p += len; continue; }
            dst[out++] = (char)b;
        } else {
            if (out + len < max_len) {
                for (int i = 0; i < len; i++) {
                    dst[out++] = (char)p[i];
                }
            }
        }
        p += len;
    }

    /* Trim trailing whitespace */
    while (out > 0 && (dst[out - 1] == ' ' || dst[out - 1] == '\t' || dst[out - 1] == '\n' || dst[out - 1] == '\r')) {
        out--;
    }
    dst[out] = '\0';
}

static int is_valid_id_char(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_';
}

int invidious_extract_video_id(const char *input, char *out_id, size_t max_len) {
    if (!input || !out_id || max_len < 12) return 0;
    out_id[0] = '\0';

    while (*input == ' ' || *input == '\t') input++;
    size_t len = strlen(input);
    while (len > 0 && (input[len - 1] == ' ' || input[len - 1] == '\t' || input[len - 1] == '\n' || input[len - 1] == '\r')) {
        len--;
    }
    if (len == 0) return 0;

    const char *candidate = NULL;
    const char *v_param = strstr(input, "v=");
    if (v_param) {
        candidate = v_param + 2;
    } else {
        const char *prefixes[] = {"youtu.be/", "/shorts/", "/embed/", "/live/", "/v/"};
        for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
            const char *found = strstr(input, prefixes[i]);
            if (found) {
                candidate = found + strlen(prefixes[i]);
                break;
            }
        }
    }

    if (candidate) {
        for (int i = 0; i < 11; i++) {
            if (!is_valid_id_char(candidate[i])) return 0;
        }
        memcpy(out_id, candidate, 11);
        out_id[11] = '\0';
        return 1;
    }

    if (len == 11) {
        for (int i = 0; i < 11; i++) {
            if (!is_valid_id_char(input[i])) return 0;
        }
        memcpy(out_id, input, 11);
        out_id[11] = '\0';
        return 1;
    }
    return 0;
}

static void parse_invidious_video_object(cJSON *item, VideoItem *v) {
    memset(v, 0, sizeof(VideoItem));
    v->is_channel = 0;

    cJSON *vid = cJSON_GetObjectItemCaseSensitive(item, "videoId");
    if (cJSON_IsString(vid) && vid->valuestring) {
        snprintf(v->id, sizeof(v->id), "%s", vid->valuestring);
    }

    cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
    if (cJSON_IsString(title) && title->valuestring) {
        sanitize_utf8_text(title->valuestring, v->title, sizeof(v->title));
    }

    cJSON *author = cJSON_GetObjectItemCaseSensitive(item, "author");
    if (cJSON_IsString(author) && author->valuestring) {
        sanitize_utf8_text(author->valuestring, v->author, sizeof(v->author));
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
    if (!desc) desc = cJSON_GetObjectItemCaseSensitive(item, "descriptionShort");
    if (cJSON_IsString(desc) && desc->valuestring && desc->valuestring[0] != '\0') {
        sanitize_utf8_text(desc->valuestring, v->description, sizeof(v->description));
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

    if (v->id[0] != '\0') {
        snprintf(v->thumbnail_url, sizeof(v->thumbnail_url),
                 "https://i.ytimg.com/vi/%s/default.jpg", v->id);
    }
}

static void parse_invidious_channel_object(cJSON *item, VideoItem *v) {
    memset(v, 0, sizeof(VideoItem));
    v->is_channel = 1;

    cJSON *author_id = cJSON_GetObjectItemCaseSensitive(item, "authorId");
    if (!author_id) author_id = cJSON_GetObjectItemCaseSensitive(item, "channelId");
    if (cJSON_IsString(author_id) && author_id->valuestring) {
        snprintf(v->id, sizeof(v->id), "%s", author_id->valuestring);
        snprintf(v->author_id, sizeof(v->author_id), "%s", author_id->valuestring);
    }

    cJSON *author = cJSON_GetObjectItemCaseSensitive(item, "author");
    if (!author) author = cJSON_GetObjectItemCaseSensitive(item, "title");
    if (cJSON_IsString(author) && author->valuestring) {
        sanitize_utf8_text(author->valuestring, v->title, sizeof(v->title));
        strncpy(v->author, v->title, sizeof(v->author) - 1);
        v->author[sizeof(v->author) - 1] = '\0';
    }

    cJSON *subs = cJSON_GetObjectItemCaseSensitive(item, "subCount");
    if (cJSON_IsNumber(subs)) v->sub_count = (long long)subs->valuedouble;

    cJSON *vc = cJSON_GetObjectItemCaseSensitive(item, "videoCount");
    if (cJSON_IsNumber(vc)) v->video_count = vc->valueint;

    cJSON *desc = cJSON_GetObjectItemCaseSensitive(item, "description");
    if (!desc) desc = cJSON_GetObjectItemCaseSensitive(item, "descriptionShort");
    if (cJSON_IsString(desc) && desc->valuestring) {
        sanitize_utf8_text(desc->valuestring, v->description, sizeof(v->description));
    }

    cJSON *thumbs = cJSON_GetObjectItemCaseSensitive(item, "authorThumbnails");
    if (cJSON_IsArray(thumbs) && cJSON_GetArraySize(thumbs) > 0) {
        cJSON *last_thumb = cJSON_GetArrayItem(thumbs, cJSON_GetArraySize(thumbs) - 1);
        cJSON *url = cJSON_GetObjectItemCaseSensitive(last_thumb, "url");
        if (cJSON_IsString(url) && url->valuestring) {
            if (strncmp(url->valuestring, "//", 2) == 0) {
                snprintf(v->thumbnail_url, sizeof(v->thumbnail_url), "https:%s", url->valuestring);
            } else if (strncmp(url->valuestring, "http", 4) == 0) {
                snprintf(v->thumbnail_url, sizeof(v->thumbnail_url), "%s", url->valuestring);
            } else {
                snprintf(v->thumbnail_url, sizeof(v->thumbnail_url), "https://yt.chocolatemoo53.com%s", url->valuestring);
            }
        }
    }
    if (v->thumbnail_url[0] == '\0' && v->id[0] != '\0') {
        snprintf(v->thumbnail_url, sizeof(v->thumbnail_url), "https://i.ytimg.com/vi/%s/default.jpg", v->id);
    }
}

static int list_contains_id(const VideoList *list, const char *video_id) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].id, video_id) == 0) return 1;
    }
    return 0;
}

static int fetch_invidious_array_endpoint(const char *endpoint, VideoList *out_list) {
    inv_log("[Invidious] Requesting feed: %s", endpoint);

    HttpResponse resp;
    int net_status = http_get(endpoint, &resp);
    if (net_status != 0) {
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

    int added = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (out_list->count >= MAX_VIDEOS) break;

        cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        if (type && cJSON_IsString(type) && strcmp(type->valuestring, "channel") == 0) {
            VideoItem parsed;
            parse_invidious_channel_object(item, &parsed);
            if (parsed.id[0] != '\0' && !list_contains_id(out_list, parsed.id)) {
                out_list->items[out_list->count++] = parsed;
                added++;
            }
        } else {
            VideoItem parsed;
            parse_invidious_video_object(item, &parsed);
            if (parsed.id[0] != '\0' && !list_contains_id(out_list, parsed.id)) {
                out_list->items[out_list->count++] = parsed;
                added++;
            }
        }
    }

    cJSON_Delete(root);
    return added;
}

int invidious_fetch_trending(const char *instance, VideoList *out_list) {
    if (!out_list) return -1;
    memset(out_list, 0, sizeof(VideoList));

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    snprintf(out_list->instance, sizeof(out_list->instance), "%s", base_url);

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/popular", base_url);
    int r1 = fetch_invidious_array_endpoint(endpoint, out_list);

    if (out_list->count < MAX_VIDEOS) {
        snprintf(endpoint, sizeof(endpoint), "%s/api/v1/popular", base_url);
        int r2 = fetch_invidious_array_endpoint(endpoint, out_list);
        if (out_list->count == 0 && r1 < 0) return (r2 < 0) ? r1 : out_list->count;
    }

    inv_log("[Invidious] Loaded %d popular videos.", out_list->count);
    return out_list->count;
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

static void parse_piped_stream_item(cJSON *item, VideoItem *v) {
    memset(v, 0, sizeof(VideoItem));

    cJSON *url = cJSON_GetObjectItemCaseSensitive(item, "url");
    if (cJSON_IsString(url) && url->valuestring) {
        invidious_extract_video_id(url->valuestring, v->id, sizeof(v->id));
    }

    cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
    if (cJSON_IsString(title) && title->valuestring) {
        sanitize_utf8_text(title->valuestring, v->title, sizeof(v->title));
    }
    if (v->title[0] == '\0') {
        snprintf(v->title, sizeof(v->title), "Untitled Video");
    }

    cJSON *uploader = cJSON_GetObjectItemCaseSensitive(item, "uploaderName");
    if (cJSON_IsString(uploader) && uploader->valuestring) {
        sanitize_utf8_text(uploader->valuestring, v->author, sizeof(v->author));
    }

    cJSON *uploader_url = cJSON_GetObjectItemCaseSensitive(item, "uploaderUrl");
    if (cJSON_IsString(uploader_url) && uploader_url->valuestring) {
        snprintf(v->author_url, sizeof(v->author_url), "%s", uploader_url->valuestring);
        const char *ch = strstr(uploader_url->valuestring, "/channel/");
        if (ch) {
            snprintf(v->author_id, sizeof(v->author_id), "%s", ch + 9);
        }
    }

    cJSON *desc = cJSON_GetObjectItemCaseSensitive(item, "shortDescription");
    if (cJSON_IsString(desc) && desc->valuestring && desc->valuestring[0] != '\0') {
        sanitize_utf8_text(desc->valuestring, v->description, sizeof(v->description));
    } else {
        snprintf(v->description, sizeof(v->description), "Select Description during playback to view full notes.");
    }

    cJSON *dur = cJSON_GetObjectItemCaseSensitive(item, "duration");
    if (cJSON_IsNumber(dur)) {
        v->length_seconds = dur->valueint;
    }

    cJSON *views = cJSON_GetObjectItemCaseSensitive(item, "views");
    if (cJSON_IsNumber(views)) {
        v->view_count = (long long)views->valuedouble;
    }

    cJSON *pub = cJSON_GetObjectItemCaseSensitive(item, "uploadedDate");
    if (cJSON_IsString(pub) && pub->valuestring) {
        snprintf(v->published_text, sizeof(v->published_text), "%s", pub->valuestring);
    }

    if (v->id[0] != '\0') {
        snprintf(v->thumbnail_url, sizeof(v->thumbnail_url),
                 "https://i.ytimg.com/vi/%s/default.jpg", v->id);
    }
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
    int inv_count = fetch_invidious_array_endpoint(endpoint, out_list);
    if (inv_count > 0) {
        inv_log("[Invidious Search] Loaded %d videos from %s.", inv_count, base_url);
        return inv_count;
    }

    /* Fallback search query on Invidious without type constraint */
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/search?q=%s", base_url, encoded_q);
    inv_count = fetch_invidious_array_endpoint(endpoint, out_list);
    if (inv_count > 0) {
        inv_log("[Invidious Search] Fallback returned %d videos.", inv_count);
        return inv_count;
    }

    inv_log("[Invidious Search] No video results found for query: %s", query);
    return out_list->count;
}

int invidious_search_channels(const char *instance, const char *query, VideoList *out_list) {
    if (!query || !out_list) return -1;
    memset(out_list, 0, sizeof(VideoList));

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    snprintf(out_list->instance, sizeof(out_list->instance), "%s", base_url);

    char encoded_q[256];
    url_encode_query(query, encoded_q, sizeof(encoded_q));

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/search?q=%s&type=channel", base_url, encoded_q);
    int inv_count = fetch_invidious_array_endpoint(endpoint, out_list);
    if (inv_count > 0) {
        inv_log("[Invidious Channel Search] Loaded %d channels from %s.", inv_count, base_url);
        return inv_count;
    }

    inv_log("[Invidious Channel Search] No channels found for query: %s", query);
    return out_list->count;
}

int invidious_fetch_video(const char *instance, const char *video_id, VideoItem *out_item,
                          char *out_stream_url, size_t max_stream_len) {
    if (!video_id || !out_item) return -1;
    if (out_stream_url && max_stream_len > 0) out_stream_url[0] = '\0';

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    char endpoint[512];

    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/videos/%s", base_url, video_id);
    inv_log("[Invidious Direct Stream] Fetching stream metadata: %s", endpoint);

    HttpResponse resp;
    if (http_get(endpoint, &resp) == 0 && resp.data && resp.size > 0) {
        cJSON *root = cJSON_Parse(resp.data);
        http_response_free(&resp);
        if (root && cJSON_IsObject(root)) {
            parse_invidious_video_object(root, out_item);

            if (out_stream_url && max_stream_len > 0) {
                /* 1. Extract formatStreams from Invidious (combined mp4 video+audio direct HTTPS streams) */
                cJSON *fstreams = cJSON_GetObjectItemCaseSensitive(root, "formatStreams");
                if (cJSON_IsArray(fstreams)) {
                    cJSON *fs = NULL;
                    cJSON_ArrayForEach(fs, fstreams) {
                        cJSON *cont = cJSON_GetObjectItemCaseSensitive(fs, "container");
                        cJSON *url = cJSON_GetObjectItemCaseSensitive(fs, "url");
                        if (cJSON_IsString(url) && url->valuestring && url->valuestring[0] != '\0') {
                            if (!cont || !cJSON_IsString(cont) || strcmp(cont->valuestring, "mp4") == 0) {
                                if (strncmp(url->valuestring, "//", 2) == 0) {
                                    snprintf(out_stream_url, max_stream_len, "https:%s", url->valuestring);
                                } else if (strncmp(url->valuestring, "/", 1) == 0) {
                                    snprintf(out_stream_url, max_stream_len, "%s%s", base_url, url->valuestring);
                                } else {
                                    snprintf(out_stream_url, max_stream_len, "%s", url->valuestring);
                                }
                                cJSON *q = cJSON_GetObjectItemCaseSensitive(fs, "qualityLabel");
                                if (cJSON_IsString(q) && (strncmp(q->valuestring, "360p", 4) == 0 || strncmp(q->valuestring, "720p", 4) == 0)) {
                                    break;
                                }
                            }
                        }
                    }
                }

                /* 2. Fallback to adaptiveFormats from Invidious if formatStreams is empty */
                if (out_stream_url[0] == '\0') {
                    cJSON *astreams = cJSON_GetObjectItemCaseSensitive(root, "adaptiveFormats");
                    if (cJSON_IsArray(astreams)) {
                        cJSON *as = NULL;
                        cJSON_ArrayForEach(as, astreams) {
                            cJSON *cont = cJSON_GetObjectItemCaseSensitive(as, "container");
                            cJSON *type = cJSON_GetObjectItemCaseSensitive(as, "type");
                            cJSON *url = cJSON_GetObjectItemCaseSensitive(as, "url");
                            if (cJSON_IsString(url) && url->valuestring && url->valuestring[0] != '\0') {
                                if ((cont && cJSON_IsString(cont) && strcmp(cont->valuestring, "mp4") == 0) ||
                                    (type && cJSON_IsString(type) && strstr(type->valuestring, "mp4") != NULL)) {
                                    if (strncmp(url->valuestring, "//", 2) == 0) {
                                        snprintf(out_stream_url, max_stream_len, "https:%s", url->valuestring);
                                    } else if (strncmp(url->valuestring, "/", 1) == 0) {
                                        snprintf(out_stream_url, max_stream_len, "%s%s", base_url, url->valuestring);
                                    } else {
                                        snprintf(out_stream_url, max_stream_len, "%s", url->valuestring);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }

                /* 3. Direct Invidious Proxy Stream URL (itag 18 / 22) */
                if (out_stream_url[0] == '\0') {
                    snprintf(out_stream_url, max_stream_len, "%s/latest_version?id=%s&itag=18", base_url, video_id);
                }
            }

            cJSON_Delete(root);
            if (out_stream_url && out_stream_url[0] != '\0') {
                inv_log("[Invidious Direct Stream] Direct HTTPS Stream URL: %s", out_stream_url);
                return 0;
            }
        } else if (root) {
            cJSON_Delete(root);
        }
    } else {
        http_response_free(&resp);
    }

    /* Fallback: Direct YouTube oEmbed metadata with Invidious direct proxy stream */
    snprintf(endpoint, sizeof(endpoint),
             "https://www.youtube.com/oembed?url=https://www.youtube.com/watch?v=%s&format=json",
             video_id);
    if (http_get(endpoint, &resp) == 0 && resp.data && resp.size > 0) {
        cJSON *root = cJSON_Parse(resp.data);
        http_response_free(&resp);
        if (root && cJSON_IsObject(root)) {
            snprintf(out_item->id, sizeof(out_item->id), "%s", video_id);
            cJSON *title = cJSON_GetObjectItemCaseSensitive(root, "title");
            if (cJSON_IsString(title) && title->valuestring) {
                sanitize_utf8_text(title->valuestring, out_item->title, sizeof(out_item->title));
            }
            cJSON *author = cJSON_GetObjectItemCaseSensitive(root, "author_name");
            if (cJSON_IsString(author) && author->valuestring) {
                sanitize_utf8_text(author->valuestring, out_item->author, sizeof(out_item->author));
            }
            snprintf(out_item->thumbnail_url, sizeof(out_item->thumbnail_url),
                     "https://i.ytimg.com/vi/%s/default.jpg", video_id);
            if (out_stream_url && max_stream_len > 0) {
                snprintf(out_stream_url, max_stream_len, "%s/latest_version?id=%s&itag=18", base_url, video_id);
            }
            cJSON_Delete(root);
            return 0;
        } else if (root) {
            cJSON_Delete(root);
        }
    } else {
        http_response_free(&resp);
    }

    return -1;
}

int invidious_fetch_channel_info(const char *instance, const char *channel_id, ChannelInfo *out_info) {
    if (!channel_id || !out_info) return -1;
    memset(out_info, 0, sizeof(ChannelInfo));

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/channels/%s", base_url, channel_id);
    inv_log("[Invidious Channel] Fetching metadata: %s", endpoint);

    HttpResponse resp;
    if (http_get(endpoint, &resp) == 0 && resp.data && resp.size > 0) {
        cJSON *root = cJSON_Parse(resp.data);
        http_response_free(&resp);
        if (root && cJSON_IsObject(root)) {
            snprintf(out_info->author_id, sizeof(out_info->author_id), "%s", channel_id);

            cJSON *author = cJSON_GetObjectItemCaseSensitive(root, "author");
            if (cJSON_IsString(author) && author->valuestring) {
                sanitize_utf8_text(author->valuestring, out_info->author, sizeof(out_info->author));
            }

            cJSON *desc = cJSON_GetObjectItemCaseSensitive(root, "description");
            if (cJSON_IsString(desc) && desc->valuestring) {
                sanitize_utf8_text(desc->valuestring, out_info->description, sizeof(out_info->description));
            }

            cJSON *subs = cJSON_GetObjectItemCaseSensitive(root, "subCount");
            if (cJSON_IsNumber(subs)) out_info->sub_count = (long long)subs->valuedouble;

            cJSON *banners = cJSON_GetObjectItemCaseSensitive(root, "authorBanners");
            if (cJSON_IsArray(banners) && cJSON_GetArraySize(banners) > 0) {
                cJSON *b0 = cJSON_GetArrayItem(banners, cJSON_GetArraySize(banners) - 1);
                cJSON *burl = cJSON_GetObjectItemCaseSensitive(b0, "url");
                if (cJSON_IsString(burl) && burl->valuestring) {
                    if (strncmp(burl->valuestring, "//", 2) == 0) {
                        snprintf(out_info->banner_url, sizeof(out_info->banner_url), "https:%s", burl->valuestring);
                    } else if (strncmp(burl->valuestring, "http", 4) == 0) {
                        snprintf(out_info->banner_url, sizeof(out_info->banner_url), "%s", burl->valuestring);
                    } else if (strncmp(burl->valuestring, "/", 1) == 0) {
                        snprintf(out_info->banner_url, sizeof(out_info->banner_url), "%s%s", base_url, burl->valuestring);
                    } else {
                        snprintf(out_info->banner_url, sizeof(out_info->banner_url), "%s", burl->valuestring);
                    }
                }
            }

            cJSON *thumbs = cJSON_GetObjectItemCaseSensitive(root, "authorThumbnails");
            if (cJSON_IsArray(thumbs) && cJSON_GetArraySize(thumbs) > 0) {
                cJSON *t0 = cJSON_GetArrayItem(thumbs, cJSON_GetArraySize(thumbs) - 1);
                cJSON *turl = cJSON_GetObjectItemCaseSensitive(t0, "url");
                if (cJSON_IsString(turl) && turl->valuestring) {
                    if (strncmp(turl->valuestring, "//", 2) == 0) {
                        snprintf(out_info->avatar_url, sizeof(out_info->avatar_url), "https:%s", turl->valuestring);
                    } else if (strncmp(turl->valuestring, "http", 4) == 0) {
                        snprintf(out_info->avatar_url, sizeof(out_info->avatar_url), "%s", turl->valuestring);
                    } else if (strncmp(turl->valuestring, "/", 1) == 0) {
                        snprintf(out_info->avatar_url, sizeof(out_info->avatar_url), "%s%s", base_url, turl->valuestring);
                    } else {
                        snprintf(out_info->avatar_url, sizeof(out_info->avatar_url), "%s", turl->valuestring);
                    }
                }
            }
            if (out_info->avatar_url[0] == '\0' && channel_id[0] != '\0') {
                snprintf(out_info->avatar_url, sizeof(out_info->avatar_url), "https://i.ytimg.com/vi/%s/default.jpg", channel_id);
            }

            cJSON_Delete(root);
            return 0;
        } else if (root) {
            cJSON_Delete(root);
        }
    } else {
        http_response_free(&resp);
    }
    return -1;
}

int invidious_fetch_channel_videos(const char *instance, const char *channel_id, const char *tab, VideoList *out_list) {
    if (!channel_id || !out_list) return -1;
    memset(out_list, 0, sizeof(VideoList));

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    snprintf(out_list->instance, sizeof(out_list->instance), "%s", base_url);

    char endpoint[512];
    if (tab && strcmp(tab, "Playlists") == 0) {
        snprintf(endpoint, sizeof(endpoint), "%s/api/v1/channels/%s/playlists", base_url, channel_id);
    } else {
        snprintf(endpoint, sizeof(endpoint), "%s/api/v1/channels/%s/videos", base_url, channel_id);
    }
    inv_log("[Invidious] Requesting channel videos: %s", endpoint);

    HttpResponse resp;
    int net_status = http_get(endpoint, &resp);
    if (net_status != 0) {
        http_response_free(&resp);
        return net_status;
    }

    cJSON *root = cJSON_Parse(resp.data);
    http_response_free(&resp);
    if (!root) return -5;

    cJSON *videos_arr = root;
    if (cJSON_IsObject(root)) {
        videos_arr = cJSON_GetObjectItemCaseSensitive(root, "videos");
        if (!videos_arr) videos_arr = cJSON_GetObjectItemCaseSensitive(root, "playlists");
    }

    if (!videos_arr || !cJSON_IsArray(videos_arr)) {
        cJSON_Delete(root);
        return 0;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, videos_arr) {
        if (out_list->count >= MAX_VIDEOS) break;
        VideoItem parsed;
        parse_invidious_video_object(item, &parsed);
        if (parsed.id[0] != '\0') {
            out_list->items[out_list->count++] = parsed;
        }
    }

    cJSON_Delete(root);
    return out_list->count;
}
