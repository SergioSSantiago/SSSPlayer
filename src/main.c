/*
 * SSSPlayer – PS Vita Homebrew Music Player
 * main.c – entry point, global subsystem instances, main loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/callback.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/power.h>
#include <psp2/shellutil.h>
#include <vita2d.h>

#include "audio_engine.h"
#include "decoder.h"
#include "equalizer.h"
#include "file_browser.h"
#include "media_db.h"
#include "metadata.h"
#include "playlist.h"
#include "ui.h"
#include "visualizer.h"
#include "globals.h"
#include "theme.h"
#include "ui/touch.h"
#include "video_bridge.h"
#include "system/app_update.h"

static Equalizer g_eq;

/* ── Background DB rebuild thread ───────────────────────────────────────── */
static int db_rebuild_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    media_db_rebuild();
    sceKernelExitThread(0);
    return 0;
}

/* ── Global subsystem instances ─────────────────────────────────────────── */
static AudioEngine    g_engine;
static Playlist      *g_playlist     = NULL;
static FileList      *g_browser      = NULL;
static UIState        g_ui;
static Visualizer     g_vis;
static TrackMetadata  g_current_meta;
static PlaylistManager g_playlist_manager;
static ThemeManager   g_theme_mgr;


/* ── Global accessors ────────────────────────────────────────────────────── */
AudioEngine     *get_audio_engine    (void) { return &g_engine;            }
Playlist        *get_playlist        (void) { return g_playlist;            }
FileList        *get_browser         (void) { return g_browser;             }
UIState         *get_ui_state        (void) { return &g_ui;                 }
Visualizer      *get_visualizer      (void) { return &g_vis;                }
TrackMetadata   *get_current_meta    (void) { return &g_current_meta;       }
PlaylistManager *get_playlist_manager(void) { return &g_playlist_manager;   }
ThemeManager    *get_theme_manager   (void) { return &g_theme_mgr;          }
Equalizer       *get_equalizer       (void) { return &g_eq;                 }

/* ── FPS limiter helper ───────────────────────────────────────────────────── */
static void fps_limit(uint64_t frame_start_us)
{
    const uint64_t TARGET_FRAME_US = 1000000ULL / UI_FPS;
    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t elapsed = now - frame_start_us;
    if (elapsed < TARGET_FRAME_US) {
        sceKernelDelayThreadCB((unsigned int)(TARGET_FRAME_US - elapsed));
    }
}

/*
 * This app cannot suspend cleanly (vita2d GXM + BGM + net). Calling
 * vita2d_fini from the Home power-callback corrupts GXM until reboot.
 * Settings → Exit on the main thread is the only safe shutdown.
 *
 * Lock the PS button so Home cannot leave a half-dead process. Unlock
 * only after the Exit cleanup finishes.
 */
static volatile int g_exit_cleanup_started;
static volatile int g_ps_btn_locked;
static volatile int g_forbid_draw;

static void sss_ps_btn_lock(void)
{
    sceShellUtilInitEvents(0);
    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
    g_ps_btn_locked = 1;
}

static void sss_ps_btn_unlock(void)
{
    if (!g_ps_btn_locked) return;
    sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
    g_ps_btn_locked = 0;
}

static void sss_app_run_exit_cleanup(void)
{
    if (g_exit_cleanup_started) return;
    g_exit_cleanup_started = 1;

    sss_video_shutdown();
    ui_touch_term();

    theme_manager_free(&g_theme_mgr);
    ui_destroy(&g_ui);
    visualizer_destroy(&g_vis);
    playlist_manager_save(&g_playlist_manager);
    playlist_manager_destroy(&g_playlist_manager);
    if (g_playlist) {
        playlist_destroy(g_playlist);
        g_playlist = NULL;
    }
    if (g_browser) {
        file_browser_destroy(g_browser);
        g_browser = NULL;
    }
    audio_engine_destroy(&g_engine);
    sceAppMgrReleaseBgmPort();
    metadata_free(&g_current_meta);
    sceAppUtilMusicUmount();
    vita2d_wait_rendering_done();
    vita2d_fini();
    sss_ps_btn_unlock();
}

/* Safety net only: never call vita2d_fini here (that bricks other apps). */
static int power_callback(int notify_id, int notify_count, int notify_arg,
                          void *common)
{
    (void)notify_id;
    (void)notify_count;
    (void)common;

    if (notify_arg & (SCE_POWER_CB_BUTTON_PS_PRESS |
                      SCE_POWER_CB_APP_SUSPEND |
                      SCE_POWER_CB_SYSTEM_SUSPEND)) {
        g_forbid_draw = 1;
        g_ui.request_exit = true;
        sceAppMgrReleaseBgmPort();
        audio_engine_force_release_system(&g_engine);
    }
    return 0;
}

static int power_callback_thread(SceSize args, void *argp)
{
    SceUID cbid;

    (void)args;
    (void)argp;
    cbid = sceKernelCreateCallback("sss_power_cb", 0, power_callback, NULL);
    if (cbid >= 0)
        scePowerRegisterCallback(cbid);
    for (;;)
        sceKernelDelayThreadCB(10 * 1000 * 1000);
    return 0;
}

static void register_power_exit_callback(void)
{
    SceUID thid = sceKernelCreateThread("sss_power_cb_th",
                                        power_callback_thread, 0x10000100,
                                        0x4000, 0, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
}

/* ── Application entry point ─────────────────────────────────────────────── */
int main(void)
{
    int ret;

    /* ── AppUtil init (required before sceAppUtilMusicMount) ── */
    {
        SceAppUtilInitParam  init_param;
        SceAppUtilBootParam  boot_param;
        memset(&init_param, 0, sizeof(init_param));
        memset(&boot_param, 0, sizeof(boot_param));
        sceAppUtilInit(&init_param, &boot_param);
    }
    sceAppUtilMusicMount();

    /* ── Power management ── */
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    register_power_exit_callback();
    /* Block Home: unclean suspend leaves GXM/BGM held until reboot. */
    sss_ps_btn_lock();

    /* ── vita2d init ── */
    vita2d_init();
    vita2d_set_clear_color(COLOR_BG);  /* off-white, matches Apple Music theme */

    /* Front-panel touch for Now Playing scrub (and later video bridge). */
    ui_touch_init();

    /* ── Controller ── */
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    /* ── Audio engine ── */
    memset(&g_engine, 0, sizeof(g_engine));
    ret = audio_engine_init(&g_engine);
    if (ret < 0) {
        /* non-fatal – we can still browse files */
        vita2d_set_clear_color(0xFF000033);
    }

    /* Equalizer */
    eq_init(&g_eq);
    eq_load(&g_eq);
    g_engine.eq = &g_eq;

    /* ── File browser ── */
    g_browser = file_browser_init();
    if (!g_browser) {
        goto cleanup;
    }

    /* Rebuild system music DB in the background — only affects the Vita's
     * system Music app, not SSSPlayer's own file browser.                    */
    {
        SceUID db_tid = sceKernelCreateThread("SSSPlayer_db",
                            db_rebuild_thread, 0x10000060, 0x10000, 0, 0, NULL);
        if (db_tid >= 0) sceKernelStartThread(db_tid, 0, NULL);
    }

    /* Ensure media folders exist so they appear in the browser root list */
    sceIoMkdir("ux0:/music", 0777);
    sceIoMkdir("ux0:/video", 0777);
    sceIoMkdir("ux0:/movies", 0777);
    sceIoMkdir("uma0:/music", 0777);
    sceIoMkdir("uma0:/video", 0777);
    sceIoMkdir("uma0:/movies", 0777);

    /* Scan virtual media root (music + video mounts) */
    file_browser_scan_dir(g_browser, MUSIC_ROOT);

    /* ── Playlist ── */
    g_playlist = playlist_create("Default");
    if (!g_playlist) {
        goto cleanup;
    }

    playlist_manager_init(&g_playlist_manager);
    playlist_manager_load(&g_playlist_manager);

    /* ── Theme system ── */
    sceIoMkdir("ux0:data/SSSPlayer",        0777);
    sceIoMkdir("ux0:data/SSSPlayer/themes", 0777);
    theme_manager_init(&g_theme_mgr);

    /* ── Visualizer ── */
    memset(&g_vis, 0, sizeof(g_vis));
    ret = visualizer_init(&g_vis);
    if (ret < 0) {
        /* non-fatal */
    }

    /* ── UI ── */
    memset(&g_ui, 0, sizeof(g_ui));
    ret = ui_init(&g_ui);
    if (ret < 0) {
        goto cleanup;
    }

    g_ui.theme_mgr = &g_theme_mgr;

    /* Restore custom EQ presets, then validate the saved preset index */
    eq_custom_load(g_ui.eq_custom, &g_ui.eq_custom_count);
    if (g_eq.preset_idx >= EQ_PRESET_COUNT + g_ui.eq_custom_count)
        g_eq.preset_idx = -1;
    g_ui.eq_preset_idx = g_eq.preset_idx;

    theme_manager_restore(&g_theme_mgr, &g_ui);

    /* Offer a newer GitHub release when Wi-Fi is available. */
    sss_app_update_check_on_launch();

    /* ── Main loop ── */
    while (!g_ui.request_exit) {
        uint64_t frame_start = sceKernelGetProcessTimeWide();

        if (g_forbid_draw) {
            g_ui.request_exit = true;
            break;
        }

        /* Handle input */
        ui_handle_input(&g_ui, &g_engine, g_playlist, g_browser, &g_vis);

        /* Update logic */
        ui_update(&g_ui, &g_vis);
        visualizer_update(&g_vis);

        /* Feed latest PCM samples to the visualizer */
        {
            static int16_t vis_samples[FFT_SIZE * 2];
            uint32_t n = audio_engine_get_visualizer_data(&g_engine,
                             vis_samples, FFT_SIZE * 2);
            if (n > 0) {
                visualizer_process_samples(&g_vis, vis_samples, n);
            }
        }

        /* Render */
        if (g_forbid_draw) {
            g_ui.request_exit = true;
            break;
        }
        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_render(&g_ui, &g_engine, g_playlist, g_browser, &g_vis);
        vita2d_end_drawing();
        vita2d_swap_buffers();
        fps_limit(frame_start);
    }

cleanup:
    sss_app_run_exit_cleanup();
    sceKernelExitProcess(0);
    return 0;
}
