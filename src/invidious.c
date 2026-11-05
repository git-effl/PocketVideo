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
        if ((b & 0xF8) == 0xF0) {
            /* Strip 4-byte UTF-8 emojis (outside BMP, unsupported in NotoSans-Regular) */
            p++;
            for (int k = 0; k < 3 && (*p & 0xC0) == 0x80; k++) p++;
            continue;
        } else if ((b & 0xF0) == 0xE0) {
            unsigned char b1 = p[1], b2 = p[2];
            if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                /* Skip invisible variation selectors (U+FE0E/FE0F) & ZWJ (U+200D) */
                if ((b == 0xEF && b1 == 0xB8 && (b2 == 0x8E || b2 == 0x8F)) ||
                    (b == 0xE2 && b1 == 0x80 && b2 == 0x8D)) {
                    p += 3;
                    continue;
                }
                if (out + 3 < max_len) {
                    dst[out++] = (char)b;
                    dst[out++] = (char)b1;
                    dst[out++] = (char)b2;
                    p += 3;
                    continue;
                }
                break;
            }
            p++;
        } else if ((b & 0xE0) == 0xC0) {
            unsigned char b1 = p[1];
            if ((b1 & 0xC0) == 0x80) {
                if (out + 2 < max_len) {
                    dst[out++] = (char)b;
                    dst[out++] = (char)b1;
                    p += 2;
                    continue;
                }
                break;
            }
            p++;
        } else if (b < 0x80) {
            if (b == '\r') {
                p++;
                continue;
            }
            if (b == '\t') b = ' ';
            if (out == 0 && b == ' ') {
                p++;
                continue;
            }
            dst[out++] = (char)b;
            p++;
        } else {
            p++;
        }
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

    cJSON *vid = cJSON_GetObjectItemCaseSensitive(item, "videoId");
    if (cJSON_IsString(vid) && vid->valuestring) {
        snprintf(v->id, sizeof(v->id), "%s", vid->valuestring);
    }

    cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
    if (cJSON_IsString(title) && title->valuestring) {
        sanitize_utf8_text(title->valuestring, v->title, sizeof(v->title));
    }
    if (v->title[0] == '\0') {
        snprintf(v->title, sizeof(v->title), "Untitled Video");
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
    if (cJSON_IsString(desc) && desc->valuestring && desc->valuestring[0] != '\0') {
        sanitize_utf8_text(desc->valuestring, v->description, sizeof(v->description));
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

    if (v->id[0] != '\0') {
        snprintf(v->thumbnail_url, sizeof(v->thumbnail_url),
                 "https://i.ytimg.com/vi/%s/default.jpg", v->id);
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
        if (type && cJSON_IsString(type) &&
            strcmp(type->valuestring, "video") != 0 &&
            strcmp(type->valuestring, "shortVideo") != 0) {
            continue;
        }

        VideoItem parsed;
        parse_invidious_video_object(item, &parsed);
        if (parsed.id[0] != '\0' && !list_contains_id(out_list, parsed.id)) {
            out_list->items[out_list->count++] = parsed;
            added++;
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
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/trending", base_url);
    int r1 = fetch_invidious_array_endpoint(endpoint, out_list);

    if (out_list->count < MAX_VIDEOS) {
        snprintf(endpoint, sizeof(endpoint), "%s/api/v1/popular", base_url);
        int r2 = fetch_invidious_array_endpoint(endpoint, out_list);
        if (out_list->count == 0 && r1 < 0) return (r2 < 0) ? r1 : out_list->count;
    }

    inv_log("[Invidious] Loaded %d trending/popular videos.", out_list->count);
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

    /* 1. Try primary Invidious search endpoint */
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/search?q=%s&type=video", base_url, encoded_q);
    int inv_count = fetch_invidious_array_endpoint(endpoint, out_list);
    if (inv_count > 0) {
        inv_log("[Invidious] Search returned %d videos from primary instance.", inv_count);
        return inv_count;
    }

    /* 2. Fallback to Piped search API if primary Invidious instance restricts /api/v1/search */
    snprintf(endpoint, sizeof(endpoint), "%s/search?q=%s&filter=videos", PIPED_FALLBACK_HOST, encoded_q);
    inv_log("[Search] Querying Piped fallback: %s", endpoint);

    HttpResponse resp;
    int net_status = http_get(endpoint, &resp);
    if (net_status != 0 || !resp.data || resp.size == 0) {
        http_response_free(&resp);
        return (net_status != 0) ? net_status : -4;
    }

    cJSON *root = cJSON_Parse(resp.data);
    http_response_free(&resp);
    if (!root) return -5;

    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return -6;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (out_list->count >= MAX_VIDEOS) break;
        cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        if (type && cJSON_IsString(type) && strcmp(type->valuestring, "stream") != 0) {
            continue;
        }
        VideoItem parsed;
        parse_piped_stream_item(item, &parsed);
        if (parsed.id[0] != '\0') {
            out_list->items[out_list->count++] = parsed;
        }
    }

    cJSON_Delete(root);
    inv_log("[Search] Found %d videos.", out_list->count);
    return out_list->count;
}

int invidious_fetch_video(const char *instance, const char *video_id, VideoItem *out_item,
                          char *out_stream_url, size_t max_stream_len) {
    if (!video_id || !out_item) return -1;
    if (out_stream_url && max_stream_len > 0) out_stream_url[0] = '\0';

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    char endpoint[512];

    /* 1. Try primary Invidious /api/v1/videos/<id> */
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/videos/%s", base_url, video_id);
    HttpResponse resp;
    if (http_get(endpoint, &resp) == 0 && resp.data && resp.size > 0) {
        cJSON *root = cJSON_Parse(resp.data);
        http_response_free(&resp);
        if (root && cJSON_IsObject(root)) {
            parse_invidious_video_object(root, out_item);
            if (out_stream_url && max_stream_len > 0) {
                cJSON *fstreams = cJSON_GetObjectItemCaseSensitive(root, "formatStreams");
                cJSON *fs = NULL;
                cJSON_ArrayForEach(fs, fstreams) {
                    cJSON *cont = cJSON_GetObjectItemCaseSensitive(fs, "container");
                    cJSON *url = cJSON_GetObjectItemCaseSensitive(fs, "url");
                    if (cJSON_IsString(cont) && strcmp(cont->valuestring, "mp4") == 0 &&
                        cJSON_IsString(url) && url->valuestring) {
                        snprintf(out_stream_url, max_stream_len, "%s", url->valuestring);
                        cJSON *q = cJSON_GetObjectItemCaseSensitive(fs, "qualityLabel");
                        if (cJSON_IsString(q) && strncmp(q->valuestring, "360p", 4) == 0) {
                            break;
                        }
                    }
                }
            }
            cJSON_Delete(root);
            if (out_stream_url && out_stream_url[0] != '\0') {
                return 0;
            }
        } else if (root) {
            cJSON_Delete(root);
        }
    } else {
        http_response_free(&resp);
    }

    /* 2. Query Piped /streams/<id> for full description & direct MP4 stream */
    snprintf(endpoint, sizeof(endpoint), "%s/streams/%s", PIPED_FALLBACK_HOST, video_id);
    inv_log("[Video] Fetching details & stream: %s", endpoint);
    if (http_get(endpoint, &resp) == 0 && resp.data && resp.size > 0) {
        cJSON *root = cJSON_Parse(resp.data);
        http_response_free(&resp);
        if (root && cJSON_IsObject(root)) {
            snprintf(out_item->id, sizeof(out_item->id), "%s", video_id);

            cJSON *title = cJSON_GetObjectItemCaseSensitive(root, "title");
            if (cJSON_IsString(title) && title->valuestring) {
                sanitize_utf8_text(title->valuestring, out_item->title, sizeof(out_item->title));
            }
            cJSON *uploader = cJSON_GetObjectItemCaseSensitive(root, "uploader");
            if (cJSON_IsString(uploader) && uploader->valuestring) {
                sanitize_utf8_text(uploader->valuestring, out_item->author, sizeof(out_item->author));
            }
            cJSON *uploader_url = cJSON_GetObjectItemCaseSensitive(root, "uploaderUrl");
            if (cJSON_IsString(uploader_url) && uploader_url->valuestring) {
                snprintf(out_item->author_url, sizeof(out_item->author_url), "%s", uploader_url->valuestring);
                const char *ch = strstr(uploader_url->valuestring, "/channel/");
                if (ch) {
                    snprintf(out_item->author_id, sizeof(out_item->author_id), "%s", ch + 9);
                }
            }
            cJSON *desc = cJSON_GetObjectItemCaseSensitive(root, "description");
            if (cJSON_IsString(desc) && desc->valuestring && desc->valuestring[0] != '\0') {
                sanitize_utf8_text(desc->valuestring, out_item->description, sizeof(out_item->description));
            } else if (out_item->description[0] == '\0') {
                snprintf(out_item->description, sizeof(out_item->description), "No description available for this video.");
            }
            cJSON *dur = cJSON_GetObjectItemCaseSensitive(root, "duration");
            if (cJSON_IsNumber(dur) && dur->valueint > 0) {
                out_item->length_seconds = dur->valueint;
            }
            cJSON *views = cJSON_GetObjectItemCaseSensitive(root, "views");
            if (cJSON_IsNumber(views) && views->valuedouble > 0) {
                out_item->view_count = (long long)views->valuedouble;
            }
            cJSON *pub = cJSON_GetObjectItemCaseSensitive(root, "uploadDate");
            if (cJSON_IsString(pub) && pub->valuestring && out_item->published_text[0] == '\0') {
                strncpy(out_item->published_text, pub->valuestring, 10);
                out_item->published_text[10] = '\0';
            }
            snprintf(out_item->thumbnail_url, sizeof(out_item->thumbnail_url),
                     "https://i.ytimg.com/vi/%s/default.jpg", video_id);

            if (out_stream_url && max_stream_len > 0) {
                cJSON *vstreams = cJSON_GetObjectItemCaseSensitive(root, "videoStreams");
                cJSON *vs = NULL;
                cJSON_ArrayForEach(vs, vstreams) {
                    cJSON *vonly = cJSON_GetObjectItemCaseSensitive(vs, "videoOnly");
                    cJSON *fmt = cJSON_GetObjectItemCaseSensitive(vs, "format");
                    cJSON *url = cJSON_GetObjectItemCaseSensitive(vs, "url");
                    cJSON *qual = cJSON_GetObjectItemCaseSensitive(vs, "quality");
                    if ((!vonly || !cJSON_IsTrue(vonly)) &&
                        cJSON_IsString(fmt) && strcmp(fmt->valuestring, "MPEG_4") == 0 &&
                        cJSON_IsString(url) && url->valuestring) {
                        snprintf(out_stream_url, max_stream_len, "%s", url->valuestring);
                        if (cJSON_IsString(qual) && strcmp(qual->valuestring, "360p") == 0) {
                            break;
                        }
                    }
                }
            }
            cJSON_Delete(root);
            return 0;
        } else if (root) {
            cJSON_Delete(root);
        }
    } else {
        http_response_free(&resp);
    }

    /* 3. Lightweight YouTube oEmbed fallback for direct Video ID metadata */
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

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, videos_arr) {
        if (out_list->count >= MAX_VIDEOS) break;
        parse_invidious_video_object(item, &out_list->items[out_list->count++]);
    }

    cJSON_Delete(root);
    return out_list->count;
}
