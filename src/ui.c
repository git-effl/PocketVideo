#include "ui.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

vita2d_texture *g_tex_bg           = NULL;
vita2d_texture *g_tex_bubble_glass = NULL;

vita2d_font *g_font_noto           = NULL;
vita2d_font *g_font_emoji          = NULL;
vita2d_pvf  *g_font_pvf_fallback   = NULL;

#define THUMB_CACHE_SIZE 32
typedef struct {
    char key[256];
    int attempted;
    vita2d_texture *tex;
} ThumbCacheEntry;

static ThumbCacheEntry s_thumb_cache[THUMB_CACHE_SIZE];
static int s_thumb_next = 0;

static void format_views_commas(long long val, char *buf, size_t max_len) {
    if (!buf || max_len == 0) return;
    if (val < 0) val = 0;

    unsigned long long u = (unsigned long long)val;
    char rev[40];
    size_t rpos = 0;
    int digits = 0;

    do {
        if (digits > 0 && (digits % 3) == 0 && rpos + 1 < sizeof(rev)) {
            rev[rpos++] = ',';
        }
        rev[rpos++] = (char)('0' + (u % 10ULL));
        u /= 10ULL;
        digits++;
    } while (u > 0 && rpos + 1 < sizeof(rev));

    size_t out = 0;
    while (rpos > 0 && out + 1 < max_len) {
        buf[out++] = rev[--rpos];
    }
    buf[out] = '\0';
}

static void draw_button_box(float x, float y, float w, float h,
                            unsigned int bg_col, unsigned int border_col,
                            const char *label, unsigned int text_col, unsigned int font_size) {
    vita2d_draw_rectangle(x, y, w, h, bg_col);
    vita2d_draw_line(x, y, x + w, y, border_col);
    vita2d_draw_line(x, y + h, x + w, y + h, border_col);
    vita2d_draw_line(x, y, x, y + h, border_col);
    vita2d_draw_line(x + w, y, x + w, y + h, border_col);
    if (label) {
        ui_draw_text_centered(x + (w * 0.5f), y + (h * 0.5f) + (font_size * 0.35f),
                              text_col, font_size, label);
    }
}

int ui_init(void) {
    vita2d_init();
    vita2d_set_clear_color(COLOR_BRAND_NAVY);
    memset(s_thumb_cache, 0, sizeof(s_thumb_cache));

    /* Load NotoSans-Regular.ttf as default font for numbers, letters & standard characters */
    g_font_noto = vita2d_load_font_file("app0:assets/fonts/NotoSans-Regular.ttf");
    if (!g_font_noto) {
        g_font_noto = vita2d_load_font_file("assets/fonts/NotoSans-Regular.ttf");
    }
    if (!g_font_noto) {
        g_font_noto = vita2d_load_font_file("app0:assets/fonts/NotoSansSymbols2-Regular.ttf");
    }

    /* Load NotoColorEmoji-Regular.ttf as emoji font */
    g_font_emoji = vita2d_load_font_file("app0:assets/fonts/NotoColorEmoji-Regular.ttf");
    if (!g_font_emoji) {
        g_font_emoji = vita2d_load_font_file("assets/fonts/NotoColorEmoji-Regular.ttf");
    }

    if (!g_font_noto) {
        g_font_pvf_fallback = vita2d_load_default_pvf();
    }

    g_tex_bg = vita2d_load_PNG_file("app0:assets/textures/bg.png");
    if (!g_tex_bg) g_tex_bg = vita2d_load_PNG_file("assets/textures/bg.png");

    g_tex_bubble_glass = vita2d_load_PNG_file("app0:assets/textures/bubble_glass.png");
    if (!g_tex_bubble_glass) g_tex_bubble_glass = vita2d_load_PNG_file("assets/textures/bubble_glass.png");

    return 0;
}

void ui_clear_thumbnails(void) {
    for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
        if (s_thumb_cache[i].tex) {
            vita2d_free_texture(s_thumb_cache[i].tex);
            s_thumb_cache[i].tex = NULL;
        }
        s_thumb_cache[i].key[0] = '\0';
        s_thumb_cache[i].attempted = 0;
    }
    s_thumb_next = 0;
}

void ui_term(void) {
    ui_clear_thumbnails();
    if (g_font_noto)         { vita2d_free_font(g_font_noto);            g_font_noto = NULL; }
    if (g_font_emoji)        { vita2d_free_font(g_font_emoji);           g_font_emoji = NULL; }
    if (g_font_pvf_fallback) { vita2d_free_pvf(g_font_pvf_fallback);    g_font_pvf_fallback = NULL; }
    if (g_tex_bg)            { vita2d_free_texture(g_tex_bg);            g_tex_bg = NULL; }
    if (g_tex_bubble_glass)  { vita2d_free_texture(g_tex_bubble_glass);  g_tex_bubble_glass = NULL; }
    vita2d_fini();
}

vita2d_texture *ui_get_cached_thumbnail(const char *key) {
    if (!key || key[0] == '\0') return NULL;
    for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
        if (s_thumb_cache[i].key[0] != '\0' &&
            strcmp(s_thumb_cache[i].key, key) == 0) {
            return s_thumb_cache[i].tex;
        }
    }
    return NULL;
}

static int is_thumbnail_attempted(const char *key) {
    for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
        if (s_thumb_cache[i].key[0] != '\0' &&
            strcmp(s_thumb_cache[i].key, key) == 0) {
            return s_thumb_cache[i].attempted;
        }
    }
    return 0;
}

void ui_fetch_one_visible_thumbnail(const VideoList *list, int offset, int visible_count) {
    if (!list || list->count <= 0) return;

    for (int i = 0; i < visible_count && (offset + i) < list->count; i++) {
        const VideoItem *item = &list->items[offset + i];
        const char *key = (item->thumbnail_url[0] != '\0') ? item->thumbnail_url : item->id;
        if (key[0] == '\0') continue;
        if (is_thumbnail_attempted(key)) continue;

        ThumbCacheEntry *slot = &s_thumb_cache[s_thumb_next];
        if (slot->tex) {
            vita2d_free_texture(slot->tex);
            slot->tex = NULL;
        }
        snprintf(slot->key, sizeof(slot->key), "%s", key);
        slot->attempted = 1;
        s_thumb_next = (s_thumb_next + 1) % THUMB_CACHE_SIZE;

        HttpResponse resp;
        if (http_get(item->thumbnail_url, &resp) == 0 && resp.data && resp.size > 0) {
            slot->tex = vita2d_load_JPEG_buffer(resp.data, (unsigned long)resp.size);
            if (!slot->tex) {
                slot->tex = vita2d_load_PNG_buffer(resp.data, (unsigned long)resp.size);
            }
        }
        http_response_free(&resp);
        return;
    }
}

void ui_draw_background(void) {
    if (g_tex_bg) {
        vita2d_draw_texture(g_tex_bg, 0, 0);
    } else {
        vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BRAND_NAVY);
    }
}

void ui_draw_text(float x, float y, unsigned int color, unsigned int size, const char *text) {
    if (!text) return;
    if (g_font_noto) {
        vita2d_font_draw_text(g_font_noto, (int)x, (int)y, color, size, text);
    } else if (g_font_pvf_fallback) {
        float pvf_scale = (float)size / 20.0f;
        vita2d_pvf_draw_text(g_font_pvf_fallback, x, y, color, pvf_scale, text);
    }
}

int ui_get_text_width(unsigned int size, const char *text) {
    if (!text || text[0] == '\0') return 0;
    if (g_font_noto) {
        return vita2d_font_text_width(g_font_noto, size, text);
    } else if (g_font_pvf_fallback) {
        float pvf_scale = (float)size / 20.0f;
        return vita2d_pvf_text_width(g_font_pvf_fallback, pvf_scale, text);
    }
    return (int)(strlen(text) * (size * 0.52f));
}

void ui_draw_text_centered(float center_x, float y, unsigned int color, unsigned int size, const char *text) {
    if (!text) return;
    int w = ui_get_text_width(size, text);
    ui_draw_text(center_x - (w / 2.0f), y, color, size, text);
}

void ui_draw_status_bar(const char *title) {
    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 32, RGBA8(8, 14, 24, 235));
    vita2d_draw_line(0, 32, SCREEN_WIDTH, 32, RGBA8(35, 55, 90, 255));

    ui_draw_text(16, 22, COLOR_BRAND_CYAN, 16, "PocketVideo (yt.chocolatemoo53.com)");

    if (title) {
        ui_draw_text_centered(480.0f, 22, COLOR_TEXT_LIGHT, 17, title);
    }

    ui_draw_text(850, 22, COLOR_TEXT_LIGHT, 16, "100%");
    vita2d_draw_rectangle(905, 10, 24, 12, RGBA8(255, 255, 255, 180));
    vita2d_draw_rectangle(907, 12, 20, 8, RGBA8(46, 204, 113, 255));
    vita2d_draw_rectangle(929, 13, 3, 6, RGBA8(255, 255, 255, 180));
}

void ui_draw_bubble(float cx, float cy, float radius, const char *title,
                    unsigned int base_col, int is_pressed, int is_selected) {
    float r = radius;
    if (is_pressed) {
        r *= 0.94f;
        cy += 2.0f;
    }

    vita2d_draw_fill_circle(cx + 3, cy + 6, r + 2, COLOR_BUBBLE_SHADOW);

    if (is_selected) {
        vita2d_draw_fill_circle(cx, cy, r + 7.0f, COLOR_HIGHLIGHT_AMBER);
        vita2d_draw_fill_circle(cx, cy, r + 1.0f, RGBA8(10, 18, 36, 255));
    }

    vita2d_draw_fill_circle(cx, cy, r, base_col);

    if (g_tex_bubble_glass) {
        float gw = (float)vita2d_texture_get_width(g_tex_bubble_glass);
        float scale = (r * 2.0f) / gw;
        vita2d_draw_texture_scale(g_tex_bubble_glass, cx - r, cy - r, scale, scale);
    } else {
        vita2d_draw_fill_circle(cx - (r * 0.22f), cy - (r * 0.25f), r * 0.42f, COLOR_GLASS_SHEEN);
    }

    if (title) {
        ui_draw_text_centered(cx, cy + r + 24.0f, COLOR_TEXT_LIGHT, 16, title);
    }
}

void ui_draw_search_bar(const char *query, int is_search_mode, int search_type) {
    vita2d_draw_rectangle(20, 38, 700, 34, RGBA8(8, 15, 28, 240));
    vita2d_draw_line(20, 38, 720, 38, RGBA8(0, 174, 239, 190));
    vita2d_draw_line(20, 72, 720, 72, RGBA8(0, 174, 239, 190));
    vita2d_draw_line(20, 38, 20, 72, RGBA8(0, 174, 239, 190));
    vita2d_draw_line(720, 38, 720, 72, RGBA8(0, 174, 239, 190));

    vita2d_draw_fill_circle(37, 53, 6.0f, COLOR_BRAND_CYAN);
    vita2d_draw_fill_circle(37, 53, 3.5f, RGBA8(8, 15, 28, 255));
    vita2d_draw_line(41, 57, 46, 62, COLOR_BRAND_CYAN);

    if (query && query[0] != '\0') {
        char display_q[72];
        size_t qlen = strlen(query);
        if (qlen <= 60) {
            snprintf(display_q, sizeof(display_q), "%s", query);
        } else {
            memcpy(display_q, query, 60);
            memcpy(display_q + 60, "...", 4);
        }
        ui_draw_text(54, 61, COLOR_TEXT_LIGHT, 15, display_q);
    } else {
        ui_draw_text(54, 61, COLOR_TEXT_DIM, 14,
                     (search_type == 1) ? "Search Channels or enter Channel ID (Tap or press \xE2\x96\xA1 SQUARE)..."
                                        : "Search YouTube or enter Video ID (Tap or press \xE2\x96\xA1 SQUARE)...");
    }

    /* Mode button: Videos / Channels (x = 728..818) */
    unsigned int mode_bg = (search_type == 1) ? RGBA8(200, 45, 120, 235) : RGBA8(0, 130, 195, 235);
    draw_button_box(728.0f, 38.0f, 90.0f, 34.0f,
                    mode_bg, COLOR_BRAND_CYAN,
                    (search_type == 1) ? "Channels \xE2\x96\xA1" : "Videos \xE2\x96\xA1",
                    COLOR_TEXT_LIGHT, 12);

    /* Search button (x = 824..880) */
    draw_button_box(824.0f, 38.0f, 56.0f, 34.0f,
                    RGBA8(0, 150, 215, 235), COLOR_BRAND_CYAN,
                    "Search", COLOR_TEXT_LIGHT, 12);

    /* Reset button (x = 886..940) */
    if (is_search_mode) {
        draw_button_box(886.0f, 38.0f, 54.0f, 34.0f,
                        RGBA8(217, 119, 6, 235), COLOR_HIGHLIGHT_AMBER,
                        "Reset", COLOR_TEXT_LIGHT, 12);
    } else {
        draw_button_box(886.0f, 38.0f, 54.0f, 34.0f,
                        RGBA8(24, 44, 76, 235), RGBA8(65, 105, 165, 255),
                        "Refresh", COLOR_TEXT_LIGHT, 12);
    }
}

void ui_draw_video_card(float x, float y, float w, float h, const VideoItem *item, int is_focused) {
    if (!item) return;

    if (item->is_channel) {
        /* CHANNEL SEARCH RESULT CARD */
        unsigned int bg_col = is_focused ? RGBA8(38, 22, 54, 240) : RGBA8(18, 12, 28, 210);
        vita2d_draw_rectangle(x, y, w, h, bg_col);

        unsigned int border_col = is_focused ? COLOR_HIGHLIGHT_AMBER : RGBA8(85, 45, 115, 180);
        vita2d_draw_line(x, y, x + w, y, border_col);
        vita2d_draw_line(x, y + h, x + w, y + h, border_col);
        vita2d_draw_line(x, y, x, y + h, border_col);
        vita2d_draw_line(x + w, y, x + w, y + h, border_col);

        /* Direct streamed channel round avatar icon */
        float icon_cx = x + 40.0f;
        float icon_cy = y + (h * 0.5f);
        float icon_r  = 28.0f;

        vita2d_draw_fill_circle(icon_cx, icon_cy, icon_r + 2.0f, COLOR_BRAND_CYAN);
        vita2d_draw_fill_circle(icon_cx, icon_cy, icon_r, RGBA8(8, 14, 28, 255));

        const char *thumb_key = (item->thumbnail_url[0] != '\0') ? item->thumbnail_url : item->id;
        vita2d_texture *avatar = ui_get_cached_thumbnail(thumb_key);
        if (avatar) {
            float tw = (float)vita2d_texture_get_width(avatar);
            float th = (float)vita2d_texture_get_height(avatar);
            if (tw > 0.0f && th > 0.0f) {
                float scale = (icon_r * 2.0f) / tw;
                vita2d_draw_texture_scale(avatar, icon_cx - icon_r, icon_cy - icon_r, scale, scale);
            }
        }

        /* Channel Name */
        char truncated_name[58];
        size_t nlen = strlen(item->title);
        if (nlen <= 50) {
            snprintf(truncated_name, sizeof(truncated_name), "%s", item->title);
        } else {
            memcpy(truncated_name, item->title, 50);
            memcpy(truncated_name + 50, "...", 4);
        }
        ui_draw_text(x + 82.0f, y + 25.0f,
                     is_focused ? COLOR_HIGHLIGHT_AMBER : COLOR_TEXT_LIGHT, 17, truncated_name);

        /* Channel Stats & Description snippet ending in ... */
        char stats_buf[256];
        if (item->sub_count > 0) {
            char sub_buf[40];
            format_views_commas(item->sub_count, sub_buf, sizeof(sub_buf));
            snprintf(stats_buf, sizeof(stats_buf), "Channel  *  %s subscribers", sub_buf);
        } else {
            snprintf(stats_buf, sizeof(stats_buf), "YouTube Channel");
        }
        ui_draw_text(x + 82.0f, y + 46.0f, COLOR_BRAND_CYAN, 13, stats_buf);

        char desc_snippet[128];
        size_t dlen = strlen(item->description);
        if (dlen <= 65) {
            snprintf(desc_snippet, sizeof(desc_snippet), "%s", item->description[0] ? item->description : "YouTube Channel");
        } else {
            memcpy(desc_snippet, item->description, 62);
            memcpy(desc_snippet + 62, "...", 4);
        }
        ui_draw_text(x + 82.0f, y + 64.0f, COLOR_TEXT_DIM, 12, desc_snippet);

        /* View Channel button */
        draw_button_box(x + w - 128.0f, y + 22.0f, 114.0f, 32.0f,
                        is_focused ? RGBA8(217, 119, 6, 230) : RGBA8(140, 70, 10, 180),
                        COLOR_HIGHLIGHT_AMBER,
                        "View Channel", COLOR_TEXT_LIGHT, 13);
        return;
    }

    /* STANDARD VIDEO CARD */
    unsigned int bg_col = is_focused ? RGBA8(22, 42, 74, 240) : RGBA8(14, 22, 38, 210);
    vita2d_draw_rectangle(x, y, w, h, bg_col);

    unsigned int border_col = is_focused ? COLOR_BRAND_CYAN : RGBA8(35, 55, 85, 180);
    vita2d_draw_line(x, y, x + w, y, border_col);
    vita2d_draw_line(x, y + h, x + w, y + h, border_col);
    vita2d_draw_line(x, y, x, y + h, border_col);
    vita2d_draw_line(x + w, y, x + w, y + h, border_col);

    float thumb_x = x + 8.0f;
    float thumb_y = y + 6.0f;
    float thumb_w = 114.0f;
    float thumb_h = 64.0f;
    vita2d_draw_rectangle(thumb_x, thumb_y, thumb_w, thumb_h, RGBA8(4, 8, 14, 255));

    const char *key = (item->thumbnail_url[0] != '\0') ? item->thumbnail_url : item->id;
    vita2d_texture *thumb = ui_get_cached_thumbnail(key);
    if (thumb) {
        float tw = (float)vita2d_texture_get_width(thumb);
        float th = (float)vita2d_texture_get_height(thumb);
        if (tw > 0.0f && th > 0.0f) {
            vita2d_draw_texture_scale(thumb, thumb_x, thumb_y, thumb_w / tw, thumb_h / th);
        }
    }

    char dur[16];
    format_duration(item->length_seconds, dur, sizeof(dur));
    unsigned int badge_col = (item->length_seconds <= 0) ? RGBA8(200, 30, 45, 235) : RGBA8(0, 0, 0, 225);
    vita2d_draw_rectangle(thumb_x + thumb_w - 46.0f, thumb_y + thumb_h - 16.0f, 46.0f, 16.0f, badge_col);
    ui_draw_text_centered(thumb_x + thumb_w - 23.0f, thumb_y + thumb_h - 3.0f, COLOR_TEXT_LIGHT, 11, dur);

    char truncated_title[58];
    size_t tlen = strlen(item->title);
    if (tlen <= 50) {
        snprintf(truncated_title, sizeof(truncated_title), "%s", item->title);
    } else {
        memcpy(truncated_title, item->title, 50);
        memcpy(truncated_title + 50, "...", 4);
    }
    ui_draw_text(x + thumb_w + 18.0f, y + 27.0f,
                 is_focused ? COLOR_BRAND_CYAN : COLOR_TEXT_LIGHT, 17, truncated_title);

    char meta_info[384];
    if (item->view_count > 0) {
        char views_buf[40];
        format_views_commas(item->view_count, views_buf, sizeof(views_buf));
        if (item->published_text[0] != '\0') {
            snprintf(meta_info, sizeof(meta_info), "%s  *  %s views  *  %s",
                     item->author, views_buf, item->published_text);
        } else {
            snprintf(meta_info, sizeof(meta_info), "%s  *  %s views",
                     item->author, views_buf);
        }
    } else if (item->published_text[0] != '\0' && strcmp(item->published_text, "0 seconds ago") != 0) {
        snprintf(meta_info, sizeof(meta_info), "%s  *  %s", item->author, item->published_text);
    } else {
        snprintf(meta_info, sizeof(meta_info), "%s  *  Live / Featured", item->author);
    }
    ui_draw_text(x + thumb_w + 18.0f, y + 53.0f, COLOR_TEXT_DIM, 14, meta_info);

    unsigned int chan_btn_bg = is_focused ? RGBA8(16, 185, 129, 230) : RGBA8(12, 130, 90, 170);
    unsigned int chan_btn_border = is_focused ? RGBA8(52, 211, 153, 255) : RGBA8(16, 110, 78, 210);
    draw_button_box(x + w - 118.0f, y + 22.0f, 104.0f, 30.0f,
                    chan_btn_bg, chan_btn_border,
                    "Channel", COLOR_TEXT_LIGHT, 13);
}

void ui_draw_player_screen(const VideoItem *item, vita2d_texture *video_tex,
                           vita2d_texture *fallback_thumb, float hud_alpha,
                           int is_paused, uint64_t cur_ms, uint64_t total_ms,
                           int desc_open, int desc_scroll, const char *player_status) {
    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(0, 0, 0, 255));

    if (video_tex) {
        float vw = (float)vita2d_texture_get_width(video_tex);
        float vh = (float)vita2d_texture_get_height(video_tex);
        if (vw > 0.0f && vh > 0.0f) {
            float sx = (float)SCREEN_WIDTH / vw;
            float sy = (float)SCREEN_HEIGHT / vh;
            float scale = (sx < sy) ? sx : sy;
            float dx = ((float)SCREEN_WIDTH - (vw * scale)) * 0.5f;
            float dy = ((float)SCREEN_HEIGHT - (vh * scale)) * 0.5f;
            vita2d_draw_texture_scale(video_tex, dx, dy, scale, scale);
        }
    } else if (fallback_thumb) {
        float tw = (float)vita2d_texture_get_width(fallback_thumb);
        float th = (float)vita2d_texture_get_height(fallback_thumb);
        if (tw > 0.0f && th > 0.0f) {
            float sx = (float)SCREEN_WIDTH / tw;
            float sy = (float)SCREEN_HEIGHT / th;
            float scale = (sx < sy) ? sx : sy;
            float dx = ((float)SCREEN_WIDTH - (tw * scale)) * 0.5f;
            float dy = ((float)SCREEN_HEIGHT - (th * scale)) * 0.5f;
            vita2d_draw_texture_scale(fallback_thumb, dx, dy, scale, scale);
            vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(6, 12, 24, 135));
        }
    }

    if (!video_tex && player_status && player_status[0] != '\0') {
        ui_draw_text_centered(480.0f, 272.0f, COLOR_TEXT_LIGHT, 18, player_status);
    }

    if (desc_open && item) {
        ui_draw_description_modal(item, desc_scroll);
        return;
    }

    if (hud_alpha > 0.01f && item) {
        if (hud_alpha > 1.0f) hud_alpha = 1.0f;
        uint8_t a = (uint8_t)(hud_alpha * 255.0f);
        uint8_t bar_a = (uint8_t)(hud_alpha * 218.0f);

        vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 58, RGBA8(8, 14, 26, bar_a));
        vita2d_draw_line(0, 58, SCREEN_WIDTH, 58, RGBA8(0, 174, 239, (uint8_t)(hud_alpha * 160.0f)));

        char title_buf[72];
        size_t tlen = strlen(item->title);
        if (tlen <= 64) {
            snprintf(title_buf, sizeof(title_buf), "%s", item->title);
        } else {
            memcpy(title_buf, item->title, 64);
            memcpy(title_buf + 64, "...", 4);
        }
        ui_draw_text(20, 26, RGBA8(245, 245, 248, a), 18, title_buf);

        char sub_buf[384];
        if (item->view_count > 0) {
            char vbuf[40];
            format_views_commas(item->view_count, vbuf, sizeof(vbuf));
            snprintf(sub_buf, sizeof(sub_buf), "%s   |   %s views   |   ID: %s",
                     item->author, vbuf, item->id);
        } else {
            snprintf(sub_buf, sizeof(sub_buf), "%s   |   ID: %s", item->author, item->id);
        }
        ui_draw_text(20, 48, RGBA8(0, 174, 239, a), 13, sub_buf);

        vita2d_draw_rectangle(0, 444, SCREEN_WIDTH, 100, RGBA8(8, 14, 26, bar_a));
        vita2d_draw_line(0, 444, SCREEN_WIDTH, 444, RGBA8(40, 68, 110, (uint8_t)(hud_alpha * 200.0f)));

        float bar_x = 24.0f;
        float bar_y = 458.0f;
        float bar_w = 912.0f;
        vita2d_draw_rectangle(bar_x, bar_y, bar_w, 6.0f, RGBA8(45, 65, 100, a));

        uint64_t effective_total = total_ms;
        if (effective_total == 0 && item->length_seconds > 0) {
            effective_total = (uint64_t)item->length_seconds * 1000ULL;
        }
        float ratio = 0.0f;
        if (effective_total > 0) {
            ratio = (float)cur_ms / (float)effective_total;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
        }
        float fill_w = bar_w * ratio;
        vita2d_draw_rectangle(bar_x, bar_y, fill_w, 6.0f, RGBA8(0, 174, 239, a));
        vita2d_draw_fill_circle(bar_x + fill_w, bar_y + 3.0f, 6.5f, RGBA8(255, 175, 20, a));

        char cur_str[16], tot_str[16], time_label[64];
        format_duration((int)(cur_ms / 1000ULL), cur_str, sizeof(cur_str));
        if (effective_total > 0) {
            format_duration((int)(effective_total / 1000ULL), tot_str, sizeof(tot_str));
        } else {
            snprintf(tot_str, sizeof(tot_str), "LIVE");
        }
        snprintf(time_label, sizeof(time_label), "%s / %s%s",
                 (cur_ms == 0 && effective_total == 0) ? "LIVE" : cur_str,
                 tot_str,
                 is_paused ? "  [PAUSED]" : "");
        ui_draw_text(24, 486, RGBA8(245, 245, 248, a), 14, time_label);

        draw_button_box(240.0f, 492.0f, 104.0f, 38.0f,
                        RGBA8(22, 42, 74, bar_a), RGBA8(75, 115, 175, a),
                        "<< -10s (L)", RGBA8(245, 245, 248, a), 13);

        draw_button_box(356.0f, 492.0f, 124.0f, 38.0f,
                        RGBA8(0, 125, 195, bar_a), RGBA8(0, 174, 239, a),
                        is_paused ? "Play (X)" : "Pause (X)", RGBA8(245, 245, 248, a), 14);

        draw_button_box(492.0f, 492.0f, 104.0f, 38.0f,
                        RGBA8(22, 42, 74, bar_a), RGBA8(75, 115, 175, a),
                        "+10s (R) >>", RGBA8(245, 245, 248, a), 13);

        draw_button_box(612.0f, 492.0f, 160.0f, 38.0f,
                        RGBA8(205, 45, 120, bar_a), RGBA8(255, 120, 190, a),
                        "Description \xE2\x96\xA1", RGBA8(245, 245, 248, a), 13);

        float sq_x = 620.0f;
        float sq_y = 504.0f;
        float sq_s = 13.0f;
        vita2d_draw_line(sq_x, sq_y, sq_x + sq_s, sq_y, RGBA8(255, 255, 255, a));
        vita2d_draw_line(sq_x + sq_s, sq_y, sq_x + sq_s, sq_y + sq_s, RGBA8(255, 255, 255, a));
        vita2d_draw_line(sq_x, sq_y + sq_s, sq_x + sq_s, sq_y + sq_s, RGBA8(255, 255, 255, a));
        vita2d_draw_line(sq_x, sq_y, sq_x, sq_y + sq_s, RGBA8(255, 255, 255, a));

        draw_button_box(784.0f, 492.0f, 152.0f, 38.0f,
                        RGBA8(30, 44, 68, bar_a), RGBA8(120, 145, 185, a),
                        "Back (O)", RGBA8(245, 245, 248, a), 13);
    }
}

void ui_draw_description_modal(const VideoItem *item, int scroll_line) {
    if (!item) return;

    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(0, 0, 0, 185));

    float mx = 90.0f;
    float my = 54.0f;
    float mw = 780.0f;
    float mh = 436.0f;

    vita2d_draw_rectangle(mx + 6, my + 8, mw, mh, RGBA8(0, 0, 0, 140));
    vita2d_draw_rectangle(mx, my, mw, mh, RGBA8(12, 20, 36, 248));

    vita2d_draw_rectangle(mx, my, mw, 3, COLOR_BRAND_CYAN);
    vita2d_draw_line(mx, my, mx, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx + mw, my, mx + mw, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx, my + mh, mx + mw, my + mh, RGBA8(40, 65, 105, 255));

    ui_draw_text(mx + 24, my + 34, COLOR_BRAND_CYAN, 20, "Video Description & Metadata \xE2\x96\xA1");
    vita2d_draw_line(mx + 24, my + 46, mx + mw - 24, my + 46, RGBA8(30, 50, 80, 255));

    char truncated_title[74];
    size_t tlen = strlen(item->title);
    if (tlen <= 66) {
        snprintf(truncated_title, sizeof(truncated_title), "%s", item->title);
    } else {
        memcpy(truncated_title, item->title, 66);
        memcpy(truncated_title + 66, "...", 4);
    }
    ui_draw_text(mx + 24, my + 76, COLOR_TEXT_LIGHT, 17, truncated_title);

    char views_buf[40];
    format_views_commas(item->view_count, views_buf, sizeof(views_buf));
    char meta[384];
    snprintf(meta, sizeof(meta), "Channel: %s   |   Views: %s   |   Published: %s",
             item->author, views_buf, item->published_text[0] ? item->published_text : "N/A");
    ui_draw_text(mx + 24, my + 104, COLOR_HIGHLIGHT_AMBER, 14, meta);

    vita2d_draw_rectangle(mx + 24, my + 120, mw - 48, 254, RGBA8(6, 10, 18, 245));

    const char *desc = (strlen(item->description) > 0) ? item->description : "No description provided.";
    int len = (int)strlen(desc);
    int start = 0;
    int line_idx = 0;
    int drawn = 0;
    int max_visible = 10;
    if (scroll_line < 0) scroll_line = 0;

    char line_buf[88];
    while (start < len) {
        int take = 78;
        if (start + take > len) take = len - start;

        int space_pos = -1;
        if (start + take < len) {
            for (int k = take - 1; k >= 20; k--) {
                if (desc[start + k] == ' ' || desc[start + k] == '\n') {
                    space_pos = k;
                    break;
                }
            }
        }
        if (space_pos > 0) take = space_pos;

        if (line_idx >= scroll_line && drawn < max_visible) {
            memcpy(line_buf, desc + start, take);
            line_buf[take] = '\0';
            ui_draw_text(mx + 36, my + 144 + (drawn * 23), COLOR_TEXT_LIGHT, 14, line_buf);
            drawn++;
        }
        line_idx++;
        start += take;
        while (start < len && (desc[start] == ' ' || desc[start] == '\n' || desc[start] == '\r')) start++;
    }

    draw_button_box(mx + mw - 180.0f, my + mh - 44.0f, 156.0f, 32.0f,
                    RGBA8(0, 130, 195, 235), COLOR_BRAND_CYAN,
                    "Close (Circle / \xE2\x96\xA1)", COLOR_TEXT_LIGHT, 12);
}

void ui_draw_channel_description_modal(const ChannelInfo *channel, int scroll_line) {
    if (!channel) return;

    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(0, 0, 0, 185));

    float mx = 90.0f;
    float my = 54.0f;
    float mw = 780.0f;
    float mh = 436.0f;

    vita2d_draw_rectangle(mx + 6, my + 8, mw, mh, RGBA8(0, 0, 0, 140));
    vita2d_draw_rectangle(mx, my, mw, mh, RGBA8(12, 20, 36, 248));

    vita2d_draw_rectangle(mx, my, mw, 3, COLOR_HIGHLIGHT_AMBER);
    vita2d_draw_line(mx, my, mx, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx + mw, my, mx + mw, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx, my + mh, mx + mw, my + mh, RGBA8(40, 65, 105, 255));

    ui_draw_text(mx + 24, my + 34, COLOR_HIGHLIGHT_AMBER, 20, "Channel Bio & Full Description \xE2\x96\xA1");
    vita2d_draw_line(mx + 24, my + 46, mx + mw - 24, my + 46, RGBA8(30, 50, 80, 255));

    ui_draw_text(mx + 24, my + 76, COLOR_TEXT_LIGHT, 18, channel->author[0] ? channel->author : "Channel Info");

    char sub_str[40];
    format_views_commas(channel->sub_count, sub_str, sizeof(sub_str));
    char meta[128];
    snprintf(meta, sizeof(meta), "Subscribers: %s   |   ID: %s", sub_str, channel->author_id);
    ui_draw_text(mx + 24, my + 104, COLOR_BRAND_CYAN, 14, meta);

    vita2d_draw_rectangle(mx + 24, my + 120, mw - 48, 254, RGBA8(6, 10, 18, 245));

    const char *desc = (strlen(channel->description) > 0) ? channel->description : "No description provided for this channel.";
    int len = (int)strlen(desc);
    int start = 0;
    int line_idx = 0;
    int drawn = 0;
    int max_visible = 10;
    if (scroll_line < 0) scroll_line = 0;

    char line_buf[88];
    while (start < len) {
        int take = 78;
        if (start + take > len) take = len - start;

        int space_pos = -1;
        if (start + take < len) {
            for (int k = take - 1; k >= 20; k--) {
                if (desc[start + k] == ' ' || desc[start + k] == '\n') {
                    space_pos = k;
                    break;
                }
            }
        }
        if (space_pos > 0) take = space_pos;

        if (line_idx >= scroll_line && drawn < max_visible) {
            memcpy(line_buf, desc + start, take);
            line_buf[take] = '\0';
            ui_draw_text(mx + 36, my + 144 + (drawn * 23), COLOR_TEXT_LIGHT, 14, line_buf);
            drawn++;
        }
        line_idx++;
        start += take;
        while (start < len && (desc[start] == ' ' || desc[start] == '\n' || desc[start] == '\r')) start++;
    }

    draw_button_box(mx + mw - 180.0f, my + mh - 44.0f, 156.0f, 32.0f,
                    RGBA8(217, 119, 6, 235), COLOR_HIGHLIGHT_AMBER,
                    "Close (Circle / \xE2\x96\xA1)", COLOR_TEXT_LIGHT, 12);
}

void ui_draw_channel_view(const ChannelInfo *channel, const VideoList *videos,
                          int selected_tab, int selected_video_idx, int desc_modal_open,
                          int desc_scroll) {
    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BRAND_NAVY);

    /* Channel Banner Header (Y: 0..110) */
    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 110, RGBA8(16, 28, 52, 255));
    if (channel && channel->banner_url[0] != '\0') {
        vita2d_texture *banner_tex = ui_get_cached_thumbnail(channel->banner_url);
        if (banner_tex) {
            float bw = (float)vita2d_texture_get_width(banner_tex);
            float bh = (float)vita2d_texture_get_height(banner_tex);
            if (bw > 0.0f && bh > 0.0f) {
                vita2d_draw_texture_scale(banner_tex, 0, 0, (float)SCREEN_WIDTH / bw, 110.0f / bh);
                vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 110, RGBA8(0, 0, 0, 90));
            }
        }
    }
    vita2d_draw_line(0, 110, SCREEN_WIDTH, 110, COLOR_BRAND_CYAN);

    /* Channel Avatar Icon (Top Middle-Left, X: 24..94, Y: 20..90) */
    float icon_cx = 59.0f;
    float icon_cy = 55.0f;
    float icon_r  = 35.0f;
    vita2d_draw_fill_circle(icon_cx, icon_cy, icon_r + 3.0f, COLOR_HIGHLIGHT_AMBER);
    vita2d_draw_fill_circle(icon_cx, icon_cy, icon_r, RGBA8(8, 14, 28, 255));

    if (channel && channel->avatar_url[0] != '\0') {
        vita2d_texture *avatar_tex = ui_get_cached_thumbnail(channel->avatar_url);
        if (avatar_tex) {
            float aw = (float)vita2d_texture_get_width(avatar_tex);
            float ah = (float)vita2d_texture_get_height(avatar_tex);
            if (aw > 0.0f && ah > 0.0f) {
                float scale = (icon_r * 2.0f) / aw;
                vita2d_draw_texture_scale(avatar_tex, icon_cx - icon_r, icon_cy - icon_r, scale, scale);
            }
        }
    }

    /* Channel Name & Sub Count (X: 110, Y: 46) */
    if (channel) {
        ui_draw_text(110, 48, COLOR_TEXT_LIGHT, 22, channel->author[0] ? channel->author : "YouTube Channel");

        char sub_str[40];
        format_views_commas(channel->sub_count, sub_str, sizeof(sub_str));
        char stats_buf[128];
        snprintf(stats_buf, sizeof(stats_buf), "%s subscribers", sub_str);
        ui_draw_text(110, 72, COLOR_HIGHLIGHT_AMBER, 14, stats_buf);
    }

    /* Channel Description Snippet (Y: 118..148) */
    vita2d_draw_rectangle(0, 111, SCREEN_WIDTH, 42, RGBA8(12, 20, 36, 245));
    vita2d_draw_line(0, 153, SCREEN_WIDTH, 153, RGBA8(35, 55, 85, 255));

    if (channel && channel->description[0] != '\0') {
        char desc_snip[120];
        size_t dlen = strlen(channel->description);
        if (dlen <= 75) {
            snprintf(desc_snip, sizeof(desc_snip), "%s", channel->description);
        } else {
            memcpy(desc_snip, channel->description, 72);
            memcpy(desc_snip + 72, "...", 4);
        }
        ui_draw_text(20, 134, COLOR_TEXT_DIM, 13, desc_snip);
    } else {
        ui_draw_text(20, 134, COLOR_TEXT_DIM, 13, "Official YouTube Channel");
    }

    /* Bio button / Square hint on right of description snippet */
    draw_button_box(SCREEN_WIDTH - 170.0f, 116.0f, 150.0f, 32.0f,
                    RGBA8(205, 45, 120, 230), RGBA8(255, 120, 190, 255),
                    "Full Bio \xE2\x96\xA1", COLOR_TEXT_LIGHT, 12);

    /* Category Tabs: Home, Videos, Shorts, Live, Playlists (Y: 154..190) */
    const char *tabs[] = {"Home", "Videos", "Shorts", "Live", "Playlists"};
    int num_tabs = 5;
    float tab_w = 176.0f;
    float tab_start_x = 20.0f;
    float tab_y = 158.0f;

    for (int t = 0; t < num_tabs; t++) {
        float tx = tab_start_x + (t * (tab_w + 10.0f));
        int is_active = (t == selected_tab);
        unsigned int bg = is_active ? RGBA8(0, 140, 210, 240) : RGBA8(18, 30, 52, 210);
        unsigned int border = is_active ? COLOR_HIGHLIGHT_AMBER : RGBA8(40, 65, 100, 200);
        draw_button_box(tx, tab_y, tab_w, 32.0f, bg, border, tabs[t],
                        is_active ? COLOR_HIGHLIGHT_AMBER : COLOR_TEXT_LIGHT, 14);
    }

    /* Channel Videos Feed (Y: 196..530) */
    int card_h = 76;
    int gap = 8;
    int start_y = 196;
    int visible = 4;

    int offset = 0;
    if (selected_video_idx >= visible) {
        offset = selected_video_idx - visible + 1;
    }

    if (videos && videos->count > 0) {
        ui_fetch_one_visible_thumbnail(videos, offset, visible);

        for (int i = 0; i < visible && (offset + i) < videos->count; i++) {
            int idx = offset + i;
            float cy = start_y + (i * (card_h + gap));
            ui_draw_video_card(20, cy, 920, card_h, &videos->items[idx], (idx == selected_video_idx));
        }
    } else {
        ui_draw_text_centered(480.0f, 320.0f, COLOR_TEXT_DIM, 16,
                              "No content available under this category tab.");
    }

    /* Full Channel Description Modal */
    if (desc_modal_open && channel) {
        ui_draw_channel_description_modal(channel, desc_scroll);
    }
}
