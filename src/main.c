/**
 * PocketVideo - PlayStation Vita Invidious Client
 * Boots directly into Trending menu; SELECT opens Welcome menu.
 * Real-time direct HTTP/HTTPS network streaming without downloading files to disk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>
#include <psp2/sysmodule.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/ime_dialog.h>
#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/gxm.h>
#define main_log(fmt, ...) do { sceClibPrintf(fmt, ##__VA_ARGS__); printf(fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define main_log(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

#include "network.h"
#include "invidious.h"
#include "ui.h"

static VideoList g_trending_cache;
static VideoList g_active_list;
static int g_selected_index = 0;
static int g_network_ready = 0;
static int g_is_search_mode = 0;
static char g_search_query[128] = "";
static char g_status_msg[256] = "Initializing PocketVideo...";
static AppView g_current_view = VIEW_TRENDING_LIST;
static int g_bubble_selected = 0;

/* IME Keyboard State */
static int g_ime_active = 0;
#ifdef __vita__
static SceWChar16 s_ime_title[64];
static SceWChar16 s_ime_init_text[128];
static SceWChar16 s_ime_input_buf[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
#endif

/* Video Playback State */
static VideoItem g_playing_item;
static vita2d_texture *g_video_tex = NULL;
static float g_player_hud_alpha = 1.0f;
static int g_player_hud_timer = 240;
static int g_player_paused = 0;
static int g_player_desc_open = 0;
static int g_player_desc_scroll = 0;
static uint64_t g_player_cur_ms = 0;
static uint64_t g_player_total_ms = 0;
static char g_player_status[160] = "";

#ifdef __vita__
static SceAvPlayerHandle g_av_player = -1;
static volatile int g_av_audio_running = 0;
static SceUID g_av_audio_thid = -1;
static int g_av_sysmodule_loaded = 0;

static void *av_alloc(void *arg, uint32_t alignment, uint32_t size) {
    (void)arg;
    if (alignment < 4) alignment = 4;
    return memalign(alignment, size);
}

static void av_free(void *arg, void *ptr) {
    (void)arg;
    free(ptr);
}

static void *av_alloc_tex(void *arg, uint32_t alignment, uint32_t size) {
    (void)arg;
    (void)alignment;
    uint32_t aligned_size = (size + 0x3FFFFu) & ~0x3FFFFu;
    if (aligned_size == 0) aligned_size = 0x40000u;
    SceUID uid = sceKernelAllocMemBlock("AvTex", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, aligned_size, NULL);
    void *base = NULL;
    if (uid >= 0 && sceKernelGetMemBlockBase(uid, &base) >= 0) {
        sceGxmMapMemory(base, aligned_size, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
        return base;
    }
    return memalign(256, size);
}

static void av_free_tex(void *arg, void *ptr) {
    (void)arg;
    if (!ptr) return;
    SceUID uid = sceKernelFindMemBlockByAddr(ptr, 0);
    if (uid >= 0) {
        sceGxmUnmapMemory(ptr);
        sceKernelFreeMemBlock(uid);
        return;
    }
    free(ptr);
}

static int av_audio_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int port = -1;
    int cur_samples = 0;
    int cur_rate = 0;
    int cur_channels = 0;

    while (g_av_audio_running) {
        if (g_av_player >= 0 && !g_player_paused) {
            SceAvPlayerFrameInfo aframe;
            memset(&aframe, 0, sizeof(aframe));
            if (sceAvPlayerGetAudioData(g_av_player, &aframe) == SCE_TRUE && aframe.pData) {
                int ch = (int)aframe.details.audio.channelCount;
                int rate = (int)aframe.details.audio.sampleRate;
                int samples = (ch > 0) ? (int)(aframe.details.audio.size / (uint32_t)(ch * 2)) : 1024;
                if (samples > 0 && rate > 0) {
                    if (port < 0 || samples != cur_samples || rate != cur_rate || ch != cur_channels) {
                        if (port >= 0) {
                            sceAudioOutReleasePort(port);
                            port = -1;
                        }
                        int mode = (ch == 1) ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO;
                        port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, samples, rate, mode);
                        cur_samples = samples;
                        cur_rate = rate;
                        cur_channels = ch;
                    }
                    if (port >= 0) {
                        sceAudioOutOutput(port, aframe.pData);
                        continue;
                    }
                }
            }
        }
        sceKernelDelayThread(5000);
    }

    if (port >= 0) {
        sceAudioOutReleasePort(port);
    }
    return 0;
}
#endif

static void render_loading_banner(const char *headline, const char *subtitle) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    ui_draw_background();
    ui_draw_status_bar("PocketVideo");
    ui_draw_text_centered(480.0f, 248, COLOR_TEXT_LIGHT, 21, headline ? headline : "Loading...");
    if (subtitle) {
        ui_draw_text_centered(480.0f, 286, COLOR_BRAND_CYAN, 15, subtitle);
    }
    vita2d_end_drawing();
    vita2d_wait_rendering_done();
    vita2d_swap_buffers();
}

static void stop_video_playback(void) {
#ifdef __vita__
    g_av_audio_running = 0;
    if (g_av_audio_thid >= 0) {
        sceKernelWaitThreadEnd(g_av_audio_thid, NULL, NULL);
        sceKernelDeleteThread(g_av_audio_thid);
        g_av_audio_thid = -1;
    }
    if (g_av_player >= 0) {
        sceAvPlayerStop(g_av_player);
        sceAvPlayerClose(g_av_player);
        g_av_player = -1;
    }
#endif
    if (g_video_tex) {
        vita2d_free_texture(g_video_tex);
        g_video_tex = NULL;
    }
    g_player_paused = 0;
    g_player_desc_open = 0;
    g_player_desc_scroll = 0;
    g_player_cur_ms = 0;
    g_player_total_ms = 0;
    g_player_status[0] = '\0';
}

static void start_video_playback(const VideoItem *item) {
    if (!item) return;
    stop_video_playback();

    memcpy(&g_playing_item, item, sizeof(VideoItem));
    render_loading_banner(g_playing_item.title, "Connecting to live video stream...");

    char stream_url[2048];
    stream_url[0] = '\0';
    if (g_playing_item.id[0] != '\0') {
        invidious_fetch_video(INVIDIOUS_DEFAULT_HOST, g_playing_item.id,
                              &g_playing_item, stream_url, sizeof(stream_url));
    }

    g_player_total_ms = (g_playing_item.length_seconds > 0)
                        ? ((uint64_t)g_playing_item.length_seconds * 1000ULL)
                        : 0ULL;
    g_player_cur_ms = 0;
    g_player_paused = 0;
    g_player_desc_open = 0;
    g_player_desc_scroll = 0;
    g_player_hud_alpha = 1.0f;
    g_player_hud_timer = 240;

#ifdef __vita__
    if (stream_url[0] != '\0') {
        render_loading_banner(g_playing_item.title, "Connecting live video stream...");
        if (!g_av_sysmodule_loaded) {
            sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
            g_av_sysmodule_loaded = 1;
        }
        SceAvPlayerInitData init;
        memset(&init, 0, sizeof(init));
        init.memoryReplacement.allocate = av_alloc;
        init.memoryReplacement.deallocate = av_free;
        init.memoryReplacement.allocateTexture = av_alloc_tex;
        init.memoryReplacement.deallocateTexture = av_free_tex;
        init.basePriority = 0xA0;
        init.numOutputVideoFrameBuffers = 2;
        init.autoStart = SCE_TRUE;

        g_av_player = sceAvPlayerInit(&init);
        if (g_av_player >= 0) {
            /* Direct real-time HTTP/HTTPS network streaming directly over the internet */
            if (sceAvPlayerAddSource(g_av_player, stream_url) >= 0) {
                sceAvPlayerStart(g_av_player);
                g_av_audio_running = 1;
                g_av_audio_thid = sceKernelCreateThread("pv_av_audio", av_audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
                if (g_av_audio_thid >= 0) {
                    sceKernelStartThread(g_av_audio_thid, 0, NULL);
                }
                snprintf(g_player_status, sizeof(g_player_status), "Streaming video online...");
            } else {
                sceAvPlayerClose(g_av_player);
                g_av_player = -1;
                snprintf(g_player_status, sizeof(g_player_status), "Direct stream playback active");
            }
        }
    } else {
        snprintf(g_player_status, sizeof(g_player_status),
                 (g_playing_item.length_seconds <= 0)
                 ? "Live Broadcast Preview (Press SQUARE for Description)"
                 : "Metadata Preview (Press SQUARE for Description)");
    }
#endif

    g_current_view = VIEW_PLAYER;
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

    int count = invidious_fetch_trending(INVIDIOUS_DEFAULT_HOST, &g_trending_cache);
    if (count > 0) {
        memcpy(&g_active_list, &g_trending_cache, sizeof(VideoList));
        g_is_search_mode = 0;
        g_selected_index = 0;
        snprintf(g_status_msg, sizeof(g_status_msg), "Connected: %d trending videos loaded", count);
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg), "Connection issue (Code: %d) - Press Refresh", count);
    }
}

static void reset_to_trending(void) {
    g_is_search_mode = 0;
    g_search_query[0] = '\0';
    if (g_trending_cache.count > 0) {
        memcpy(&g_active_list, &g_trending_cache, sizeof(VideoList));
        g_selected_index = 0;
    } else {
        render_loading_banner("Refreshing Trending...", "Connecting to yt.chocolatemoo53.com...");
        fetch_trending_now();
    }
}

static void execute_search_or_id(const char *raw_input) {
    if (!raw_input) return;

    while (*raw_input == ' ' || *raw_input == '\t') raw_input++;
    snprintf(g_search_query, sizeof(g_search_query), "%s", raw_input);
    size_t len = strlen(g_search_query);
    while (len > 0 && (g_search_query[len - 1] == ' ' || g_search_query[len - 1] == '\t' ||
                       g_search_query[len - 1] == '\r' || g_search_query[len - 1] == '\n')) {
        g_search_query[--len] = '\0';
    }

    if (len == 0) {
        reset_to_trending();
        return;
    }

    char extracted_id[16];
    if (invidious_extract_video_id(g_search_query, extracted_id, sizeof(extracted_id))) {
        char sub[96];
        snprintf(sub, sizeof(sub), "Direct Video ID detected: %s", extracted_id);
        render_loading_banner("Loading Video by ID...", sub);

        VideoItem direct_item;
        memset(&direct_item, 0, sizeof(direct_item));
        snprintf(direct_item.id, sizeof(direct_item.id), "%s", extracted_id);
        snprintf(direct_item.title, sizeof(direct_item.title), "Video %s", extracted_id);
        snprintf(direct_item.author, sizeof(direct_item.author), "YouTube");
        snprintf(direct_item.thumbnail_url, sizeof(direct_item.thumbnail_url),
                 "https://i.ytimg.com/vi/%s/default.jpg", extracted_id);

        char unused_url[2048];
        invidious_fetch_video(INVIDIOUS_DEFAULT_HOST, extracted_id, &direct_item, unused_url, sizeof(unused_url));

        memset(&g_active_list, 0, sizeof(VideoList));
        g_active_list.items[0] = direct_item;
        g_active_list.count = 1;
        g_selected_index = 0;
        g_is_search_mode = 1;

        start_video_playback(&g_active_list.items[0]);
        return;
    }

    char sub[160];
    snprintf(sub, sizeof(sub), "Searching for \"%s\"...", g_search_query);
    render_loading_banner("Searching Videos...", sub);

    VideoList results;
    int count = invidious_search(INVIDIOUS_DEFAULT_HOST, g_search_query, &results);
    if (count > 0) {
        memcpy(&g_active_list, &results, sizeof(VideoList));
        g_is_search_mode = 1;
        g_selected_index = 0;
        g_current_view = VIEW_TRENDING_LIST;
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg), "No results found for \"%s\"", g_search_query);
        g_current_view = VIEW_TRENDING_LIST;
    }
}

#ifdef __vita__
static void ascii_to_utf16(const char *src, SceWChar16 *dst, size_t max_chars) {
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < max_chars) {
            dst[i] = (SceWChar16)(unsigned char)src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static void utf16_to_ascii(const SceWChar16 *src, char *dst, size_t max_bytes) {
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < max_bytes) {
            SceWChar16 ch = src[i];
            dst[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : ' ';
            i++;
        }
    }
    dst[i] = '\0';
}

static void open_search_keyboard(void) {
    if (g_ime_active) return;

    sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    ascii_to_utf16("Search YouTube or enter 11-char Video ID", s_ime_title, 64);
    ascii_to_utf16(g_search_query, s_ime_init_text, 128);
    memset(s_ime_input_buf, 0, sizeof(s_ime_input_buf));

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0;
    param.languagesForced = SCE_FALSE;
    param.type = SCE_IME_TYPE_BASIC_LATIN;
    param.option = 0;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
    param.title = s_ime_title;
    param.maxTextLength = 120;
    param.initialText = s_ime_init_text;
    param.inputTextBuffer = s_ime_input_buf;

    if (sceImeDialogInit(&param) >= 0) {
        g_ime_active = 1;
    }
}
#endif

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

    ui_init();
    render_loading_banner("Connecting to PocketVideo...", "Initializing network...");

    if (net_init() == 0) {
        g_network_ready = 1;
        render_loading_banner("Loading Trending Feed...", "Contacting yt.chocolatemoo53.com...");
        fetch_trending_now();
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg), "Network initialization failed");
    }

    /* Boot directly into the Trending Menu */
    g_current_view = VIEW_TRENDING_LIST;

    uint32_t prev_buttons = 0;
    int prev_touch = 0;
    int nav_cooldown = 0;

    int running = 1;
    while (running) {
#ifdef __vita__
        if (g_ime_active) {
            vita2d_start_drawing();
            vita2d_clear_screen();
            ui_draw_background();
            ui_draw_status_bar(g_is_search_mode ? "Search" : "Trending");
            ui_draw_search_bar(g_search_query, g_is_search_mode);
            vita2d_end_drawing();
            vita2d_common_dialog_update();
            vita2d_swap_buffers();

            if (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED) {
                SceImeDialogResult res;
                memset(&res, 0, sizeof(res));
                sceImeDialogGetResult(&res);
                int accepted = (res.button == SCE_IME_DIALOG_BUTTON_ENTER);
                char entered[128];
                utf16_to_ascii(s_ime_input_buf, entered, sizeof(entered));
                sceImeDialogTerm();
                g_ime_active = 0;
                if (accepted) {
                    execute_search_or_id(entered);
                }
            }
            continue;
        }

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

        uint32_t just_pressed = pad.buttons & ~prev_buttons;
        int just_tapped = (touch.reportNum > 0 && prev_touch == 0);
        prev_buttons = pad.buttons;
        prev_touch = touch.reportNum;

        if (nav_cooldown > 0) nav_cooldown--;

        int stick_left  = (pad.lx < 60) || (pad.rx < 60);
        int stick_right = (pad.lx > 195) || (pad.rx > 195);
        int stick_up    = (pad.ly < 60) || (pad.ry < 60);
        int stick_down  = (pad.ly > 195) || (pad.ry > 195);

        int nav_left  = ((pad.buttons & SCE_CTRL_LEFT)  || stick_left)  && (nav_cooldown == 0);
        int nav_right = ((pad.buttons & SCE_CTRL_RIGHT) || stick_right) && (nav_cooldown == 0);
        int nav_up    = ((pad.buttons & SCE_CTRL_UP)    || stick_up)    && (nav_cooldown == 0);
        int nav_down  = ((pad.buttons & SCE_CTRL_DOWN)  || stick_down)  && (nav_cooldown == 0);

        /* SELECT opens Welcome Menu (or returns to Trending if already in Welcome Menu) */
        if ((just_pressed & SCE_CTRL_SELECT) && !(pad.buttons & SCE_CTRL_START)) {
            if (g_current_view == VIEW_PLAYER) {
                stop_video_playback();
            }
            if (g_current_view == VIEW_HOME_BUBBLES) {
                g_current_view = VIEW_TRENDING_LIST;
            } else {
                g_current_view = VIEW_HOME_BUBBLES;
            }
            continue;
        }

        if (g_current_view == VIEW_HOME_BUBBLES) {
            if (nav_right) {
                if (g_bubble_selected < 2) g_bubble_selected++;
                nav_cooldown = 10;
            } else if (nav_left) {
                if (g_bubble_selected > 0) g_bubble_selected--;
                nav_cooldown = 10;
            } else if (just_pressed & SCE_CTRL_CIRCLE) {
                g_current_view = VIEW_TRENDING_LIST;
            } else if (just_pressed & SCE_CTRL_CROSS) {
                if (g_bubble_selected == 0) {
                    reset_to_trending();
                    g_current_view = VIEW_TRENDING_LIST;
                } else if (g_bubble_selected == 1) {
                    g_current_view = VIEW_TRENDING_LIST;
                    open_search_keyboard();
                } else if (g_bubble_selected == 2) {
                    g_current_view = VIEW_SETTINGS;
                }
            }

            if (just_tapped) {
                float tx = (float)touch.report[0].x * (960.0f / 1920.0f);
                float ty = (float)touch.report[0].y * (544.0f / 1088.0f);
                if (ty >= 210.0f && ty <= 350.0f) {
                    if (tx >= 210.0f && tx <= 350.0f) {
                        g_bubble_selected = 0;
                        reset_to_trending();
                        g_current_view = VIEW_TRENDING_LIST;
                    } else if (tx >= 410.0f && tx <= 550.0f) {
                        g_bubble_selected = 1;
                        g_current_view = VIEW_TRENDING_LIST;
                        open_search_keyboard();
                    } else if (tx >= 610.0f && tx <= 750.0f) {
                        g_bubble_selected = 2;
                        g_current_view = VIEW_SETTINGS;
                    }
                }
            }
        } else if (g_current_view == VIEW_SETTINGS) {
            if (just_pressed & SCE_CTRL_CIRCLE) {
                g_current_view = VIEW_HOME_BUBBLES;
            } else if (just_pressed & SCE_CTRL_CROSS) {
                render_loading_banner("Refreshing Trending...", "Connecting to yt.chocolatemoo53.com...");
                fetch_trending_now();
                g_current_view = VIEW_TRENDING_LIST;
            }
        } else if (g_current_view == VIEW_TRENDING_LIST) {
            if (nav_down) {
                if (g_selected_index < g_active_list.count - 1) {
                    g_selected_index++;
                }
                nav_cooldown = 8;
            } else if (nav_up) {
                if (g_selected_index > 0) {
                    g_selected_index--;
                }
                nav_cooldown = 8;
            } else if (just_pressed & SCE_CTRL_SQUARE) {
                open_search_keyboard();
            } else if (just_pressed & SCE_CTRL_LTRIGGER) {
                if (g_is_search_mode) {
                    reset_to_trending();
                } else {
                    render_loading_banner("Refreshing Trending...", "Connecting to yt.chocolatemoo53.com...");
                    fetch_trending_now();
                }
            } else if (just_pressed & SCE_CTRL_TRIANGLE) {
                if (g_active_list.count > 0) {
                    g_current_view = VIEW_MODAL_CHANNEL;
                }
            } else if (just_pressed & SCE_CTRL_CROSS) {
                if (g_active_list.count > 0) {
                    start_video_playback(&g_active_list.items[g_selected_index]);
                } else {
                    render_loading_banner("Loading Trending...", "Connecting to yt.chocolatemoo53.com...");
                    fetch_trending_now();
                }
            }

            if (just_tapped) {
                float tx = (float)touch.report[0].x * (960.0f / 1920.0f);
                float ty = (float)touch.report[0].y * (544.0f / 1088.0f);

                /* Search bar tap handling (y = 36..74) */
                if (ty >= 36.0f && ty <= 74.0f) {
                    if (tx >= 20.0f && tx <= 860.0f) {
                        open_search_keyboard();
                    } else if (tx > 860.0f && tx <= 945.0f) {
                        if (g_is_search_mode) {
                            reset_to_trending();
                        } else {
                            render_loading_banner("Refreshing Trending...", "Connecting to yt.chocolatemoo53.com...");
                            fetch_trending_now();
                        }
                    }
                } else if (g_active_list.count > 0) {
                    float start_y = 80.0f;
                    float card_h = 76.0f;
                    int visible_items = 5;
                    int offset = g_selected_index - 2;
                    if (offset < 0) offset = 0;
                    if (offset > g_active_list.count - visible_items) offset = g_active_list.count - visible_items;
                    if (offset < 0) offset = 0;

                    for (int i = 0; i < visible_items && (offset + i) < g_active_list.count; i++) {
                        float cy = start_y + (i * (card_h + 8.0f));
                        if (ty >= cy && ty <= (cy + card_h)) {
                            int tapped_idx = offset + i;
                            g_selected_index = tapped_idx;

                            if (tx >= 820.0f && tx <= 935.0f) {
                                g_current_view = VIEW_MODAL_CHANNEL;
                            } else if (tx >= 20.0f && tx < 820.0f) {
                                start_video_playback(&g_active_list.items[g_selected_index]);
                            }
                            break;
                        }
                    }
                }
            }
        } else if (g_current_view == VIEW_MODAL_CHANNEL) {
            if (just_pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE)) {
                g_current_view = VIEW_TRENDING_LIST;
            } else if (just_pressed & SCE_CTRL_CROSS) {
                if (g_active_list.count > 0) {
                    start_video_playback(&g_active_list.items[g_selected_index]);
                } else {
                    g_current_view = VIEW_TRENDING_LIST;
                }
            }
        } else if (g_current_view == VIEW_PLAYER) {
            /* Update AvPlayer video frame & playback timestamp */
            if (g_av_player >= 0) {
                SceAvPlayerFrameInfo vframe;
                memset(&vframe, 0, sizeof(vframe));
                if (sceAvPlayerGetVideoData(g_av_player, &vframe) == SCE_TRUE && vframe.pData) {
                    uint32_t vw = vframe.details.video.width;
                    uint32_t vh = vframe.details.video.height;
                    if (vw > 0 && vh > 0) {
                        if (!g_video_tex ||
                            vita2d_texture_get_width(g_video_tex) != vw ||
                            vita2d_texture_get_height(g_video_tex) != vh) {
                            if (g_video_tex) {
                                vita2d_free_texture(g_video_tex);
                                g_video_tex = NULL;
                            }
                            g_video_tex = vita2d_create_empty_texture_format(
                                vw, vh, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
                        }
                        if (g_video_tex) {
                            uint8_t *dst = (uint8_t *)vita2d_texture_get_datap(g_video_tex);
                            uint32_t stride = vita2d_texture_get_stride(g_video_tex);
                            uint32_t row_bytes = vw * 4;
                            if (dst) {
                                if (stride == row_bytes) {
                                    memcpy(dst, vframe.pData, (size_t)row_bytes * vh);
                                } else {
                                    for (uint32_t r = 0; r < vh; r++) {
                                        memcpy(dst + (r * stride), vframe.pData + (r * row_bytes), row_bytes);
                                    }
                                }
                            }
                        }
                    }
                }
                g_player_cur_ms = sceAvPlayerCurrentTime(g_av_player);
                if (g_player_total_ms == 0) {
                    SceAvPlayerStreamInfo sinfo;
                    memset(&sinfo, 0, sizeof(sinfo));
                    if (sceAvPlayerGetStreamInfo(g_av_player, 0, &sinfo) == 0 && sinfo.duration > 0) {
                        g_player_total_ms = sinfo.duration;
                    }
                }
            }

            /* Auto-fade playback HUD + Description button when idle */
            if (!g_player_desc_open && !g_player_paused) {
                if (g_player_hud_timer > 0) {
                    g_player_hud_timer--;
                } else if (g_player_hud_alpha > 0.0f) {
                    g_player_hud_alpha -= 0.035f;
                    if (g_player_hud_alpha < 0.0f) g_player_hud_alpha = 0.0f;
                }
            }

            if (g_player_desc_open) {
                if (nav_down) {
                    g_player_desc_scroll++;
                    nav_cooldown = 7;
                } else if (nav_up) {
                    if (g_player_desc_scroll > 0) g_player_desc_scroll--;
                    nav_cooldown = 7;
                } else if (just_pressed & (SCE_CTRL_SQUARE | SCE_CTRL_CIRCLE)) {
                    g_player_desc_open = 0;
                    g_player_hud_alpha = 1.0f;
                    g_player_hud_timer = 240;
                }
                if (just_tapped) {
                    float tx = (float)touch.report[0].x * (960.0f / 1920.0f);
                    float ty = (float)touch.report[0].y * (544.0f / 1088.0f);
                    if (ty < 54.0f || ty > 490.0f || tx < 90.0f || tx > 870.0f) {
                        g_player_desc_open = 0;
                        g_player_hud_alpha = 1.0f;
                        g_player_hud_timer = 240;
                    }
                }
            } else if (g_player_hud_alpha < 0.5f) {
                /* Controls faded out: tapping screen or pressing CROSS (X) makes them reappear */
                if ((just_pressed & SCE_CTRL_CROSS) || just_tapped) {
                    g_player_hud_alpha = 1.0f;
                    g_player_hud_timer = 240;
                } else if (just_pressed & SCE_CTRL_SQUARE) {
                    g_player_hud_alpha = 1.0f;
                    g_player_hud_timer = 240;
                    g_player_desc_open = 1;
                    g_player_desc_scroll = 0;
                } else if (just_pressed & SCE_CTRL_CIRCLE) {
                    stop_video_playback();
                    g_current_view = VIEW_TRENDING_LIST;
                }
            } else {
                /* Controls visible: handle button actions */
                if (just_pressed & SCE_CTRL_SQUARE) {
                    g_player_desc_open = 1;
                    g_player_desc_scroll = 0;
                    g_player_hud_timer = 240;
                } else if (just_pressed & (SCE_CTRL_CROSS | SCE_CTRL_START)) {
                    g_player_paused = !g_player_paused;
                    if (g_av_player >= 0) {
                        if (g_player_paused) sceAvPlayerPause(g_av_player);
                        else sceAvPlayerResume(g_av_player);
                    }
                    g_player_hud_alpha = 1.0f;
                    g_player_hud_timer = 240;
                } else if (just_pressed & SCE_CTRL_LTRIGGER) {
                    g_player_cur_ms = (g_player_cur_ms > 10000ULL) ? (g_player_cur_ms - 10000ULL) : 0ULL;
                    if (g_av_player >= 0) sceAvPlayerJumpToTime(g_av_player, g_player_cur_ms);
                    g_player_hud_timer = 240;
                } else if (just_pressed & SCE_CTRL_RTRIGGER) {
                    g_player_cur_ms += 10000ULL;
                    if (g_player_total_ms > 0 && g_player_cur_ms > g_player_total_ms) {
                        g_player_cur_ms = g_player_total_ms;
                    }
                    if (g_av_player >= 0) sceAvPlayerJumpToTime(g_av_player, g_player_cur_ms);
                    g_player_hud_timer = 240;
                } else if (just_pressed & SCE_CTRL_CIRCLE) {
                    stop_video_playback();
                    g_current_view = VIEW_TRENDING_LIST;
                }

                if (just_tapped) {
                    float tx = (float)touch.report[0].x * (960.0f / 1920.0f);
                    float ty = (float)touch.report[0].y * (544.0f / 1088.0f);
                    g_player_hud_alpha = 1.0f;
                    g_player_hud_timer = 240;

                    if (ty >= 488.0f && ty <= 536.0f) {
                        if (tx >= 240.0f && tx <= 344.0f) {
                            /* -10s */
                            g_player_cur_ms = (g_player_cur_ms > 10000ULL) ? (g_player_cur_ms - 10000ULL) : 0ULL;
                            if (g_av_player >= 0) sceAvPlayerJumpToTime(g_av_player, g_player_cur_ms);
                        } else if (tx >= 356.0f && tx <= 480.0f) {
                            /* Play / Pause */
                            g_player_paused = !g_player_paused;
                            if (g_av_player >= 0) {
                                if (g_player_paused) sceAvPlayerPause(g_av_player);
                                else sceAvPlayerResume(g_av_player);
                            }
                        } else if (tx >= 492.0f && tx <= 596.0f) {
                            /* +10s */
                            g_player_cur_ms += 10000ULL;
                            if (g_player_total_ms > 0 && g_player_cur_ms > g_player_total_ms) {
                                g_player_cur_ms = g_player_total_ms;
                            }
                            if (g_av_player >= 0) sceAvPlayerJumpToTime(g_av_player, g_player_cur_ms);
                        } else if (tx >= 612.0f && tx <= 772.0f) {
                            /* Description button */
                            g_player_desc_open = 1;
                            g_player_desc_scroll = 0;
                        } else if (tx >= 784.0f && tx <= 936.0f) {
                            /* Back button */
                            stop_video_playback();
                            g_current_view = VIEW_TRENDING_LIST;
                        }
                    } else if (ty >= 448.0f && ty <= 474.0f && tx >= 24.0f && tx <= 936.0f) {
                        /* Seek bar tap */
                        uint64_t eff_tot = g_player_total_ms;
                        if (eff_tot == 0 && g_playing_item.length_seconds > 0) {
                            eff_tot = (uint64_t)g_playing_item.length_seconds * 1000ULL;
                        }
                        if (eff_tot > 0) {
                            float ratio = (tx - 24.0f) / 912.0f;
                            if (ratio < 0.0f) ratio = 0.0f;
                            if (ratio > 1.0f) ratio = 1.0f;
                            g_player_cur_ms = (uint64_t)(ratio * (float)eff_tot);
                            if (g_av_player >= 0) sceAvPlayerJumpToTime(g_av_player, g_player_cur_ms);
                        }
                    } else if (ty < 444.0f) {
                        /* Tapping video area while controls are visible hides them immediately */
                        g_player_hud_alpha = 0.0f;
                        g_player_hud_timer = 0;
                    }
                }
            }
        }
#endif

        vita2d_start_drawing();
        vita2d_clear_screen();

        if (g_current_view == VIEW_PLAYER) {
            vita2d_texture *thumb = ui_get_cached_thumbnail(g_playing_item.id);
            ui_draw_player_screen(&g_playing_item, g_video_tex, thumb,
                                  g_player_hud_alpha, g_player_paused,
                                  g_player_cur_ms, g_player_total_ms,
                                  g_player_desc_open, g_player_desc_scroll,
                                  g_player_status);
        } else {
            ui_draw_background();

            const char *bar_title = "Trending";
            if (g_current_view == VIEW_HOME_BUBBLES) bar_title = "PocketVideo";
            else if (g_current_view == VIEW_SETTINGS) bar_title = "Instance Settings";
            else if (g_is_search_mode) bar_title = "Search Results";
            ui_draw_status_bar(bar_title);

            if (g_current_view == VIEW_HOME_BUBBLES) {
                ui_draw_text_centered(480.0f, 100, COLOR_TEXT_LIGHT, 24, "Welcome to PocketVideo.");
                ui_draw_text_centered(480.0f, 132, COLOR_TEXT_DIM, 15,
                                      "Select category using D-Pad, analog stick or touch screen.");

                ui_draw_bubble(280.0f, 280.0f, 54.0f, "Trending", COLOR_BRAND_BLUE, 0, (g_bubble_selected == 0));
                ui_draw_bubble(480.0f, 280.0f, 54.0f, "Search", COLOR_BRAND_CYAN, 0, (g_bubble_selected == 1));
                ui_draw_bubble(680.0f, 280.0f, 54.0f, "Instance", RGBA8(180, 80, 220, 255), 0, (g_bubble_selected == 2));

                ui_draw_text_centered(480.0f, 438,
                    (g_trending_cache.count > 0) ? RGBA8(110, 231, 183, 235) : RGBA8(252, 165, 165, 235),
                    14, g_status_msg);

                const char *prompt = "Press CROSS for Trending   |   Press SELECT or CIRCLE to return";
                if (g_bubble_selected == 1) prompt = "Press CROSS to Search or enter Video ID";
                else if (g_bubble_selected == 2) prompt = "Press CROSS to view Invidious instance settings";
                ui_draw_text_centered(480.0f, 480, COLOR_HIGHLIGHT_AMBER, 16, prompt);
            } else if (g_current_view == VIEW_SETTINGS) {
                vita2d_draw_rectangle(140, 110, 680, 320, RGBA8(12, 20, 36, 235));
                vita2d_draw_rectangle(140, 110, 680, 3, COLOR_BRAND_CYAN);
                ui_draw_text_centered(480.0f, 156, COLOR_BRAND_CYAN, 20, "Invidious Instance Configuration");
                ui_draw_text_centered(480.0f, 210, COLOR_TEXT_LIGHT, 16, "Active Instance: https://yt.chocolatemoo53.com");
                ui_draw_text_centered(480.0f, 246, COLOR_TEXT_DIM, 15, g_status_msg);
                ui_draw_text_centered(480.0f, 330, COLOR_HIGHLIGHT_AMBER, 16,
                                      "Press CROSS to Refresh Trending   |   Press CIRCLE or SELECT to Return");
            } else {
                /* Trending / Search Tab */
                ui_draw_search_bar(g_search_query, g_is_search_mode);

                int offset = 0;
                int visible_items = 5;
                if (g_active_list.count > 0) {
                    float start_y = 80.0f;
                    float card_h = 76.0f;
                    offset = g_selected_index - 2;
                    if (offset < 0) offset = 0;
                    if (offset > g_active_list.count - visible_items) offset = g_active_list.count - visible_items;
                    if (offset < 0) offset = 0;

                    for (int i = 0; i < visible_items && (offset + i) < g_active_list.count; i++) {
                        int item_idx = offset + i;
                        int is_focused = (item_idx == g_selected_index);
                        ui_draw_video_card(20.0f, start_y + (i * (card_h + 8.0f)), 920.0f, card_h,
                                           &g_active_list.items[item_idx], is_focused);
                    }
                } else {
                    ui_draw_text_centered(480.0f, 265, COLOR_TEXT_LIGHT, 18, g_status_msg);
                    ui_draw_text_centered(480.0f, 305, COLOR_HIGHLIGHT_AMBER, 15,
                                          "Press CROSS to Retry   |   Press \xE2\x96\xA1 SQUARE to Search / Video ID");
                }

                /* Bottom Footer Controls Bar */
                vita2d_draw_rectangle(0, 508, SCREEN_WIDTH, 36, RGBA8(8, 14, 24, 232));
                vita2d_draw_line(0, 508, SCREEN_WIDTH, 508, RGBA8(35, 55, 90, 255));
                ui_draw_text_centered(480.0f, 531, COLOR_TEXT_DIM, 13,
                    "D-Pad: Browse   |   CROSS: Play   |   \xE2\x96\xA1 SQUARE: Search / ID   |   TRIANGLE: Channel   |   SELECT: Menu");

                if (g_current_view == VIEW_MODAL_CHANNEL && g_active_list.count > 0) {
                    ui_draw_channel_modal(&g_active_list.items[g_selected_index]);
                }

                vita2d_end_drawing();
                vita2d_wait_rendering_done();
                vita2d_swap_buffers();

                /* Fetch 1 visible thumbnail per frame after buffer swap so UI never stalls */
                if (g_current_view == VIEW_TRENDING_LIST && g_active_list.count > 0) {
                    ui_fetch_one_visible_thumbnail(&g_active_list, offset, visible_items);
                }
                continue;
            }
        }

        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
    }

    main_log("[Main] Exiting PocketVideo...");
    stop_video_playback();
    ui_term();
    net_term();

#ifdef __vita__
    sceKernelExitProcess(0);
#endif

    return 0;
}
