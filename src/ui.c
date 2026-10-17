#include "ui.h"
#include <stdio.h>
#include <string.h>

vita2d_texture *g_tex_bg           = NULL;
vita2d_texture *g_tex_bubble_glass = NULL;

vita2d_font *g_font_noto           = NULL;
vita2d_pvf  *g_font_pvf_fallback   = NULL;

int ui_init(void) {
    vita2d_init();
    vita2d_set_clear_color(COLOR_BRAND_NAVY);

    // 1. Load Noto Sans TrueType font
    g_font_noto = vita2d_load_font_file("app0:assets/fonts/NotoSans-Regular.ttf");
    if (!g_font_noto) {
        g_font_noto = vita2d_load_font_file("assets/fonts/NotoSans-Regular.ttf");
    }

    if (g_font_noto) {
        printf("[UI] Noto Sans font loaded successfully!\n");
    } else {
        printf("[UI] Loading system font fallback...\n");
        g_font_pvf_fallback = vita2d_load_default_pvf();
    }

    // 2. Load UI Textures (bg.png & bubble_glass.png)
    g_tex_bg = vita2d_load_PNG_file("app0:assets/textures/bg.png");
    if (!g_tex_bg) g_tex_bg = vita2d_load_PNG_file("assets/textures/bg.png");

    g_tex_bubble_glass = vita2d_load_PNG_file("app0:assets/textures/bubble_glass.png");
    if (!g_tex_bubble_glass) g_tex_bubble_glass = vita2d_load_PNG_file("assets/textures/bubble_glass.png");

    printf("[UI] Engine initialized at 960x544 with formatted textures.\n");
    return 0;
}

void ui_term(void) {
    if (g_font_noto)         { vita2d_free_font(g_font_noto);            g_font_noto = NULL; }
    if (g_font_pvf_fallback) { vita2d_free_pvf(g_font_pvf_fallback);    g_font_pvf_fallback = NULL; }
    if (g_tex_bg)            { vita2d_free_texture(g_tex_bg);            g_tex_bg = NULL; }
    if (g_tex_bubble_glass)  { vita2d_free_texture(g_tex_bubble_glass);  g_tex_bubble_glass = NULL; }
    vita2d_fini();
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

void ui_draw_status_bar(const char *title) {
    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 32, RGBA8(8, 14, 24, 235));
    vita2d_draw_line(0, 32, SCREEN_WIDTH, 32, RGBA8(35, 55, 90, 255));

    ui_draw_text(16, 22, COLOR_BRAND_CYAN, 16, "PocketVideo (invidious.f5.si)");

    if (title) {
        ui_draw_text(380, 22, COLOR_TEXT_LIGHT, 17, title);
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

    // Drop shadow
    vita2d_draw_fill_circle(cx + 3, cy + 6, r + 2, COLOR_BUBBLE_SHADOW);

    // Sphere base
    vita2d_draw_fill_circle(cx, cy, r, base_col);

    // Glass specular overlay texture
    if (g_tex_bubble_glass) {
        float gw = (float)vita2d_texture_get_width(g_tex_bubble_glass);
        float scale = (r * 2.0f) / gw;
        vita2d_draw_texture_scale(g_tex_bubble_glass, cx - r, cy - r, scale, scale);
    } else {
        vita2d_draw_fill_circle(cx - (r * 0.22f), cy - (r * 0.25f), r * 0.42f, COLOR_GLASS_SHEEN);
    }

    if (is_selected) {
        vita2d_draw_line(cx - r - 4, cy, cx + r + 4, cy, COLOR_HIGHLIGHT_AMBER);
    }

    if (title) {
        float text_x = cx - (float)(strlen(title) * 4.5f);
        ui_draw_text(text_x, cy + r + 24.0f, COLOR_TEXT_LIGHT, 16, title);
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

    float thumb_w = 120.0f;
    float thumb_h = 68.0f;
    vita2d_draw_rectangle(x + 8, y + 8, thumb_w, thumb_h, RGBA8(4, 8, 14, 255));

    char dur[16];
    format_duration(item->length_seconds, dur, sizeof(dur));
    vita2d_draw_rectangle(x + thumb_w - 42, y + thumb_h - 10, 46, 16, RGBA8(0, 0, 0, 220));
    ui_draw_text(x + thumb_w - 38, y + thumb_h + 3, COLOR_TEXT_LIGHT, 12, dur);

    char truncated_title[54];
    snprintf(truncated_title, sizeof(truncated_title), "%.48s%s", item->title, strlen(item->title) > 48 ? "..." : "");
    ui_draw_text(x + thumb_w + 18, y + 26, is_focused ? COLOR_BRAND_CYAN : COLOR_TEXT_LIGHT, 17, truncated_title);

    char meta_info[128];
    snprintf(meta_info, sizeof(meta_info), "%s  *  %lld views  *  %s",
             item->author, item->view_count, item->published_text);
    ui_draw_text(x + thumb_w + 18, y + 52, COLOR_TEXT_DIM, 14, meta_info);

    // Description & Channel tactile button badges
    // [■] Description button badge
    unsigned int desc_btn_bg = is_focused ? RGBA8(236, 72, 153, 230) : RGBA8(180, 50, 115, 160);
    unsigned int desc_btn_border = is_focused ? RGBA8(255, 120, 190, 255) : RGBA8(130, 40, 85, 200);
    vita2d_draw_rectangle(x + w - 225, y + 24, 102, 28, desc_btn_bg);
    vita2d_draw_line(x + w - 225, y + 24, x + w - 123, y + 24, desc_btn_border);
    vita2d_draw_line(x + w - 225, y + 52, x + w - 123, y + 52, desc_btn_border);
    vita2d_draw_line(x + w - 225, y + 24, x + w - 225, y + 52, desc_btn_border);
    vita2d_draw_line(x + w - 123, y + 24, x + w - 123, y + 52, desc_btn_border);
    ui_draw_text(x + w - 216, y + 43, COLOR_TEXT_LIGHT, 12, "[■] Description");

    // [▲] Channel button badge
    unsigned int chan_btn_bg = is_focused ? RGBA8(16, 185, 129, 230) : RGBA8(12, 130, 90, 160);
    unsigned int chan_btn_border = is_focused ? RGBA8(52, 211, 153, 255) : RGBA8(10, 95, 68, 200);
    vita2d_draw_rectangle(x + w - 115, y + 24, 102, 28, chan_btn_bg);
    vita2d_draw_line(x + w - 115, y + 24, x + w - 13, y + 24, chan_btn_border);
    vita2d_draw_line(x + w - 115, y + 52, x + w - 13, y + 52, chan_btn_border);
    vita2d_draw_line(x + w - 115, y + 24, x + w - 115, y + 52, chan_btn_border);
    vita2d_draw_line(x + w - 13, y + 24, x + w - 13, y + 52, chan_btn_border);
    ui_draw_text(x + w - 106, y + 43, COLOR_TEXT_LIGHT, 12, "[▲] Channel");
}

void ui_draw_description_modal(const VideoItem *item) {
    if (!item) return;

    // Dark backdrop overlay
    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(0, 0, 0, 180));

    // Modal dialog box (760x420 centered)
    float mx = 100.0f;
    float my = 60.0f;
    float mw = 760.0f;
    float mh = 424.0f;

    // Outer shadow & background
    vita2d_draw_rectangle(mx + 6, my + 8, mw, mh, RGBA8(0, 0, 0, 140));
    vita2d_draw_rectangle(mx, my, mw, mh, RGBA8(12, 20, 36, 245));

    // Border with cyan accent
    vita2d_draw_rectangle(mx, my, mw, 3, COLOR_BRAND_CYAN);
    vita2d_draw_line(mx, my, mx, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx + mw, my, mx + mw, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx, my + mh, mx + mw, my + mh, RGBA8(40, 65, 105, 255));

    // Header title
    ui_draw_text(mx + 24, my + 34, COLOR_BRAND_CYAN, 20, "Video Description & Metadata");
    vita2d_draw_line(mx + 24, my + 46, mx + mw - 24, my + 46, RGBA8(30, 50, 80, 255));

    // Video Title
    char truncated_title[70];
    snprintf(truncated_title, sizeof(truncated_title), "%.64s%s", item->title, strlen(item->title) > 64 ? "..." : "");
    ui_draw_text(mx + 24, my + 76, COLOR_TEXT_LIGHT, 17, truncated_title);

    // Channel & metadata
    char meta[160];
    snprintf(meta, sizeof(meta), "Channel: %s   |   Views: %lld   |   Published: %s",
             item->author, item->view_count, item->published_text);
    ui_draw_text(mx + 24, my + 104, COLOR_HIGHLIGHT_AMBER, 14, meta);

    // Description text box
    vita2d_draw_rectangle(mx + 24, my + 120, mw - 48, 225, RGBA8(6, 10, 18, 240));

    // Render wrapped lines of description
    const char *desc = (strlen(item->description) > 0) ? item->description : "No description provided.";
    int line_num = 0;
    int line_max = 9;
    char line_buf[80];
    int len = strlen(desc);
    int start = 0;

    while (start < len && line_num < line_max) {
        int take = 74;
        if (start + take > len) take = len - start;
        snprintf(line_buf, sizeof(line_buf), "%.*s", take, desc + start);
        ui_draw_text(mx + 36, my + 148 + (line_num * 22), COLOR_TEXT_LIGHT, 13, line_buf);
        start += take;
        line_num++;
    }

    // Modal footer button instructions
    vita2d_draw_line(mx + 24, my + mh - 44, mx + mw - 24, my + mh - 44, RGBA8(30, 50, 80, 255));
    ui_draw_text(mx + 24, my + mh - 18, COLOR_TEXT_DIM, 14, "Press [● CIRCLE] to Close   |   Press [✕ CROSS] to Play Video");
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

    vita2d_draw_rectangle(mx, my, mw, 3, RGBA8(16, 185, 129, 255)); // Green accent for Channel
    vita2d_draw_line(mx, my, mx, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx + mw, my, mx + mw, my + mh, RGBA8(40, 65, 105, 255));
    vita2d_draw_line(mx, my + mh, mx + mw, my + mh, RGBA8(40, 65, 105, 255));

    ui_draw_text(mx + 24, my + 36, RGBA8(52, 211, 153, 255), 20, "Channel Information");
    vita2d_draw_line(mx + 24, my + 48, mx + mw - 24, my + 48, RGBA8(30, 50, 80, 255));

    // Channel Name
    ui_draw_text(mx + 24, my + 90, COLOR_TEXT_LIGHT, 22, item->author);

    // Channel ID
    char id_buf[128];
    snprintf(id_buf, sizeof(id_buf), "Channel ID:  %s", (strlen(item->author_id) > 0) ? item->author_id : "N/A");
    ui_draw_text(mx + 24, my + 130, COLOR_BRAND_CYAN, 14, id_buf);

    // Channel URL
    char url_buf[160];
    snprintf(url_buf, sizeof(url_buf), "Instance Path:  %s", (strlen(item->author_url) > 0) ? item->author_url : "/channel");
    ui_draw_text(mx + 24, my + 160, COLOR_TEXT_DIM, 14, url_buf);

    // Info card box
    vita2d_draw_rectangle(mx + 24, my + 190, mw - 48, 85, RGBA8(6, 10, 18, 240));
    ui_draw_text(mx + 40, my + 224, COLOR_TEXT_LIGHT, 14, "Browse all videos and uploads published by this creator.");
    ui_draw_text(mx + 40, my + 252, COLOR_HIGHLIGHT_AMBER, 13, "Powered by remote Invidious channel endpoints.");

    vita2d_draw_line(mx + 24, my + mh - 44, mx + mw - 24, my + mh - 44, RGBA8(30, 50, 80, 255));
    ui_draw_text(mx + 24, my + mh - 18, COLOR_TEXT_DIM, 14, "Press [● CIRCLE] to Close   |   Press [✕ CROSS] to Browse Channel");
}

void ui_draw_corner_tab(float peel_progress) {
    float size = 44.0f + (peel_progress * 30.0f);
    vita2d_draw_line(SCREEN_WIDTH - size, 0, SCREEN_WIDTH, size, RGBA8(255, 255, 255, 220));
    ui_draw_text(SCREEN_WIDTH - 38, 22, COLOR_BRAND_CYAN, 12, "TAB");
}
