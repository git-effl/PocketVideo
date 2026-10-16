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

typedef enum {
    VIEW_HOME_BUBBLES = 0,
    VIEW_TRENDING_LIST,
    VIEW_SEARCH,
    VIEW_PLAYER,
    VIEW_SETTINGS
} AppView;

typedef struct {
    float x, y;
    float radius;
    const char *title;
    const char *subtitle;
    unsigned int color_base;
    unsigned int color_accent;
    int is_pressed;
    int is_selected;
} UiBubble;

/**
 * Initialize 2D graphics subsystem (vita2d) and load UI fonts/textures.
 */
int ui_init(void);

/**
 * Free UI resources and shutdown vita2d.
 */
void ui_term(void);

/**
 * Render the top status bar (Vita clock, battery status, Wi-Fi icon).
 */
void ui_draw_status_bar(const char *title);

/**
 * Render a tactile 2012-style glossy bubble with radial gradient and bevel highlight.
 */
void ui_draw_bubble(const UiBubble *bubble);

/**
 * Render a video card in the trending list view.
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
