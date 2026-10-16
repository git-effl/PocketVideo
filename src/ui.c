#include "ui.h"
#include <stdio.h>
#include <string.h>

// Global asset texture pointers
vita2d_texture *g_tex_bg             = NULL;
vita2d_texture *g_tex_bubble_glass   = NULL;
vita2d_texture *g_tex_icon_trending  = NULL;
vita2d_texture *g_tex_icon_search    = NULL;
vita2d_texture *g_tex_icon_settings  = NULL;
vita2d_texture *g_tex_icon_play      = NULL;
vita2d_texture *g_tex_icon_pause     = NULL;

// Global typography pointers
vita2d_font *g_font_noto             = NULL;
vita2d_pvf  *g_font_pvf_fallback     = NULL;

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

    // 2. Load UI Textures & Icons (using bg.png)
    g_tex_bg = vita2d_load_PNG_file("app0:assets/textures/bg.png");
    if (!g_tex_bg) g_tex_bg = vita2d_load_PNG_file("assets/textures/bg.png");

    g_tex_bubble_glass = vita2d_load_PNG_file("app0:assets/textures/bubble_glass.png");
    if (!g_tex_bubble_glass) g_tex_bubble_glass = vita2d_load_PNG_file("assets/textures/bubble_glass.png");

    g_tex_icon_trending = vita2d_load_PNG_file("app0:assets/icons/trending.png");
    if (!g_tex_icon_trending) g_tex_icon_trending = vita2d_load_PNG_file("assets/icons/trending.png");

    g_tex_icon_search = vita2d_load_PNG_file("app0:assets/icons/search.png");
    if (!g_tex_icon_search) g_tex_icon_search = vita2d_load_PNG_file("assets/icons/search.png");

    g_tex_icon_settings = vita2d_load_PNG_file("app0:assets/icons/settings.png");
    if (!g_tex_icon_settings) g_tex_icon_settings = vita2d_load_PNG_file("assets/icons/settings.png");

    g_tex_icon_play = vita2d_load_PNG_file("app0:assets/icons/play.png");
    if (!g_tex_icon_play) g_tex_icon_play = vita2d_load_PNG_file("assets/icons/play.png");

    g_tex_icon_pause = vita2d_load_PNG_file("app0:assets/icons/pause.png");
    if (!g_tex_icon_pause) g_tex_icon_pause = vita2d_load_PNG_file("assets/icons/pause.png");

    printf("[UI] Engine initialized at 960x544 with custom assets.\n");
    return 0;
}

void ui_term(void) {
    if (g_font_noto)         { vita2d_free_font(g_font_noto);            g_font_noto = NULL; }
    if (g_font_pvf_fallback) { vita2d_free_pvf(g_font_pvf_fallback);    g_font_pvf_fallback = NULL; }

    if (g_tex_bg)            { vita2d_free_texture(g_tex_bg);            g_tex_bg = NULL; }
    if (g_tex_bubble_glass)  { vita2d_free_texture(g_tex_bubble_glass);  g_tex_bubble_glass = NULL; }
    if (g_tex_icon_trending) { vita2d_free_texture(g_tex_icon_trending); g_tex_icon_trending = NULL; }
    if (g_tex_icon_search)   { vita2d_free_texture(g_tex_icon_search);   g_tex_icon_search = NULL; }
    if (g_tex_icon_settings) { vita2d_free_texture(g_tex_icon_settings); g_tex_icon_settings = NULL; }
    if (g_tex_icon_play)     { vita2d_free_texture(g_tex_icon_play);     g_tex_icon_play = NULL; }
    if (g_tex_icon_pause)    { vita2d_free_texture(g_tex_icon_pause);    g_tex_icon_pause = NULL; }

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
    // Top status bar (32px height)
    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 32, RGBA8(8, 14, 24, 235));
    vita2d_draw_line(0, 32, SCREEN_WIDTH, 32, RGBA8(35, 55, 90, 255));

    // Left: Connection info
    ui_draw_text(16, 22, COLOR_BRAND_CYAN, 16, "Connected (invidious.f5.si)");

    // Center: Title
    if (title) {
        ui_draw_text(380, 22, COLOR_TEXT_LIGHT, 17, title);
    }

    // Right: Battery & Clock
    ui_draw_text(850, 22, COLOR_TEXT_LIGHT, 16, "100%");
    vita2d_draw_rectangle(905, 10, 24, 12, RGBA8(255, 255, 255, 180));
    vita2d_draw_rectangle(907, 12, 20, 8, RGBA8(46, 204, 113, 255));
    vita2d_draw_rectangle(929, 13, 3, 6, RGBA8(255, 255, 255, 180));
}

void ui_draw_bubble_with_icon(float cx, float cy, float radius,
                              vita2d_texture *icon, const char *title,
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

    // Center icon
    if (icon) {
        float iw = (float)vita2d_texture_get_width(icon);
        float ih = (float)vita2d_texture_get_height(icon);
        vita2d_draw_texture(icon, cx - (iw / 2.0f), cy - (ih / 2.0f));
    }

    // Glass specular overlay texture
    if (g_tex_bubble_glass) {
        float gw = (float)vita2d_texture_get_width(g_tex_bubble_glass);
        float scale = (r * 2.0f) / gw;
        vita2d_draw_texture_scale(g_tex_bubble_glass, cx - r, cy - r, scale, scale);
    } else {
        vita2d_draw_fill_circle(cx - (r * 0.22f), cy - (r * 0.25f), r * 0.42f, COLOR_GLASS_SHEEN);
    }

    // Selected rim
    if (is_selected) {
        vita2d_draw_line(cx - r - 4, cy, cx + r + 4, cy, COLOR_HIGHLIGHT_AMBER);
    }

    // Text label
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

    if (g_tex_icon_play) {
        vita2d_draw_texture(g_tex_icon_play, x + 8 + (thumb_w / 2.0f) - 16, y + 8 + (thumb_h / 2.0f) - 16);
    }

    char dur[16];
    format_duration(item->length_seconds, dur, sizeof(dur));
    vita2d_draw_rectangle(x + thumb_w - 42, y + thumb_h - 10, 46, 16, RGBA8(0, 0, 0, 220));
    ui_draw_text(x + thumb_w - 38, y + thumb_h + 3, COLOR_TEXT_LIGHT, 12, dur);

    char truncated_title[60];
    snprintf(truncated_title, sizeof(truncated_title), "%.50s%s", item->title, strlen(item->title) > 50 ? "..." : "");
    ui_draw_text(x + thumb_w + 18, y + 26, is_focused ? COLOR_BRAND_CYAN : COLOR_TEXT_LIGHT, 17, truncated_title);

    char meta_info[128];
    snprintf(meta_info, sizeof(meta_info), "%s  *  %lld views  *  %s",
             item->author, item->view_count, item->published_text);
    ui_draw_text(x + thumb_w + 18, y + 54, COLOR_TEXT_DIM, 14, meta_info);
}

void ui_draw_corner_tab(float peel_progress) {
    float size = 44.0f + (peel_progress * 30.0f);
    vita2d_draw_line(SCREEN_WIDTH - size, 0, SCREEN_WIDTH, size, RGBA8(255, 255, 255, 220));
    ui_draw_text(SCREEN_WIDTH - 38, 22, COLOR_BRAND_CYAN, 12, "TAB");
}
