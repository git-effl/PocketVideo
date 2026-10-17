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
    char author_url[128];
    char description[1024];
    int length_seconds;
    long long view_count;
    char published_text[64];
    char thumbnail_url[256];
} VideoItem;

typedef struct {
    char author[128];
    char author_id[64];
    char author_url[128];
    char description[512];
    long long sub_count;
    int video_count;
} ChannelItem;

typedef struct {
    VideoItem items[MAX_VIDEOS];
    int count;
    char instance[128];
} VideoList;

int invidious_fetch_trending(const char *instance, VideoList *out_list);
int invidious_search(const char *instance, const char *query, VideoList *out_list);
int invidious_fetch_channel_videos(const char *instance, const char *channel_id, VideoList *out_list);
void format_duration(int seconds, char *buffer, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* INVIDIOUS_H */
