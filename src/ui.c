#include "ui.h"
#include <stdio.h>
#include <math.h>

static vita2d_pvf *font_default = NULL;

int ui_init(void) {
    vita2d_init();
    vita2d_set_clear_color(COLOR_VITA_NAVY);

    // Load default system font or bundled font
    font_default = vita2d_load_default_pvf();
    if (!font_default) {
        printf("[UI] Warning: Could not load default PVF font.\n");
    }

    printf("[UI] vita2d initialized at 960x544.\n");
    return 0;
}

void ui_term(void) {
    if (font_default) {
        vita2d_free_pvf(font_default);
        font_default = NULL;
    }
    vita2d_fini();
}

void ui_draw_status_bar(const char *title) {
    // 2012 Vita top status bar (32px height)
    // Dark glass bar with subtle bottom rim highlight
    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 32, RGBA8(12, 18, 30, 230));
    vita2d_draw_line(0, 32, SCREEN_WIDTH, 32, RGBA8(40, 60, 95, 255));

    if (font_default) {
        // App title in center
        if (title) {
            vita2d_pvf_draw_text(font_default, 380, 22, COLOR_TEXT_LIGHT, 1.0f, title);
        }
        // PS Vita 2012 Wi-Fi & Battery indicators
        vita2d_pvf_draw_text(font_default, 16, 22, COLOR_VITA_CYAN, 0.9f, "Wi-Fi 72Mbps");
        vita2d_pvf_draw_text(font_default, 870, 22, COLOR_TEXT_LIGHT, 0.9f, "100%");
    }

    // Battery rectangle icon
    vita2d_draw_rectangle(920, 10, 24, 12, RGBA8(255, 255, 255, 180));
    vita2d_draw_rectangle(922, 12, 20, 8, RGBA8(46, 204, 113, 255));
    vita2d_draw_rectangle(944, 13, 3, 6, RGBA8(255, 255, 255, 180));
}

void ui_draw_bubble(const UiBubble *bubble) {
    if (!bubble) return;

    float cx = bubble->x;
    float cy = bubble->y;
    float r = bubble->radius;

    if (bubble->is_pressed) {
        r *= 0.94f; // Tactile depression on touch
    }

    // 1. Outer drop shadow
    vita2d_draw_fill_circle(cx + 3, cy + 6, r + 2, COLOR_BUBBLE_SHADOW);

    // 2. Base vibrant colored sphere
    vita2d_draw_fill_circle(cx, cy, r, bubble->color_base);

    // 3. 2012 Skeuomorphic Glass Specular Highlight (Upper half ellipse)
    vita2d_draw_fill_circle(cx - (r * 0.22f), cy - (r * 0.25f), r * 0.42f, COLOR_VITA_GLASS);

    // 4. Rim glow if focused or selected
    if (bubble->is_selected) {
        vita2d_draw_line(cx - r, cy, cx + r, cy, COLOR_HIGHLIGHT_AMBER);
    }

    // 5. Bubble title label
    if (bubble->title && font_default) {
        vita2d_pvf_draw_text(font_default, cx - (r * 0.65f), cy + r + 22, COLOR_TEXT_LIGHT, 0.95f, bubble->title);
    }
}

void ui_draw_video_card(float x, float y, float w, float h, const VideoItem *item, int is_focused) {
    if (!item) return;

    // Card background with rounded bevel
    unsigned int bg_col = is_focused ? RGBA8(26, 45, 78, 240) : RGBA8(18, 28, 48, 200);
    vita2d_draw_rectangle(x, y, w, h, bg_col);

    // Border
    unsigned int border_col = is_focused ? COLOR_VITA_CYAN : RGBA8(40, 60, 95, 180);
    vita2d_draw_line(x, y, x + w, y, border_col);
    vita2d_draw_line(x, y + h, x + w, y + h, border_col);
    vita2d_draw_line(x, y, x, y + h, border_col);
    vita2d_draw_line(x + w, y, x + w, y + h, border_col);

    // Thumbnail placeholder box (16:9)
    float thumb_w = 120.0f;
    float thumb_h = 68.0f;
    vita2d_draw_rectangle(x + 8, y + 8, thumb_w, thumb_h, RGBA8(8, 12, 20, 255));

    // Text labels
    if (font_default) {
        // Truncate title visually if needed
        char truncated_title[60];
        snprintf(truncated_title, sizeof(truncated_title), "%.54s%s", item->title, strlen(item->title) > 54 ? "..." : "");
        vita2d_pvf_draw_text(font_default, x + thumb_w + 18, y + 26, COLOR_TEXT_LIGHT, 1.0f, truncated_title);

        // Author and views
        char meta_info[128];
        snprintf(meta_info, sizeof(meta_info), "%s  *  %lld views  *  %s",
                 item->author, item->view_count, item->published_text);
        vita2d_pvf_draw_text(font_default, x + thumb_w + 18, y + 54, COLOR_TEXT_DIM, 0.85f, meta_info);

        // Duration badge inside thumbnail box
        char dur[16];
        format_duration(item->length_seconds, dur, sizeof(dur));
        vita2d_draw_rectangle(x + thumb_w - 42, y + thumb_h - 12, 46, 16, RGBA8(0, 0, 0, 210));
        vita2d_pvf_draw_text(font_default, x + thumb_w - 38, y + thumb_h + 1, COLOR_TEXT_LIGHT, 0.75f, dur);
    }
}

void ui_draw_livearea_peel(float peel_progress) {
    // Classic PS Vita LiveArea peelable tab in top right corner
    float size = 44.0f + (peel_progress * 30.0f);
    vita2d_draw_line(SCREEN_WIDTH - size, 0, SCREEN_WIDTH, size, RGBA8(255, 255, 255, 200));
}
