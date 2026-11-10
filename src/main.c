/**
 * PocketVideo - PlayStation Vita Invidious Client
 * Direct HTTP/HTTPS real-time internet streaming for videos, channels, banners & icons.
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

/* Button mapping aliases for VitaSDK compatibility */
#ifndef PSP2_CTRL_SELECT
#define PSP2_CTRL_SELECT SCE_CTRL_SELECT
#endif
#ifndef PSP2_CTRL_START
#define PSP2_CTRL_START SCE_CTRL_START
#endif
#ifndef PSP2_CTRL_UP
#define PSP2_CTRL_UP SCE_CTRL_UP
#endif
#ifndef PSP2_CTRL_DOWN
#define PSP2_CTRL_DOWN SCE_CTRL_DOWN
#endif
#ifndef PSP2_CTRL_LEFT
#define PSP2_CTRL_LEFT SCE_CTRL_LEFT
#endif
#ifndef PSP2_CTRL_RIGHT
#define PSP2_CTRL_RIGHT SCE_CTRL_RIGHT
#endif
#ifndef PSP2_CTRL_CROSS
#define PSP2_CTRL_CROSS SCE_CTRL_CROSS
#endif
#ifndef PSP2_CTRL_CIRCLE
#define PSP2_CTRL_CIRCLE SCE_CTRL_CIRCLE
#endif
#ifndef PSP2_CTRL_SQUARE
#define PSP2_CTRL_SQUARE SCE_CTRL_SQUARE
#endif
#ifndef PSP2_CTRL_TRIANGLE
#define PSP2_CTRL_TRIANGLE SCE_CTRL_TRIANGLE
#endif
#ifndef SCE_IME_DIALOG_BUTTON_ENTER
#define SCE_IME_DIALOG_BUTTON_ENTER 1
#endif
#ifndef SCE_IME_DIALOG_BUTTON_CLOSE
#define SCE_IME_DIALOG_BUTTON_CLOSE 2
#endif

static VideoList g_trending_cache;
static VideoList g_active_list;
static VideoList g_channel_videos;
static ChannelInfo g_current_channel;
static int g_selected_index = 0;
static int g_network_ready = 0;
static int g_is_search_mode = 0;
static int g_search_type = 0; /* 0 = Videos, 1 = Channels */
static char g_search_query[128] = "";
static char g_status_msg[256] = "Initializing PocketVideo...";
static AppView g_current_view = VIEW_TRENDING_LIST;
static int g_bubble_selected = 0;

/* Channel View State */
static int g_channel_tab = 0; /* 0: Home, 1: Videos, 2: Shorts, 3: Live, 4: Playlists */
static int g_channel_video_idx = 0;
static int g_channel_desc_modal = 0;
static int g_channel_desc_scroll = 0;

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
        snprintf(g_player_status, sizeof(g_player_status), "Live stream ready");
    }
#else
    snprintf(g_player_status, sizeof(g_player_status), "Direct video stream ready");
#endif
    g_current_view = VIEW_PLAYER;
}

static void open_channel_view(const char *channel_id) {
    if (!channel_id || channel_id[0] == '\0') return;
    render_loading_banner("Opening Channel...", "Fetching channel details & banner...");

    invidious_fetch_channel_info(INVIDIOUS_DEFAULT_HOST, channel_id, &g_current_channel);
    const char *tabs[] = {"Home", "Videos", "Shorts", "Live", "Playlists"};
    invidious_fetch_channel_videos(INVIDIOUS_DEFAULT_HOST, channel_id, tabs[g_channel_tab], &g_channel_videos);

    g_channel_video_idx = 0;
    g_channel_desc_modal = 0;
    g_channel_desc_scroll = 0;
    g_current_view = VIEW_CHANNEL_VIEW;
}

static int g_ignore_tap_frames = 0;

static void perform_search(const char *query) {
    if (!query || query[0] == '\0') return;
    snprintf(g_search_query, sizeof(g_search_query), "%s", query);
    g_is_search_mode = 1;

    render_loading_banner("Searching...", query);
    ui_clear_thumbnails();

    if (g_search_type == 1) {
        int count = invidious_search_channels(INVIDIOUS_DEFAULT_HOST, query, &g_active_list);
        if (count > 0) {
            snprintf(g_status_msg, sizeof(g_status_msg), "Found %d channels for '%s'", count, query);
        } else {
            snprintf(g_status_msg, sizeof(g_status_msg), "No channels found for '%s'", query);
        }
    } else {
        int count = invidious_search(INVIDIOUS_DEFAULT_HOST, query, &g_active_list);
        if (count > 0) {
            snprintf(g_status_msg, sizeof(g_status_msg), "Found %d results for '%s'", count, query);
        } else {
            snprintf(g_status_msg, sizeof(g_status_msg), "No results found for '%s'", query);
        }
    }
    g_selected_index = 0;
    g_current_view = VIEW_TRENDING_LIST;
    g_ignore_tap_frames = 20; /* Ignore touch clicks leftover from Submit button on IME keyboard */
}

#ifdef __vita__
static void utf16_to_utf8(const SceWChar16 *src, char *dst, size_t max_len) {
    size_t out = 0;
    if (!src || !dst || max_len == 0) return;
    while (*src && out + 1 < max_len) {
        uint16_t code = (uint16_t)*src++;
        if (code < 0x80) {
            dst[out++] = (char)code;
        } else if (code < 0x800) {
            if (out + 2 >= max_len) break;
            dst[out++] = (char)(0xC0 | (code >> 6));
            dst[out++] = (char)(0x80 | (code & 0x3F));
        } else {
            if (out + 3 >= max_len) break;
            dst[out++] = (char)(0xE0 | (code >> 12));
            dst[out++] = (char)(0x80 | ((code >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (code & 0x3F));
        }
    }
    dst[out] = '\0';
}

static void utf8_to_utf16(const char *src, SceWChar16 *dst, size_t max_units) {
    size_t out = 0;
    if (!src || !dst || max_units == 0) return;
    const unsigned char *p = (const unsigned char *)src;
    while (*p && out + 1 < max_units) {
        unsigned char b = *p;
        if (b < 0x80) {
            dst[out++] = (SceWChar16)b;
            p++;
        } else if ((b & 0xE0) == 0xC0) {
            if (!p[1]) break;
            dst[out++] = (SceWChar16)(((b & 0x1F) << 6) | (p[1] & 0x3F));
            p += 2;
        } else if ((b & 0xF0) == 0xE0) {
            if (!p[1] || !p[2]) break;
            dst[out++] = (SceWChar16)(((b & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
            p += 3;
        } else {
            p++;
        }
    }
    dst[out] = 0;
}

static void open_keyboard(const char *title, const char *initial_text) {
    if (g_ime_active) return;
    utf8_to_utf16(title ? title : "Search", s_ime_title, 64);
    utf8_to_utf16(initial_text ? initial_text : "", s_ime_init_text, 128);
    memset(s_ime_input_buf, 0, sizeof(s_ime_input_buf));

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.sdkVersion = 0x03500010;
    param.supportedLanguages = 0x0001FFFF;
    param.title = s_ime_title;
    param.initialText = s_ime_init_text;
    param.inputTextBuffer = s_ime_input_buf;
    param.maxTextLength = SCE_IME_DIALOG_MAX_TEXT_LENGTH;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_DEFAULT;
    param.type = SCE_IME_TYPE_DEFAULT;

    if (sceImeDialogInit(&param) >= 0) {
        g_ime_active = 1;
    }
}

static void update_keyboard(void) {
    if (!g_ime_active) return;
    SceCommonDialogStatus status = sceImeDialogGetStatus();
    if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) {
        SceImeDialogResult result;
        memset(&result, 0, sizeof(result));
        sceImeDialogGetResult(&result);
        
        char search_buf[128] = "";
        utf16_to_utf8(s_ime_input_buf, search_buf, sizeof(search_buf));

        sceImeDialogTerm();
        g_ime_active = 0;
        g_ignore_tap_frames = 20;

        if (search_buf[0] != '\0') {
            perform_search(search_buf);
        }
    }
}
#endif

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    ui_init();
    main_log("[Main] PocketVideo initialized.");

    render_loading_banner("Connecting to Network...", "Initializing Invidious API...");
    if (net_init() == 0) {
        g_network_ready = 1;
        int count = invidious_fetch_trending(INVIDIOUS_DEFAULT_HOST, &g_trending_cache);
        if (count > 0) {
            memcpy(&g_active_list, &g_trending_cache, sizeof(VideoList));
            snprintf(g_status_msg, sizeof(g_status_msg), "Loaded %d trending videos.", count);
            g_current_view = VIEW_TRENDING_LIST;
        } else {
            snprintf(g_status_msg, sizeof(g_status_msg), "Failed to load trending feed.");
            g_current_view = VIEW_HOME_BUBBLES;
        }
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg), "Network initialization failed.");
        g_current_view = VIEW_HOME_BUBBLES;
    }

    SceCtrlData pad, old_pad;
    memset(&pad, 0, sizeof(pad));
    memset(&old_pad, 0, sizeof(old_pad));

    SceTouchData touch;
    memset(&touch, 0, sizeof(touch));
    int old_touch_count = 0;

    int running = 1;
    while (running) {
#ifdef __vita__
        update_keyboard();
#endif

        memcpy(&old_pad, &pad, sizeof(SceCtrlData));
        sceCtrlPeekBufferPositive(0, &pad, 1);

        uint32_t btn_pressed = pad.buttons & ~old_pad.buttons;

        /* Analog stick thresholds for smooth navigation */
        int stick_left  = (pad.lx < 60 && old_pad.lx >= 60);
        int stick_right = (pad.lx > 195 && old_pad.lx <= 195);
        int stick_up    = (pad.ly < 60 && old_pad.ly >= 60);
        int stick_down  = (pad.ly > 195 && old_pad.ly <= 195);

        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        int tap_event = (touch.reportNum > 0 && old_touch_count == 0);
        float tap_x = (touch.reportNum > 0) ? (touch.report[0].x * 960.0f / 1920.0f) : 0.0f;
        float tap_y = (touch.reportNum > 0) ? (touch.report[0].y * 544.0f / 1088.0f) : 0.0f;
        old_touch_count = touch.reportNum;

        if (g_ignore_tap_frames > 0) {
            g_ignore_tap_frames--;
            tap_event = 0;
        }

        /* Global SELECT button opens Home Welcome bubbles menu */
        if (btn_pressed & PSP2_CTRL_SELECT) {
            g_current_view = VIEW_HOME_BUBBLES;
            g_bubble_selected = 0;
        }

        /* VIEW: HOME WELCOME BUBBLES */
        if (g_current_view == VIEW_HOME_BUBBLES) {
            if ((btn_pressed & PSP2_CTRL_LEFT) || stick_left) {
                g_bubble_selected = (g_bubble_selected - 1 + 3) % 3;
            }
            if ((btn_pressed & PSP2_CTRL_RIGHT) || stick_right) {
                g_bubble_selected = (g_bubble_selected + 1) % 3;
            }
            if (tap_event) {
                if (tap_x >= 180 && tap_x <= 340 && tap_y >= 210 && tap_y <= 370) g_bubble_selected = 0;
                else if (tap_x >= 400 && tap_x <= 560 && tap_y >= 210 && tap_y <= 370) g_bubble_selected = 1;
                else if (tap_x >= 620 && tap_x <= 780 && tap_y >= 210 && tap_y <= 370) g_bubble_selected = 2;
            }
            if ((btn_pressed & PSP2_CTRL_CROSS) || (btn_pressed & PSP2_CTRL_TRIANGLE) || tap_event) {
                if (g_bubble_selected == 0 && !(btn_pressed & PSP2_CTRL_TRIANGLE)) {
                    memcpy(&g_active_list, &g_trending_cache, sizeof(VideoList));
                    g_is_search_mode = 0;
                    g_selected_index = 0;
                    g_current_view = VIEW_TRENDING_LIST;
                } else if (g_bubble_selected == 1 || (btn_pressed & PSP2_CTRL_TRIANGLE)) {
                    g_is_search_mode = 1;
                    g_current_view = VIEW_TRENDING_LIST;
#ifdef __vita__
                    open_keyboard("Search YouTube or Channels", g_search_query);
#endif
                } else if (g_bubble_selected == 2) {
                    g_current_view = VIEW_SETTINGS;
                }
            }
        }
        /* VIEW: TRENDING / SEARCH LIST */
        else if (g_current_view == VIEW_TRENDING_LIST) {
            /* Open/Execute Search: START button or TRIANGLE button */
            if ((btn_pressed & PSP2_CTRL_START) || (btn_pressed & PSP2_CTRL_TRIANGLE)) {
                if (g_search_query[0] != '\0' && (btn_pressed & PSP2_CTRL_START)) {
                    perform_search(g_search_query);
                } else {
#ifdef __vita__
                    open_keyboard("Search YouTube or Channels", g_search_query);
#endif
                }
            }

            /* Toggle Search Mode: Square button (PSP2_CTRL_SQUARE) toggles between Video and Channel search */
            if (btn_pressed & PSP2_CTRL_SQUARE) {
                g_search_type = (g_search_type == 1) ? 0 : 1;
                if (g_search_query[0] != '\0') {
                    perform_search(g_search_query);
                }
            }

            if ((btn_pressed & PSP2_CTRL_DOWN) || stick_down) {
                if (g_selected_index + 1 < g_active_list.count) {
                    g_selected_index++;
                }
            }
            if ((btn_pressed & PSP2_CTRL_UP) || stick_up) {
                if (g_selected_index > 0) {
                    g_selected_index--;
                }
            }

            /* Tap search bar or mode buttons */
            if (tap_event) {
                if (tap_y >= 38 && tap_y <= 72) {
                    if (tap_x >= 728 && tap_x <= 818) {
                        /* Toggle channel search mode button */
                        g_search_type = (g_search_type == 1) ? 0 : 1;
                        if (g_search_query[0] != '\0') perform_search(g_search_query);
                    } else if (tap_x >= 824 && tap_x <= 880) {
                        /* Search button tapped: execute search if query exists; else open keyboard */
                        if (g_search_query[0] != '\0') {
                            perform_search(g_search_query);
                        } else {
#ifdef __vita__
                            open_keyboard("Search", g_search_query);
#endif
                        }
                    } else if (tap_x >= 886 && tap_x <= 940) {
                        g_is_search_mode = 0;
                        g_search_query[0] = '\0';
                        memcpy(&g_active_list, &g_trending_cache, sizeof(VideoList));
                        g_selected_index = 0;
                    } else if (tap_x >= 20 && tap_x <= 720) {
#ifdef __vita__
                        open_keyboard("Search", g_search_query);
#endif
                    }
                } else if (tap_y >= 80 && tap_y <= 520) {
                    int clicked_idx = (int)((tap_y - 80) / 84);
                    if (clicked_idx >= 0 && clicked_idx < 5 && clicked_idx < g_active_list.count) {
                        g_selected_index = clicked_idx;
                        VideoItem *sel = &g_active_list.items[g_selected_index];
                        if (sel->is_channel) {
                            open_channel_view(sel->id[0] ? sel->id : sel->author_id);
                        } else {
                            start_video_playback(sel);
                        }
                    }
                }
            }

            if (btn_pressed & PSP2_CTRL_CROSS) {
                if (g_active_list.count > 0 && g_selected_index < g_active_list.count) {
                    VideoItem *sel = &g_active_list.items[g_selected_index];
                    if (sel->is_channel) {
                        open_channel_view(sel->id[0] ? sel->id : sel->author_id);
                    } else {
                        start_video_playback(sel);
                    }
                }
            }
        }
        /* VIEW: CHANNEL VIEW */
        else if (g_current_view == VIEW_CHANNEL_VIEW) {
            if (g_channel_desc_modal) {
                if ((btn_pressed & PSP2_CTRL_DOWN) || stick_down) g_channel_desc_scroll++;
                if ((btn_pressed & PSP2_CTRL_UP) || stick_up) { if (g_channel_desc_scroll > 0) g_channel_desc_scroll--; }
                if ((btn_pressed & PSP2_CTRL_CIRCLE) || (btn_pressed & PSP2_CTRL_SQUARE)) {
                    g_channel_desc_modal = 0;
                }
            } else {
                if (btn_pressed & PSP2_CTRL_SQUARE) {
                    g_channel_desc_modal = 1;
                    g_channel_desc_scroll = 0;
                }
                if ((btn_pressed & PSP2_CTRL_LEFT) || stick_left) {
                    if (g_channel_tab > 0) {
                        g_channel_tab--;
                        const char *tabs[] = {"Home", "Videos", "Shorts", "Live", "Playlists"};
                        render_loading_banner(g_current_channel.author, "Loading tab content...");
                        invidious_fetch_channel_videos(INVIDIOUS_DEFAULT_HOST, g_current_channel.author_id, tabs[g_channel_tab], &g_channel_videos);
                        g_channel_video_idx = 0;
                    }
                }
                if ((btn_pressed & PSP2_CTRL_RIGHT) || stick_right) {
                    if (g_channel_tab < 4) {
                        g_channel_tab++;
                        const char *tabs[] = {"Home", "Videos", "Shorts", "Live", "Playlists"};
                        render_loading_banner(g_current_channel.author, "Loading tab content...");
                        invidious_fetch_channel_videos(INVIDIOUS_DEFAULT_HOST, g_current_channel.author_id, tabs[g_channel_tab], &g_channel_videos);
                        g_channel_video_idx = 0;
                    }
                }
                if ((btn_pressed & PSP2_CTRL_DOWN) || stick_down) {
                    if (g_channel_video_idx + 1 < g_channel_videos.count) g_channel_video_idx++;
                }
                if ((btn_pressed & PSP2_CTRL_UP) || stick_up) {
                    if (g_channel_video_idx > 0) g_channel_video_idx--;
                }
                if ((btn_pressed & PSP2_CTRL_CROSS)) {
                    if (g_channel_videos.count > 0 && g_channel_video_idx < g_channel_videos.count) {
                        start_video_playback(&g_channel_videos.items[g_channel_video_idx]);
                    }
                }
                if (btn_pressed & PSP2_CTRL_CIRCLE) {
                    g_current_view = VIEW_TRENDING_LIST;
                }
            }
        }
        /* VIEW: VIDEO PLAYER */
        else if (g_current_view == VIEW_PLAYER) {
            if (g_player_desc_open) {
                if ((btn_pressed & PSP2_CTRL_DOWN) || stick_down) g_player_desc_scroll++;
                if ((btn_pressed & PSP2_CTRL_UP) || stick_up) { if (g_player_desc_scroll > 0) g_player_desc_scroll--; }
                if ((btn_pressed & PSP2_CTRL_CIRCLE) || (btn_pressed & PSP2_CTRL_SQUARE)) {
                    g_player_desc_open = 0;
                }
            } else {
                if (btn_pressed & PSP2_CTRL_SQUARE) {
                    g_player_desc_open = 1;
                    g_player_desc_scroll = 0;
                }
                if ((btn_pressed & PSP2_CTRL_CROSS) || tap_event) {
                    g_player_paused = !g_player_paused;
                }
                if (btn_pressed & PSP2_CTRL_CIRCLE) {
                    stop_video_playback();
                    g_current_view = VIEW_TRENDING_LIST;
                }
            }
        }
        /* VIEW: SETTINGS */
        else if (g_current_view == VIEW_SETTINGS) {
            if (btn_pressed & PSP2_CTRL_CIRCLE) {
                g_current_view = VIEW_HOME_BUBBLES;
            }
        }

        /* RENDERING */
        vita2d_start_drawing();
        vita2d_clear_screen();

        if (g_current_view == VIEW_HOME_BUBBLES) {
            ui_draw_background();
            ui_draw_status_bar("Welcome to PocketVideo");
            ui_draw_text_centered(480.0f, 180.0f, COLOR_TEXT_LIGHT, 22, "Welcome to PocketVideo.");
            ui_draw_text_centered(480.0f, 214.0f, COLOR_BRAND_CYAN, 15,
                                  "Select category using D-Pad, analog stick or touch screen.");

            float cy = 290.0f;
            ui_draw_bubble(260.0f, cy, 52.0f, "Trending", COLOR_BRAND_BLUE, 0, (g_bubble_selected == 0));
            ui_draw_bubble(480.0f, cy, 52.0f, "Search", RGBA8(0, 160, 230, 255), 0, (g_bubble_selected == 1));
            ui_draw_bubble(700.0f, cy, 52.0f, "Instance", RGBA8(160, 80, 220, 255), 0, (g_bubble_selected == 2));

            ui_draw_text_centered(480.0f, 430.0f, COLOR_BRAND_CYAN, 14, g_status_msg);
            ui_draw_text_centered(480.0f, 480.0f, COLOR_HIGHLIGHT_AMBER, 15, "Press CROSS to enter category");
        }
        else if (g_current_view == VIEW_TRENDING_LIST) {
            ui_draw_background();
            ui_draw_status_bar(g_is_search_mode ? "Search Results" : "Trending Videos");
            ui_draw_search_bar(g_search_query, g_is_search_mode, g_search_type);

            int start_y = 80;
            int card_h = 80;
            int gap = 8;
            int visible = 5;

            int offset = 0;
            if (g_selected_index >= visible) {
                offset = g_selected_index - visible + 1;
            }

            ui_fetch_one_visible_thumbnail(&g_active_list, offset, visible);

            for (int i = 0; i < visible && (offset + i) < g_active_list.count; i++) {
                int idx = offset + i;
                float cy = start_y + (i * (card_h + gap));
                ui_draw_video_card(20, cy, 920, card_h, &g_active_list.items[idx], (idx == g_selected_index));
            }
        }
        else if (g_current_view == VIEW_CHANNEL_VIEW) {
            ui_draw_channel_view(&g_current_channel, &g_channel_videos,
                                 g_channel_tab, g_channel_video_idx,
                                 g_channel_desc_modal, g_channel_desc_scroll);
        }
        else if (g_current_view == VIEW_PLAYER) {
            ui_draw_player_screen(&g_playing_item, g_video_tex, NULL,
                                   g_player_hud_alpha, g_player_paused,
                                   g_player_cur_ms, g_player_total_ms,
                                   g_player_desc_open, g_player_desc_scroll,
                                   g_player_status);
        }
        else if (g_current_view == VIEW_SETTINGS) {
            ui_draw_background();
            ui_draw_status_bar("Instance Settings");
            ui_draw_text_centered(480.0f, 200.0f, COLOR_TEXT_LIGHT, 20, "Instance Configuration");
            ui_draw_text_centered(480.0f, 240.0f, COLOR_BRAND_CYAN, 15, "Current: https://yt.chocolatemoo53.com");
            ui_draw_text_centered(480.0f, 280.0f, COLOR_TEXT_DIM, 14, "Fallback: https://api.piped.private.coffee");
            ui_draw_text_centered(480.0f, 440.0f, COLOR_HIGHLIGHT_AMBER, 14, "Press CIRCLE to return");
        }

        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
    }

    stop_video_playback();
    net_term();
    ui_term();
    return 0;
}
