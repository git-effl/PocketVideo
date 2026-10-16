/**
 * PocketVideo - Invidious Video Client
 * Target API: https://invidious.f5.si/api/v1/trending
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/appmgr.h>
#endif

#include "network.h"
#include "invidious.h"
#include "ui.h"

static VideoList g_trending;
static int g_selected_index = 0;
static int g_network_ready = 0;
static char g_status_msg[256] = "Initializing PocketVideo...";
static AppView g_current_view = VIEW_HOME_BUBBLES;
static int g_bubble_selected = 0;

int main(int argc, char *argv[]) {
    printf("=========================================\n");
    printf("  PocketVideo - Invidious Video Client   \n");
    printf("  Target API: %s\n", INVIDIOUS_DEFAULT_HOST);
    printf("=========================================\n");

#ifdef __vita__
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
#endif

    // 1. Initialize Network Subsystem
    printf("[Main] Initializing network...\n");
    int net_err = net_init();
    if (net_err != 0) {
        snprintf(g_status_msg, sizeof(g_status_msg), "Network Init Failed (Error: 0x%08X)", net_err);
    } else {
        g_network_ready = 1;
        snprintf(g_status_msg, sizeof(g_status_msg), "Network ready. Fetching trending...");
    }

    // 2. Fetch Trending Videos from https://invidious.f5.si/api/v1/trending
    if (g_network_ready) {
        printf("[Main] Contacting %s/api/v1/trending...\n", INVIDIOUS_DEFAULT_HOST);
        int count = invidious_fetch_trending(INVIDIOUS_DEFAULT_HOST, &g_trending);
        if (count > 0) {
            snprintf(g_status_msg, sizeof(g_status_msg), "Trending loaded: %d videos", count);
            printf("\n--------------------------------------------------\n");
            printf("  TRENDING VIDEOS (%s)\n", INVIDIOUS_DEFAULT_HOST);
            printf("--------------------------------------------------\n");
            for (int i = 0; i < count; i++) {
                char duration[16];
                format_duration(g_trending.items[i].length_seconds, duration, sizeof(duration));
                printf("[%02d] %s\n", i + 1, g_trending.items[i].title);
                printf("     Author: %s | Duration: %s | Views: %lld | ID: %s\n\n",
                       g_trending.items[i].author,
                       duration,
                       g_trending.items[i].view_count,
                       g_trending.items[i].id);
            }
            printf("--------------------------------------------------\n\n");
        } else {
            snprintf(g_status_msg, sizeof(g_status_msg), "Fetch failed (Code: %d). Check Connection.", count);
        }
    }

    // 3. Initialize 2D Graphics Engine & Load Assets (Noto Sans + Textures)
    ui_init();

    // 4. Main Event & Render Loop
    int running = 1;
    while (running) {
#ifdef __vita__
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(0, &pad, 1);

        if ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_SELECT)) {
            running = 0;
            break;
        }

        if (g_current_view == VIEW_HOME_BUBBLES) {
            if (pad.buttons & SCE_CTRL_RIGHT) {
                if (g_bubble_selected < 2) { g_bubble_selected++; sceKernelDelayThread(150000); }
            } else if (pad.buttons & SCE_CTRL_LEFT) {
                if (g_bubble_selected > 0) { g_bubble_selected--; sceKernelDelayThread(150000); }
            } else if (pad.buttons & SCE_CTRL_CROSS) {
                if (g_bubble_selected == 0) g_current_view = VIEW_TRENDING_LIST;
                sceKernelDelayThread(200000);
            }
        } else if (g_current_view == VIEW_TRENDING_LIST) {
            if (pad.buttons & SCE_CTRL_DOWN) {
                if (g_selected_index < g_trending.count - 1) {
                    g_selected_index++;
                    sceKernelDelayThread(150000);
                }
            } else if (pad.buttons & SCE_CTRL_UP) {
                if (g_selected_index > 0) {
                    g_selected_index--;
                    sceKernelDelayThread(150000);
                }
            } else if (pad.buttons & SCE_CTRL_CIRCLE) {
                g_current_view = VIEW_HOME_BUBBLES;
                sceKernelDelayThread(200000);
            } else if (pad.buttons & SCE_CTRL_TRIANGLE) {
                invidious_fetch_trending(INVIDIOUS_DEFAULT_HOST, &g_trending);
            }
        }
#endif

        vita2d_start_drawing();
        vita2d_clear_screen();

        // 1. Draw Textured Background
        ui_draw_background();

        // 2. Draw Status Bar
        ui_draw_status_bar("PocketVideo");

        if (g_current_view == VIEW_HOME_BUBBLES) {
            ui_draw_text(370, 100, COLOR_TEXT_LIGHT, 24, "POCKETVIDEO");
            ui_draw_text(260, 130, COLOR_TEXT_DIM, 15, "Select a category using D-Pad or Touch screen");

            // Bubbles with icons & glass overlays
            ui_draw_bubble_with_icon(280.0f, 280.0f, 54.0f, g_tex_icon_trending,
                                     "Trending", COLOR_BRAND_BLUE, 0, (g_bubble_selected == 0));
            ui_draw_bubble_with_icon(480.0f, 280.0f, 54.0f, g_tex_icon_search,
                                     "Search", COLOR_BRAND_CYAN, 0, (g_bubble_selected == 1));
            ui_draw_bubble_with_icon(680.0f, 280.0f, 54.0f, g_tex_icon_settings,
                                     "Instance", RGBA8(180, 80, 220, 255), 0, (g_bubble_selected == 2));

            ui_draw_text(340, 480, COLOR_HIGHLIGHT_AMBER, 16, "Press CROSS to enter Trending feed");
        } else if (g_current_view == VIEW_TRENDING_LIST) {
            if (g_trending.count > 0) {
                float start_y = 44.0f;
                float card_h = 84.0f;
                int visible_items = 5;
                int offset = g_selected_index - 2;
                if (offset < 0) offset = 0;
                if (offset > g_trending.count - visible_items) offset = g_trending.count - visible_items;
                if (offset < 0) offset = 0;

                for (int i = 0; i < visible_items && (offset + i) < g_trending.count; i++) {
                    int item_idx = offset + i;
                    int is_focused = (item_idx == g_selected_index);
                    ui_draw_video_card(20.0f, start_y + (i * (card_h + 8.0f)), 920.0f, card_h,
                                       &g_trending.items[item_idx], is_focused);
                }
            } else {
                ui_draw_text(360, 270, COLOR_TEXT_LIGHT, 18, g_status_msg);
            }
        }

        // 3. Draw Corner Tab
        ui_draw_corner_tab(0.0f);

        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
    }

    // 5. Cleanup
    printf("[Main] Exiting PocketVideo...\n");
    ui_term();
    net_term();

#ifdef __vita__
    sceKernelExitProcess(0);
#endif

    return 0;
}
