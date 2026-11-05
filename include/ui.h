#ifndef UI_H
#define UI_H

#include <stdint.h>
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

extern vita2d_texture *g_tex_bg;
extern vita2d_texture *g_tex_bubble_glass;
extern vita2d_font *g_font_noto;
extern vita2d_pvf  *g_font_pvf_fallback;

int ui_init(void);
void ui_term(void);

void ui_draw_background(void);
void ui_draw_text(float x, float y, unsigned int color, unsigned int size, const char *text);
int ui_get_text_width(unsigned int size, const char *text);
void ui_draw_text_centered(float center_x, float y, unsigned int color, unsigned int size, const char *text);

void ui_draw_status_bar(const char *title);
void ui_draw_bubble(float cx, float cy, float radius, const char *title,
                    unsigned int base_col, int is_pressed, int is_selected);

/* Search / Direct ID bar at top of Trending tab */
void ui_draw_search_bar(const char *query, int is_search_mode);

/* Thumbnail texture cache helpers */
vita2d_texture *ui_get_cached_thumbnail(const char *video_id);
void ui_fetch_one_visible_thumbnail(const VideoList *list, int offset, int visible_count);
void ui_clear_thumbnails(void);

/* Video card (no Description button on card; Description moves to playback HUD) */
void ui_draw_video_card(float x, float y, float w, float h, const VideoItem *item, int is_focused);

/* Playback view with fading HUD controls + Description button */
void ui_draw_player_screen(const VideoItem *item, vita2d_texture *video_tex,
                           vita2d_texture *fallback_thumb, float hud_alpha,
                           int is_paused, uint64_t cur_ms, uint64_t total_ms,
                           int desc_open, int desc_scroll, const char *player_status);

void ui_draw_description_modal(const VideoItem *item, int scroll_line);
void ui_draw_channel_modal(const VideoItem *item);
void ui_draw_corner_tab(float peel_progress);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
