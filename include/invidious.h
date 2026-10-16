#ifndef INVIDIOUS_H
#define INVIDIOUS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_VIDEOS 50
#define INVIDIOUS_DEFAULT_HOST "https://invidious.f5.si"

typedef struct {
    char id[32];
    char title[256];
    char author[128];
    char author_id[64];
    int length_seconds;
    long long view_count;
    char published_text[64];
    char thumbnail_url[256];
} VideoItem;

typedef struct {
    VideoItem items[MAX_VIDEOS];
    int count;
    char instance[128];
} VideoList;

/**
 * Fetch trending videos list from Invidious instance.
 * Queries "[instance]/api/v1/trending" and parses the JSON response using cJSON.
 *
 * @param instance Base URL of Invidious instance (or NULL to use INVIDIOUS_DEFAULT_HOST)
 * @param out_list Output structure receiving parsed video items
 * @return Number of videos parsed (> 0) on success, or negative error code on failure.
 */
int invidious_fetch_trending(const char *instance, VideoList *out_list);

/**
 * Search videos on Invidious instance.
 * Queries "[instance]/api/v1/search?q=[query]" and parses results.
 *
 * @param instance Base URL of Invidious instance
 * @param query Search query string (will be URL-encoded)
 * @param out_list Output structure
 * @return Number of videos found or negative error code.
 */
int invidious_search(const char *instance, const char *query, VideoList *out_list);

/**
 * Format video duration in seconds to MM:SS or HH:MM:SS string.
 */
void format_duration(int seconds, char *buffer, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* INVIDIOUS_H */
