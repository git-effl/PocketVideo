#ifndef INVIDIOUS_H
#define INVIDIOUS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_VIDEOS 50
#define INVIDIOUS_DEFAULT_HOST "https://yt.chocolatemoo53.com"
#define PIPED_FALLBACK_HOST    "https://api.piped.private.coffee"

typedef struct {
    char id[64];             /* Video ID or Channel ID */
    char title[256];         /* Video title or Channel name */
    char author[256];        /* Channel author/owner name */
    char author_id[64];      /* Channel ID (e.g. UC...) */
    char author_url[128];
    char description[2048];  /* Video or Channel description */
    int length_seconds;
    long long view_count;    /* View count or Subscriber count */
    char published_text[64];
    char thumbnail_url[256]; /* Video thumbnail or Channel icon URL */
    int is_channel;          /* 1 if item is a Channel, 0 if Video */
    char banner_url[256];    /* Channel banner URL */
    long long sub_count;     /* Subscriber count */
    int video_count;         /* Video count */
} VideoItem;

typedef struct {
    char author[256];
    char author_id[64];
    char author_url[128];
    char description[2048];
    char banner_url[256];
    char avatar_url[256];
    long long sub_count;
    int video_count;
} ChannelInfo;

typedef struct {
    VideoItem items[MAX_VIDEOS];
    int count;
    char instance[128];
} VideoList;

typedef struct {
    char author[128];
    char author_id[64];
    char author_avatar[256];
    char published_text[64];
    long like_count;
    char content[512];
} CommentItem;

typedef struct {
    int count;
    CommentItem items[32];
} CommentList;

int invidious_fetch_trending(const char *instance, VideoList *out_list);
int invidious_search(const char *instance, const char *query, VideoList *out_list);
int invidious_search_channels(const char *instance, const char *query, VideoList *out_list);
int invidious_fetch_video(const char *instance, const char *video_id, VideoItem *out_item,
                          char *out_stream_url, size_t max_stream_len);
int invidious_fetch_channel_info(const char *instance, const char *channel_id, ChannelInfo *out_info);
int invidious_fetch_channel_videos(const char *instance, const char *channel_id, const char *tab, VideoList *out_list);
int invidious_fetch_comments(const char *instance, const char *video_id, CommentList *out_comments);
int invidious_extract_video_id(const char *input, char *out_id, size_t max_len);
void sanitize_utf8_text(const char *src, char *dst, size_t max_len);
void format_duration(int seconds, char *buffer, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* INVIDIOUS_H */
