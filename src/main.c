/**
 * PocketVideo - Invidious Video Client
 * Target API: https://yt.chocolatemoo53.com/api/v1/trending
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/appmgr.h>
#define main_log(fmt, ...) do { sceClibPrintf(fmt, ##__VA_ARGS__); printf(fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define main_log(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
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

static void render_loading_banner(const char *subtitle) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    ui_draw_background();
    ui_draw_status_bar("PocketVideo");
    ui_draw_text_centered(480.0f, 250, COLOR_TEXT_LIGHT, 22, "Connecting to yt.chocolatemoo53.com...");
    if (subtitle) {
        ui_draw_text_centered(480.0f, 286, COLOR_BRAND_CYAN, 15, subtitle);
    }
    vita2d_end_drawing();
    vita2d_wait_rendering_done();
    vita2d_swap_buffers();
}

static void fetch_trending_now(void) {
    if (!g_network_ready) {
        int net_err = net_init();
        if (net_err == 0) {
            g_network_ready = 1;
        } else {
            snprintf(g_status_msg, sizeof(g_status_msg), "Network Init Error: 0x%08X", net_err);
            return;
        }
    }

    main_log("[Main] Contacting %s/api/v1/trending...", INVIDIOUS_DEFAULT_HOST);
    int count = invidious_fetch_trending(INVIDIOUS_DEFAULT_HOST, &g_trending);
    if (count > 0) {
        g_selected_index = 0;
        snprintf(g_status_msg, sizeof(g_status_msg), "Connected: %d trending videos loaded", count);
        main_log("[Main] Loaded %d trending videos.", count);
    } else if (count == 0) {
        snprintf(g_status_msg, sizeof(g_status_msg), "Connected: 0 trending videos returned");
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg), "Connection issue (Code: %d) - Press SQUARE to retry", count);
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    main_log("[Main] Starting PocketVideo (%s)", INVIDIOUS_DEFAULT_HOST);

    setenv("OPENSSL_armcap", "0", 1);
    setenv("OPENSSL_CONF", "app0:assets/openssl.cnf", 1);

#ifdef __vita__
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
#endif

    // 1. Initialize 2D Graphics Engine & Load Assets (Noto Sans + Textures)
    ui_init();
    render_loading_banner("Initializing network...");

    // 2. Initialize Network Subsystem
    int net_err = net_init();
    if (net_err != 0) {
        snprintf(g_status_msg, sizeof(g_status_msg), "Network Init Error: 0x%08X", net_err);
    } else {
        g_network_ready = 1;
        snprintf(g_status_msg, sizeof(g_status_msg), "Network ready. Fetching trending...");
    }

    // 3. Fetch Trending Videos from https://yt.chocolatemoo53.com/api/v1/trending
    if (g_network_ready) {
        render_loading_banner("Fetching trending feed...");
        fetch_trending_now();
    }

    // 4. Main Event & Render Loop
    int running = 1;
    while (running) {
#ifdef __vita__
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(0, &pad, 1);

        SceTouchData touch;
        memset(&touch, 0, sizeof(touch));
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

        if ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_SELECT)) {
            running = 0;
            break;
        }

        // Physical Controller Buttons & Analog Sticks
        int stick_left  = (pad.lx < 60) || (pad.rx < 60);
        int stick_right = (pad.lx > 195) || (pad.rx > 195);
        int stick_up    = (pad.ly < 60) || (pad.ry < 60);
        int stick_down  = (pad.ly > 195) || (pad.ry > 195);

        int nav_left  = (pad.buttons & SCE_CTRL_LEFT)  || stick_left;
        int nav_right = (pad.buttons & SCE_CTRL_RIGHT) || stick_right;
        int nav_up    = (pad.buttons & SCE_CTRL_UP)    || stick_up;
        int nav_down  = (pad.buttons & SCE_CTRL_DOWN)  || stick_down;

        if (g_current_view == VIEW_HOME_BUBBLES) {
            if (nav_right) {
                if (g_bubble_selected < 2) { g_bubble_selected++; sceKernelDelayThread(160000); }
            } else if (nav_left) {
                if (g_bubble_selected > 0) { g_bubble_selected--; sceKernelDelayThread(160000); }
            } else if (pad.buttons & SCE_CTRL_CROSS) {
                if (g_bubble_selected == 0) {
                    if (g_trending.count == 0) {
                        render_loading_banner("Fetching trending feed...");
                        fetch_trending_now();
                    }
                    g_current_view = VIEW_TRENDING_LIST;
                } else if (g_bubble_selected == 1) {
                    g_current_view = VIEW_SEARCH;
                } else if (g_bubble_selected == 2) {
                    g_current_view = VIEW_SETTINGS;
                }
                sceKernelDelayThread(200000);
            }
        } else if (g_current_view == VIEW_TRENDING_LIST) {
            if (nav_down) {
                if (g_selected_index < g_trending.count - 1) {
                    g_selected_index++;
                    sceKernelDelayThread(150000);
                }
            } else if (nav_up) {
                if (g_selected_index > 0) {
                    g_selected_index--;
                    sceKernelDelayThread(150000);
                }
            } else if (pad.buttons & SCE_CTRL_CIRCLE) {
                g_current_view = VIEW_HOME_BUBBLES;
                sceKernelDelayThread(200000);
            } else if (pad.buttons & SCE_CTRL_SQUARE) {
                if (g_trending.count > 0) {
                    g_current_view = VIEW_MODAL_DESCRIPTION;
                } else {
                    render_loading_banner("Retrying trending feed...");
                    fetch_trending_now();
                }
                sceKernelDelayThread(200000);
            } else if (pad.buttons & SCE_CTRL_TRIANGLE) {
                if (g_trending.count > 0) {
                    g_current_view = VIEW_MODAL_CHANNEL;
                    sceKernelDelayThread(200000);
                }
            } else if (pad.buttons & SCE_CTRL_CROSS) {
                if (g_trending.count > 0) {
                    main_log("[Main] Selected video: [%s] %s",
                             g_trending.items[g_selected_index].id,
                             g_trending.items[g_selected_index].title);
                    g_current_view = VIEW_MODAL_DESCRIPTION;
                    sceKernelDelayThread(200000);
                }
            }
        } else if (g_current_view == VIEW_SEARCH || g_current_view == VIEW_SETTINGS) {
            if (pad.buttons & SCE_CTRL_CIRCLE) {
                g_current_view = VIEW_HOME_BUBBLES;
                sceKernelDelayThread(200000);
            } else if (pad.buttons & SCE_CTRL_CROSS) {
                render_loading_banner("Refreshing feed from yt.chocolatemoo53.com...");
                fetch_trending_now();
                if (g_trending.count > 0) {
                    g_current_view = VIEW_TRENDING_LIST;
                }
                sceKernelDelayThread(200000);
            }
        } else if (g_current_view == VIEW_MODAL_DESCRIPTION || g_current_view == VIEW_MODAL_CHANNEL) {
            if (pad.buttons & (SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE)) {
                g_current_view = VIEW_TRENDING_LIST;
                sceKernelDelayThread(200000);
            } else if (pad.buttons & SCE_CTRL_CROSS) {
                g_current_view = VIEW_TRENDING_LIST;
                sceKernelDelayThread(200000);
            }
        }

        // Front Touchscreen Tap Handling
        if (touch.reportNum > 0) {
            float tx = (float)touch.report[0].x * (960.0f / 1920.0f);
            float ty = (float)touch.report[0].y * (544.0f / 1088.0f);

            if (g_current_view == VIEW_HOME_BUBBLES) {
                if (ty >= 220.0f && ty <= 340.0f) {
                    if (tx >= 220.0f && tx <= 340.0f) {
                        g_bubble_selected = 0;
                        if (g_trending.count == 0) {
                            render_loading_banner("Fetching trending feed...");
                            fetch_trending_now();
                        }
                        g_current_view = VIEW_TRENDING_LIST;
                        sceKernelDelayThread(200000);
                    } else if (tx >= 420.0f && tx <= 540.0f) {
                        g_bubble_selected = 1;
                        g_current_view = VIEW_SEARCH;
                        sceKernelDelayThread(200000);
                    } else if (tx >= 620.0f && tx <= 740.0f) {
                        g_bubble_selected = 2;
                        g_current_view = VIEW_SETTINGS;
                        sceKernelDelayThread(200000);
                    }
                }
            } else if (g_current_view == VIEW_TRENDING_LIST) {
                float start_y = 44.0f;
                float card_h = 84.0f;
                int visible_items = 5;
                int offset = g_selected_index - 2;
                if (offset < 0) offset = 0;
                if (offset > g_trending.count - visible_items) offset = g_trending.count - visible_items;
                if (offset < 0) offset = 0;

                for (int i = 0; i < visible_items && (offset + i) < g_trending.count; i++) {
                    float cy = start_y + (i * (card_h + 8.0f));
                    if (ty >= cy && ty <= (cy + card_h)) {
                        int tapped_idx = offset + i;
                        g_selected_index = tapped_idx;

                        if (tx >= 715.0f && tx <= 817.0f) {
                            g_current_view = VIEW_MODAL_DESCRIPTION;
                            sceKernelDelayThread(200000);
                            break;
                        } else if (tx >= 825.0f && tx <= 927.0f) {
                            g_current_view = VIEW_MODAL_CHANNEL;
                            sceKernelDelayThread(200000);
                            break;
                        }
                    }
                }
            } else if (g_current_view == VIEW_MODAL_DESCRIPTION || g_current_view == VIEW_MODAL_CHANNEL) {
                if (ty >= 420.0f || ty <= 75.0f || tx <= 120.0f || tx >= 840.0f) {
                    g_current_view = VIEW_TRENDING_LIST;
                    sceKernelDelayThread(200000);
                }
            }
        }
#endif

        vita2d_start_drawing();
        vita2d_clear_screen();

        ui_draw_background();
        ui_draw_status_bar("PocketVideo");

        if (g_current_view == VIEW_HOME_BUBBLES) {
            ui_draw_text_centered(480.0f, 100, COLOR_TEXT_LIGHT, 24, "Welcome to PocketVideo.");
            ui_draw_text_centered(480.0f, 132, COLOR_TEXT_DIM, 15, "Select category using D-Pad, analog stick or touch screen.");

            ui_draw_bubble(280.0f, 280.0f, 54.0f, "Trending", COLOR_BRAND_BLUE, 0, (g_bubble_selected == 0));
            ui_draw_bubble(480.0f, 280.0f, 54.0f, "Search", COLOR_BRAND_CYAN, 0, (g_bubble_selected == 1));
            ui_draw_bubble(680.0f, 280.0f, 54.0f, "Instance", RGBA8(180, 80, 220, 255), 0, (g_bubble_selected == 2));

            ui_draw_text_centered(480.0f, 438,
                (g_trending.count > 0) ? RGBA8(110, 231, 183, 235) : RGBA8(252, 165, 165, 235),
                14, g_status_msg);

            const char *prompt = "Press CROSS to enter Trending feed";
            if (g_bubble_selected == 1) prompt = "Press CROSS to browse or refresh feed";
            else if (g_bubble_selected == 2) prompt = "Press CROSS to view Invidious instance details";
            ui_draw_text_centered(480.0f, 480, COLOR_HIGHLIGHT_AMBER, 16, prompt);
        } else if (g_current_view == VIEW_SEARCH || g_current_view == VIEW_SETTINGS) {
            vita2d_draw_rectangle(140, 110, 680, 320, RGBA8(12, 20, 36, 235));
            vita2d_draw_rectangle(140, 110, 680, 3, COLOR_BRAND_CYAN);
            ui_draw_text_centered(480.0f, 156, COLOR_BRAND_CYAN, 20,
                (g_current_view == VIEW_SEARCH) ? "PocketVideo Feed Browser" : "Invidious Instance Configuration");
            ui_draw_text_centered(480.0f, 210, COLOR_TEXT_LIGHT, 16, "Active Instance: https://yt.chocolatemoo53.com");
            ui_draw_text_centered(480.0f, 246, COLOR_TEXT_DIM, 15, g_status_msg);
            ui_draw_text_centered(480.0f, 330, COLOR_HIGHLIGHT_AMBER, 16, "Press CROSS to Refresh Trending   |   Press CIRCLE to Return");
        } else {
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
                ui_draw_text_centered(480.0f, 255, COLOR_TEXT_LIGHT, 18, g_status_msg);
                ui_draw_text_centered(480.0f, 295, COLOR_HIGHLIGHT_AMBER, 15, "Press SQUARE to Retry   |   Press CIRCLE to Return");
            }

            if (g_current_view == VIEW_MODAL_DESCRIPTION && g_trending.count > 0) {
                ui_draw_description_modal(&g_trending.items[g_selected_index]);
            }

            if (g_current_view == VIEW_MODAL_CHANNEL && g_trending.count > 0) {
                ui_draw_channel_modal(&g_trending.items[g_selected_index]);
            }
        }

        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
    }

    main_log("[Main] Exiting PocketVideo...");
    ui_term();
    net_term();

#ifdef __vita__
    sceKernelExitProcess(0);
#endif

    return 0;
}
