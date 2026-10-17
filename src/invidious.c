#include "invidious.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

void format_duration(int seconds, char *buffer, size_t max_len) {
    if (!buffer || max_len == 0) return;
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0) {
        snprintf(buffer, max_len, "%d:%02d:%02d", h, m, s);
    } else {
        snprintf(buffer, max_len, "%02d:%02d", m, s);
    }
}

int invidious_fetch_trending(const char *instance, VideoList *out_list) {
    if (!out_list) return -1;
    memset(out_list, 0, sizeof(VideoList));

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    snprintf(out_list->instance, sizeof(out_list->instance), "%s", base_url);

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/trending", base_url);
    printf("[Invidious] Requesting trending feed: %s\n", endpoint);

    HttpResponse resp;
    int net_status = http_get(endpoint, &resp);

    if (net_status != 0) {
        printf("[Invidious] Network request failed! Status: %d, Error: %s\n", net_status, resp.error_msg);
        http_response_free(&resp);
        return net_status;
    }

    if (!resp.data || resp.size == 0) {
        printf("[Invidious] Received empty response payload.\n");
        http_response_free(&resp);
        return -4;
    }

    cJSON *root = cJSON_Parse(resp.data);
    http_response_free(&resp);

    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        printf("[Invidious] JSON parsing failed! Error near: %s\n", error_ptr ? error_ptr : "unknown");
        return -5;
    }

    if (!cJSON_IsArray(root)) {
        cJSON *err_item = cJSON_GetObjectItemCaseSensitive(root, "error");
        if (cJSON_IsString(err_item) && err_item->valuestring) {
            printf("[Invidious] Instance returned API error: %s\n", err_item->valuestring);
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

        VideoItem *v = &out_list->items[count];

        // Video ID
        cJSON *vid = cJSON_GetObjectItemCaseSensitive(item, "videoId");
        if (cJSON_IsString(vid) && vid->valuestring) {
            snprintf(v->id, sizeof(v->id), "%s", vid->valuestring);
        }

        // Title
        cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
        if (cJSON_IsString(title) && title->valuestring) {
            snprintf(v->title, sizeof(v->title), "%s", title->valuestring);
        } else {
            snprintf(v->title, sizeof(v->title), "Untitled Video");
        }

        // Author / Channel Name
        cJSON *author = cJSON_GetObjectItemCaseSensitive(item, "author");
        if (cJSON_IsString(author) && author->valuestring) {
            snprintf(v->author, sizeof(v->author), "%s", author->valuestring);
        }

        // Author ID
        cJSON *author_id = cJSON_GetObjectItemCaseSensitive(item, "authorId");
        if (cJSON_IsString(author_id) && author_id->valuestring) {
            snprintf(v->author_id, sizeof(v->author_id), "%s", author_id->valuestring);
        }

        // Author URL
        cJSON *author_url = cJSON_GetObjectItemCaseSensitive(item, "authorUrl");
        if (cJSON_IsString(author_url) && author_url->valuestring) {
            snprintf(v->author_url, sizeof(v->author_url), "%s", author_url->valuestring);
        }

        // Description
        cJSON *desc = cJSON_GetObjectItemCaseSensitive(item, "description");
        if (cJSON_IsString(desc) && desc->valuestring) {
            snprintf(v->description, sizeof(v->description), "%.1020s", desc->valuestring);
        } else {
            snprintf(v->description, sizeof(v->description), "No description provided for this video.");
        }

        // Length in seconds
        cJSON *length = cJSON_GetObjectItemCaseSensitive(item, "lengthSeconds");
        if (cJSON_IsNumber(length)) {
            v->length_seconds = length->valueint;
        }

        // View count
        cJSON *views = cJSON_GetObjectItemCaseSensitive(item, "viewCount");
        if (cJSON_IsNumber(views)) {
            v->view_count = (long long)views->valuedouble;
        }

        // Published text
        cJSON *pub = cJSON_GetObjectItemCaseSensitive(item, "publishedText");
        if (cJSON_IsString(pub) && pub->valuestring) {
            snprintf(v->published_text, sizeof(v->published_text), "%s", pub->valuestring);
        }

        // Thumbnail URL
        cJSON *thumbs = cJSON_GetObjectItemCaseSensitive(item, "videoThumbnails");
        if (cJSON_IsArray(thumbs) && cJSON_GetArraySize(thumbs) > 0) {
            cJSON *first_thumb = cJSON_GetArrayItem(thumbs, 0);
            cJSON *thumb_url = cJSON_GetObjectItemCaseSensitive(first_thumb, "url");
            if (cJSON_IsString(thumb_url) && thumb_url->valuestring) {
                snprintf(v->thumbnail_url, sizeof(v->thumbnail_url), "%s", thumb_url->valuestring);
            }
        }

        count++;
    }

    cJSON_Delete(root);
    out_list->count = count;

    printf("[Invidious] Successfully parsed %d trending videos with metadata.\n", count);
    return count;
}

int invidious_fetch_channel_videos(const char *instance, const char *channel_id, VideoList *out_list) {
    if (!channel_id || !out_list) return -1;
    memset(out_list, 0, sizeof(VideoList));

    const char *base_url = (instance && strlen(instance) > 0) ? instance : INVIDIOUS_DEFAULT_HOST;
    snprintf(out_list->instance, sizeof(out_list->instance), "%s", base_url);

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "%s/api/v1/channels/%s/videos", base_url, channel_id);
    printf("[Invidious] Requesting channel videos for direct streaming: %s\n", endpoint);

    HttpResponse resp;
    int net_status = http_get(endpoint, &resp);
    if (net_status != 0) {
        printf("[Invidious] Channel fetch failed: %d\n", net_status);
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
        VideoItem *v = &out_list->items[count];

        cJSON *vid = cJSON_GetObjectItemCaseSensitive(item, "videoId");
        if (cJSON_IsString(vid) && vid->valuestring) {
            snprintf(v->id, sizeof(v->id), "%s", vid->valuestring);
        }

        cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
        if (cJSON_IsString(title) && title->valuestring) {
            snprintf(v->title, sizeof(v->title), "%s", title->valuestring);
        }

        cJSON *author = cJSON_GetObjectItemCaseSensitive(item, "author");
        if (cJSON_IsString(author) && author->valuestring) {
            snprintf(v->author, sizeof(v->author), "%s", author->valuestring);
        }

        cJSON *len = cJSON_GetObjectItemCaseSensitive(item, "lengthSeconds");
        if (cJSON_IsNumber(len)) {
            v->length_seconds = len->valueint;
        }

        cJSON *views = cJSON_GetObjectItemCaseSensitive(item, "viewCount");
        if (cJSON_IsNumber(views)) {
            v->view_count = (long long)views->valuedouble;
        }

        cJSON *pub = cJSON_GetObjectItemCaseSensitive(item, "publishedText");
        if (cJSON_IsString(pub) && pub->valuestring) {
            snprintf(v->published_text, sizeof(v->published_text), "%s", pub->valuestring);
        }

        cJSON *desc = cJSON_GetObjectItemCaseSensitive(item, "description");
        if (cJSON_IsString(desc) && desc->valuestring) {
            snprintf(v->description, sizeof(v->description), "%.1020s", desc->valuestring);
        }

        count++;
    }

    cJSON_Delete(root);
    out_list->count = count;
    printf("[Invidious] Loaded %d direct streaming videos from channel.\n", count);
    return count;
}

