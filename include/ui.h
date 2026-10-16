#ifndef UI_H
#define UI_H

#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include "invidious.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_WIDTH  960
#define SCREEN_HEIGHT 544

// Authentic 2010s PS Vita color palette
#define COLOR_VITA_BLUE       RGBA8(0, 114, 206, 255)
#define COLOR_VITA_CYAN       RGBA8(0, 174, 239, 255)
#define COLOR_VITA_NAVY       RGBA8(10, 22, 44, 255)
#define COLOR_VITA_GLASS      RGBA8(255, 255, 255, 45)
#define COLOR_BUBBLE_SHADOW   RGBA8(0, 0, 0, 110)
#define COLOR_TEXT_LIGHT      RGBA8(245, 245, 248, 255)
#define COLOR_TEXT_DIM        RGBA8(170, 180, 200, 255)
#define COLOR_HIGHLIGHT_AMBER RGBA8(255, 175, 20, 255)

// Global asset texture pointers
extern vita2d_texture *g_tex_bg;
extern vita2d_texture *g_tex_bubble_glass;
extern vita2d_texture *g_tex_icon_trending;
extern vita2d_texture *g_tex_icon_search;
extern vita2d_texture *g_tex_icon_settings;
extern vita2d_texture *g_tex_icon_play;
extern vita2d_texture *g_tex_icon_pause;

// Global typography pointers
extern vita2d_font *g_font_noto;
extern vita2d_pvf  *g_font_pvf_fallback;

/**
 * Initialize 2D graphics subsystem (vita2d), load Noto Sans font,
 * and load all UI PNG textures from app0:assets/.
 * Returns 0 on success, negative error code on failure.
 */
int ui_init(void);

/**
 * Free all textures, fonts, and shutdown vita2d.
 */
void ui_term(void);

/**
 * Draw background wallpaper using app0:assets/textures/livearea_bg.png
 */
void ui_draw_background(void);

/**
 * Unified text drawing helper: uses Noto Sans (vita2d_font) if available,
 * otherwise falls back to system PVF font.
 */
void ui_draw_text(float x, float y, unsigned int color, unsigned int size, const char *text);

/**
 * Render the top status bar (Vita clock, battery status, Wi-Fi icon).
 */
void ui_draw_status_bar(const char *title);

/**
 * Render an authentic 2012 tactile glossy bubble with embedded icon & glass reflection.
 */
void ui_draw_bubble_with_icon(float cx, float cy, float radius,
                              vita2d_texture *icon, const char *title,
                              unsigned int base_col, int is_pressed, int is_selected);

/**
 * Render a video card in the trending list view using loaded play icon.
 */
void ui_draw_video_card(float x, float y, float w, float h, const VideoItem *item, int is_focused);

/**
 * Render the peelable LiveArea paper tab in the top-right corner.
 */
void ui_draw_livearea_peel(float peel_progress);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
