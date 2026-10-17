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

// PocketVideo 2010s tactile console color palette
#define COLOR_BRAND_BLUE      RGBA8(0, 114, 206, 255)
#define COLOR_BRAND_CYAN      RGBA8(0, 174, 239, 255)
#define COLOR_BRAND_NAVY      RGBA8(10, 22, 44, 255)
#define COLOR_GLASS_SHEEN     RGBA8(255, 255, 255, 45)
#define COLOR_BUBBLE_SHADOW   RGBA8(0, 0, 0, 110)
#define COLOR_TEXT_LIGHT      RGBA8(245, 245, 248, 255)
#define COLOR_TEXT_DIM        RGBA8(170, 180, 200, 255)
#define COLOR_HIGHLIGHT_AMBER RGBA8(255, 175, 20, 255)

// Aliases for compatibility
#define COLOR_VITA_BLUE COLOR_BRAND_BLUE
#define COLOR_VITA_CYAN COLOR_BRAND_CYAN
#define COLOR_VITA_NAVY COLOR_BRAND_NAVY

typedef enum {
    VIEW_HOME_BUBBLES = 0,
    VIEW_TRENDING_LIST,
    VIEW_SEARCH,
    VIEW_PLAYER,
    VIEW_SETTINGS,
    VIEW_MODAL_DESCRIPTION,
    VIEW_MODAL_CHANNEL
} AppView;

// Global asset texture pointers
extern vita2d_texture *g_tex_bg;
extern vita2d_texture *g_tex_bubble_glass;

// Global typography pointers
extern vita2d_font *g_font_noto;
extern vita2d_pvf  *g_font_pvf_fallback;

/**
 * Initialize 2D graphics subsystem, load Noto Sans font,
 * and load UI textures (bg.png, bubble_glass.png) from app0:assets/.
 */
int ui_init(void);

/**
 * Free all textures, fonts, and shutdown graphics.
 */
void ui_term(void);

/**
 * Draw background wallpaper using app0:assets/textures/bg.png
 */
void ui_draw_background(void);

/**
 * Unified text drawing helper: uses Noto Sans font if available,
 * otherwise falls back to system font.
 */
void ui_draw_text(float x, float y, unsigned int color, unsigned int size, const char *text);

/**
 * Render the top status bar (Clock, battery status, Wi-Fi indicator).
 */
void ui_draw_status_bar(const char *title);

/**
 * Render a tactile glossy bubble with glass reflection and drop shadow.
 */
void ui_draw_bubble(float cx, float cy, float radius, const char *title,
                    unsigned int base_col, int is_pressed, int is_selected);

/**
 * Render a video card in the trending list view with action button badges.
 */
void ui_draw_video_card(float x, float y, float w, float h, const VideoItem *item, int is_focused);

/**
 * Render tactile modal dialog showing multi-line video description.
 */
void ui_draw_description_modal(const VideoItem *item);

/**
 * Render tactile modal dialog showing channel metadata & author profile.
 */
void ui_draw_channel_modal(const VideoItem *item);

/**
 * Render the tactile corner page tab in the top-right corner.
 */
void ui_draw_corner_tab(float peel_progress);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
