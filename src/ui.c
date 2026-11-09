#include "ui.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

vita2d_texture *g_tex_bg           = NULL;
vita2d_texture *g_tex_bubble_glass = NULL;

vita2d_font *g_font_noto           = NULL;
vita2d_pvf  *g_font_pvf_fallback   = NULL;

#define THUMB_CACHE_SIZE 16
typedef struct {
    char video_id[32];
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

    /* Load NotoSansSymbols2-Regular.ttf as primary font for full symbol & emoji rendering */
    g_font_noto = vita2d_load_font_file("app0:assets/fonts/NotoSansSymbols2-Regular.ttf");
    if (!g_font_noto) {
        g_font_noto = vita2d_load_font_file("assets/fonts/NotoSansSymbols2-Regular.ttf");
    }
    if (!g_font_noto) {
        g_font_noto = vita2d_load_font_file("app0:assets/fonts/NotoSans-Regular.ttf");
    }
    if (!g_font_noto) {
        g_font_noto = vita2d_load_font_file("assets/fonts/NotoSans-Regular.ttf");
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
        s_thumb_cache[i].video_id[0] = '\0';
        s_thumb_cache[i].attempted = 0;
    }
    s_thumb_next = 0;
}

void ui_term(void) {
    ui_clear_thumbnails();
    if (g_font_noto)         { vita2d_free_font(g_font_noto);            g_font_noto = NULL; }
    if (g_font_pvf_fallback) { vita2d_free_pvf(g_font_pvf_fallback);    g_font_pvf_fallback = NULL; }
    if (g_tex_bg)            { vita2d_free_texture(g_tex_bg);            g_tex_bg = NULL; }
    if (g_tex_bubble_glass)  { vita2d_free_texture(g_tex_bubble_glass);  g_tex_bubble_glass = NULL; }
    vita2d_fini();
}

vita2d_texture *ui_get_cached_thumbnail(const char *video_id) {
    if (!video_id || video_id[0] == '\0') return NULL;
    for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
        if (s_thumb_cache[i].video_id[0] != '\0' &&
            strcmp(s_thumb_cache[i].video_id, video_id) == 0) {
            return s_thumb_cache[i].tex;
        }
    }
    return NULL;
}

static int is_thumbnail_attempted(const char *video_id) {
    for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
        if (s_thumb_cache[i].video_id[0] != '\0' &&
            strcmp(s_thumb_cache[i].video_id, video_id) == 0) {
            return s_thumb_cache[i].attempted;
        }
    }
    return 0;
}

void ui_fetch_one_visible_thumbnail(const VideoList *list, int offset, int visible_count) {
    if (!list || list->count <= 0) return;

    for (int i = 0; i < visible_count && (offset + i) < list->count; i++) {
        const VideoItem *item = &list->items[offset + i];
        if (item->id[0] == '\0' || item->thumbnail_url[0] == '\0') continue;
        if (is_thumbnail_attempted(item->id)) continue;

        ThumbCacheEntry *slot = &s_thumb_cache[s_thumb_next];
        if (slot->tex) {
            vita2d_free_texture(slot->tex);
            slot->tex = NULL;
        }
        snprintf(slot->video_id, sizeof(slot->video_id), "%s", item->id);
        slot->attempted = 1;
        s_thumb_next = (s_thumb_next + 1) % THUMB_CACHE_SIZE;

        HttpResponse resp;
        if (http_get(item->thumbnail_url, &resp) == 0 && resp.data && resp.size > 0) {
            slot->tex = vita2d_load_JPEG_buffer(resp.data, (unsigned long)resp.size);
        }
        http_response_free(&resp);
        return; /* Load at most 1 thumbnail per frame to keep UI responsive */
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

void ui_draw_search_bar(const char *query, int is_search_mode) {
    /* Search & Direct Video ID input box (x = 20..750, y = 38..72) */
    vita2d_draw_rectangle(20, 38, 730, 34, RGBA8(8, 15, 28, 240));
    vita2d_draw_line(20, 38, 750, 38, RGBA8(0, 174, 239, 190));
    vita2d_draw_line(20, 72, 750, 72, RGBA8(0, 174, 239, 190));
    vita2d_draw_line(20, 38, 20, 72, RGBA8(0, 174, 239, 190));
    vita2d_draw_line(750, 38, 750, 72, RGBA8(0, 174, 239, 190));

    /* Magnifying glass icon */
    vita2d_draw_fill_circle(37, 53, 6.0f, COLOR_BRAND_CYAN);
    vita2d_draw_fill_circle(37, 53, 3.5f, RGBA8(8, 15, 28, 255));
    vita2d_draw_line(41, 57, 46, 62, COLOR_BRAND_CYAN);

    if (query && query[0] != '\0') {
        char display_q[72];
        size_t qlen = strlen(query);
        if (qlen <= 64) {
            snprintf(display_q, sizeof(display_q), "%s", query);
        } else {
            memcpy(display_q, query, 64);
            memcpy(display_q + 64, "...", 4);
        }
        ui_draw_text(54, 61, COLOR_TEXT_LIGHT, 15, display_q);
    } else {
        ui_draw_text(54, 61, COLOR_TEXT_DIM, 14,
                     "Search YouTube or enter 11-char Video ID (Tap or press \xE2\x96\xA1 SQUARE)...");
    }

    /* Search / Direct ID button (x = 758..858, y = 38..72) */
    draw_button_box(758.0f, 38.0f, 100.0f, 34.0f,
                    RGBA8(0, 130, 195, 235), COLOR_BRAND_CYAN,
                    "Search / ID", COLOR_TEXT_LIGHT, 13);

    /* Reset to Trending / Refresh button (x = 866..940, y = 38..72) */
    if (is_search_mode) {
        draw_button_box(866.0f, 38.0f, 74.0f, 34.0f,
                        RGBA8(217, 119, 6, 235), COLOR_HIGHLIGHT_AMBER,
                        "Trending", COLOR_TEXT_LIGHT, 13);
    } else {
        draw_button_box(866.0f, 38.0f, 74.0f, 34.0f,
                        RGBA8(24, 44, 76, 235), RGBA8(65, 105, 165, 255),
                        "Refresh", COLOR_TEXT_LIGHT, 13);
    }
}

void ui_draw_video_card(float x, float y, float w, float h, const VideoItem *item, int is_focused) {
    if (!item) return;

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

    vita2d_texture *thumb = ui_get_cached_thumbnail(item->id);
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

    /* Channel button badge only */
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

        /* Top fading title overlay */
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

        /* Bottom fading playback controls + Description button with dedicated Square symbol */
        vita2d_draw_rectangle(0, 444, SCREEN_WIDTH, 100, RGBA8(8, 14, 26, bar_a));
        vita2d_draw_line(0, 444, SCREEN_WIDTH, 444, RGBA8(40, 68, 110, (uint8_t)(hud_alpha * 200.0f)));

        /* Seek bar */
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

        /* Elapsed / Total time */
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

        /* Tactile playback buttons */
        draw_button_box(240.0f, 492.0f, 104.0f, 38.0f,
                        RGBA8(22, 42, 74, bar_a), RGBA8(75, 115, 175, a),
                        "<< -10s (L)", RGBA8(245, 245, 248, a), 13);

        draw_button_box(356.0f, 492.0f, 124.0f, 38.0f,
                        RGBA8(0, 125, 195, bar_a), RGBA8(0, 174, 239, a),
                        is_paused ? "Play (X)" : "Pause (X)", RGBA8(245, 245, 248, a), 14);

        draw_button_box(492.0f, 492.0f, 104.0f, 38.0f,
                        RGBA8(22, 42, 74, bar_a), RGBA8(75, 115, 175, a),
                        "+10s (R) >>", RGBA8(245, 245, 248, a), 13);

        /* Description button: with dedicated PlayStation Square icon symbol \xE2\x96\xA1 [□] */
        draw_button_box(612.0f, 492.0f, 160.0f, 38.0f,
                        RGBA8(205, 45, 120, bar_a), RGBA8(255, 120, 190, a),
                        "Description \xE2\x96\xA1", RGBA8(245, 245, 248, a), 13);

        /* Dedicated vector Square symbol outline icon on Description button */
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

    /* Dedicated Square symbol icon in header title */
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

    char line_buf[84];
    while (start < len && drawn < max_visible) {
        int take = 0;
        while (take < 76 && (start + take) < len && desc[start + take] != '\n') {
            take++;
        }
        if (line_idx >= scroll_line) {
            if (take >= (int)sizeof(line_buf)) take = (int)sizeof(line_buf) - 1;
            memcpy(line_buf, desc + start, (size_t)take);
            line_buf[take] = '\0';
            ui_draw_text(mx + 36, my + 146 + (drawn * 23), COLOR_TEXT_LIGHT, 13, line_buf);
            drawn++;
        }
        start += take;
        if (start < len && desc[start] == '\n') start++;
        line_idx++;
    }

    vita2d_draw_line(mx + 24, my + mh - 44, mx + mw - 24, my + mh - 44, RGBA8(30, 50, 80, 255));
    ui_draw_text(mx + 24, my + mh - 18, COLOR_TEXT_DIM, 14,
                 "D-Pad Up/Down: Scroll   |   Press SQUARE \xE2\x96\xA1 or CIRCLE to Close Description");
}

void ui_draw_channel_modal(const VideoItem *item) {
    if (!item) return;

    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(0, 0, 0, 180));

    float mx = 160.0f;
    float my = 90.0f;
    float mw = 640.0f;
    float mh = 360.0f;

    vita2d_draw_rectangle(mx + 6, my + 8, mw, mh, RGBA8(0, 0, 0, 140));
    vita2d_draw_rectangle(mx, my, mw, mh, RGBA8(12, 20, 36, 245));

    vita2d_draw_rectangle(mx, my, mw, 3, RGBA8(16, 185, 129, 255));
    vita2d_draw_line(mx, my, mx, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx + mw, my, mx + mw, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx, my + mh, mx + mw, my + mh, RGBA8(40, 65, 105, 255));

    ui_draw_text(mx + 24, my + 36, RGBA8(52, 211, 153, 255), 20, "Channel Information");
    vita2d_draw_line(mx + 24, my + 48, mx + mw - 24, my + 48, RGBA8(30, 50, 80, 255));

    ui_draw_text(mx + 24, my + 90, COLOR_TEXT_LIGHT, 22, item->author);

    char id_buf[128];
    snprintf(id_buf, sizeof(id_buf), "Channel ID:  %s", (strlen(item->author_id) > 0) ? item->author_id : "N/A");
    ui_draw_text(mx + 24, my + 130, COLOR_BRAND_CYAN, 14, id_buf);

    char url_buf[160];
    snprintf(url_buf, sizeof(url_buf), "Instance Path:  %s", (strlen(item->author_url) > 0) ? item->author_url : "/channel");
    ui_draw_text(mx + 24, my + 160, COLOR_TEXT_DIM, 14, url_buf);

    vita2d_draw_rectangle(mx + 24, my + 190, mw - 48, 85, RGBA8(6, 10, 18, 240));
    ui_draw_text(mx + 40, my + 224, COLOR_TEXT_LIGHT, 14, "Press CROSS to play this creator's video.");
    ui_draw_text(mx + 40, my + 252, COLOR_HIGHLIGHT_AMBER, 13, "Powered by PocketVideo.");

    vita2d_draw_line(mx + 24, my + mh - 44, mx + mw - 24, my + mh - 44, RGBA8(30, 50, 80, 255));
    ui_draw_text(mx + 24, my + mh - 18, COLOR_TEXT_DIM, 14, "Press CIRCLE to Close   |   Press CROSS to Play Video");
}

void ui_draw_corner_tab(float peel_progress) {
    (void)peel_progress;
}
