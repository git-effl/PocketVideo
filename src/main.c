/**
 * PocketVideo - PlayStation Vita Invidious Client
 * 
 * Target: PlayStation Vita (VitaSDK arm-vita-eabi-gcc)
 * Endpoint: https://invidious.f5.si/api/v1/trending
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
static int g_bubble_selected = 0; // 0=Trending, 1=Search, 2=Settings

int main(int argc, char *argv[]) {
    printf("=========================================\n");
    printf("  PocketVideo - PS Vita Invidious Client \n");
    printf("  Target API: %s\n", INVIDIOUS_DEFAULT_HOST);
    printf("=========================================\n");

#ifdef __vita__
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
#endif

    // 1. Initialize Network Subsystem (sceNet, sceSsl, cURL)
    printf("[Main] Initializing network...\n");
    int net_err = net_init();
    if (net_err != 0) {
        snprintf(g_status_msg, sizeof(g_status_msg), "Network Init Failed (Error: 0x%08X)", net_err);
        printf("[Main] ERROR: %s\n", g_status_msg);
    } else {
        g_network_ready = 1;
        snprintf(g_status_msg, sizeof(g_status_msg), "Network ready. Fetching trending...");
    }

    // 2. Fetch Trending Videos from Invidious instance (https://invidious.f5.si/api/v1/trending)
    if (g_network_ready) {
        printf("[Main] Contacting %s/api/v1/trending...\n", INVIDIOUS_DEFAULT_HOST);
        int count = invidious_fetch_trending(INVIDIOUS_DEFAULT_HOST, &g_trending);

        if (count > 0) {
            snprintf(g_status_msg, sizeof(g_status_msg), "Trending loaded: %d videos", count);
            printf("\n--------------------------------------------------\n");
            printf("  TRENDING VIDEOS ON INVIDIOUS (%s)\n", INVIDIOUS_DEFAULT_HOST);
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
            snprintf(g_status_msg, sizeof(g_status_msg), "Fetch failed (Code: %d). Check Wi-Fi.", count);
            printf("[Main] WARNING: Failed to fetch trending list. Error code: %d\n", count);
        }
    }

    // 3. Initialize vita2d 2D Graphics Engine & Load Assets (Noto Sans + Textures)
    ui_init();

    // 4. Main Event & Render Loop
    int running = 1;
    while (running) {
#ifdef __vita__
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(0, &pad, 1);

        // Exit on START + SELECT
        if ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_SELECT)) {
            running = 0;
            break;
        }

        // Navigation controls
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

        // Render Frame
        vita2d_start_drawing();
        vita2d_clear_screen();

        // 1. Draw Textured Background
        ui_draw_background();

        // 2. Draw 2012 PlayStation Vita Status Bar
        ui_draw_status_bar("PocketVideo - Invidious Client");

        if (g_current_view == VIEW_HOME_BUBBLES) {
            // LiveArea Bubble Grid Screen
            ui_draw_text(300, 100, COLOR_TEXT_LIGHT, 24, "POCKETVIDEO LIVEAREA");
            ui_draw_text(260, 130, COLOR_TEXT_DIM, 15, "Select a category using D-Pad or Touch screen");

            // Bubble 1: Trending Videos
            ui_draw_bubble_with_icon(280.0f, 280.0f, 54.0f, g_tex_icon_trending,
                                     "Trending", COLOR_VITA_BLUE, 0, (g_bubble_selected == 0));

            // Bubble 2: Search Videos
            ui_draw_bubble_with_icon(480.0f, 280.0f, 54.0f, g_tex_icon_search,
                                     "Search", COLOR_VITA_CYAN, 0, (g_bubble_selected == 1));

            // Bubble 3: Instance Settings
            ui_draw_bubble_with_icon(680.0f, 280.0f, 54.0f, g_tex_icon_settings,
                                     "Instance", RGBA8(180, 80, 220, 255), 0, (g_bubble_selected == 2));

            // Hint
            ui_draw_text(330, 480, COLOR_HIGHLIGHT_AMBER, 16, "Press CROSS to enter Trending feed");
        } else if (g_current_view == VIEW_TRENDING_LIST) {
            // Video List View
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

        // 3. Draw Peelable LiveArea tab
        ui_draw_livearea_peel(0.0f);

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
