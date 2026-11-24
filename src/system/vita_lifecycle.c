/**
 * @file vita_lifecycle.c
 * @brief Listen for Vita shell events without blocking the screen or media worker.
 * A PS/Home suspend can stop the entire process before a callback runs.
 */

/* Vita lifecycle: collect shell events without depending on a busy UI. */
#include "system/vita_lifecycle.h"

#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>

#include "app_paths.h"

#define VT_LIFECYCLE_TRACE_PATH VITAMEDIADECK_DATA_DIR "/lifecycle_trace.txt"
#define VT_LIFECYCLE_POLL_US 16000

/* The event IDs are stable even if older VitaSDK headers omit their names. */
enum {
    VT_APPMGR_ON_ACTIVATE = 0x10000001,
    VT_APPMGR_ON_DEACTIVATE = 0x10000002,
    VT_APPMGR_ON_RESUME = 0x10000003,
    VT_APPMGR_REQUEST_QUIT = 0x20000001
};

static volatile int g_events;
static volatile int g_quit;
static volatile int g_thread_stop;
static SceUID g_thread = -1;
#ifdef SSSPLAYER_DEBUG_LOG
static volatile int g_trace_lock;
#endif

#ifdef SSSPLAYER_DEBUG_LOG
/**
 * @brief Write a complete trace line and close the handle immediately.
 * @param text NUL-terminated line to append.
 */
static void trace_line(const char *text)
{
    if (!text) return;
    while (__sync_lock_test_and_set(&g_trace_lock, 1))
        sceKernelDelayThread(100);
    SceUID fd = sceIoOpen(VT_LIFECYCLE_TRACE_PATH,
                         SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, strlen(text));
        sceIoClose(fd);
    }
    __sync_lock_release(&g_trace_lock);
}
#endif

/**
 * @brief Reset the diagnostic file on every launch to expose stale builds.
 */
void vt_lifecycle_trace_begin(void)
{
#ifdef SSSPLAYER_DEBUG_LOG
    sceIoMkdir(VITAMEDIADECK_DATA_DIR, 0777);
    SceUID fd = sceIoOpen(VT_LIFECYCLE_TRACE_PATH,
                         SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) sceIoClose(fd);
    vt_lifecycle_trace("build " SSSPLAYER_PATCH_ID,
                       SSSPLAYER_SFO_ATTRIBUTE);
#endif
}

/**
 * @brief Persist a lifecycle checkpoint without using the session-log mutex.
 * @param stage Brief checkpoint name.
 * @param detail Event ID or result code.
 */
void vt_lifecycle_trace(const char *stage, int detail)
{
#ifdef SSSPLAYER_DEBUG_LOG
    char line[160];
    snprintf(line, sizeof(line), "%llu us %s: 0x%08X\n",
             (unsigned long long)sceKernelGetProcessTimeWide(),
             stage ? stage : "(null)", (unsigned)detail);
    trace_line(line);
#else
    (void)stage;
    (void)detail;
#endif
}

/**
 * @brief Consume one nonblocking system event and publish it to the UI.
 * @return One if an event was received; zero if none is pending.
 */
static int lifecycle_receive_one(void)
{
    SceAppMgrSystemEvent event;
    memset(&event, 0, sizeof(event));
    int ret = sceAppMgrReceiveSystemEvent(&event);
    if (ret < 0 || !event.systemEvent) return 0;

    vt_lifecycle_trace("appmgr", event.systemEvent);
    int flag = 0;
    switch (event.systemEvent) {
        case VT_APPMGR_ON_DEACTIVATE:
            flag = VT_LIFECYCLE_DEACTIVATE;
            break;
        case VT_APPMGR_ON_ACTIVATE:
        case VT_APPMGR_ON_RESUME:
            flag = VT_LIFECYCLE_RESUME;
            break;
        case VT_APPMGR_REQUEST_QUIT:
            g_quit = 1;
            flag = VT_LIFECYCLE_QUIT;
            break;
        default:
            break;
    }
    if (flag) __sync_fetch_and_or(&g_events, flag);
    return 1;
}

/**
 * @brief Listen for shell transitions even during nested or blocking screens.
 * Never closes media, touches GXM, or exits the process from this thread.
 * @param args Unused thread argument size.
 * @param argp Unused thread argument pointer.
 * @return Zero when the main thread asks the listener to stop.
 */
static int lifecycle_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    while (!g_thread_stop && !g_quit) {
        if (!lifecycle_receive_one())
            sceKernelDelayThread(VT_LIFECYCLE_POLL_US);
    }
    return 0;
}

/**
 * @brief Begin receiving AppMgr events on a lightweight worker.
 * @return Zero if monitoring started, or a negative kernel error.
 */
int vt_lifecycle_start(void)
{
    if (g_thread >= 0) return 0;
    g_events = 0;
    g_quit = 0;
    g_thread_stop = 0;
    SceUID thread = sceKernelCreateThread("SSS_lifecycle", lifecycle_thread,
                                        0x10000100, 0x8000, 0, 0, NULL);
    if (thread < 0) {
        vt_lifecycle_trace("listener create failed", thread);
        return thread;
    }
    int ret = sceKernelStartThread(thread, 0, NULL);
    if (ret < 0) {
        sceKernelDeleteThread(thread);
        vt_lifecycle_trace("listener start failed", ret);
        return ret;
    }
    g_thread = thread;
    vt_lifecycle_trace("listener started", thread);
    return 0;
}

/**
 * @brief Stop listening before AppUtil and audio resources are destroyed.
 */
void vt_lifecycle_stop(void)
{
    if (g_thread < 0) return;
    g_thread_stop = 1;
    sceKernelWaitThreadEnd(g_thread, NULL, NULL);
    sceKernelDeleteThread(g_thread);
    g_thread = -1;
    vt_lifecycle_trace("listener stopped", 0);
}

/**
 * @brief Atomically transfer observed lifecycle flags to the UI owner.
 * @return Pending event bitmask.
 */
int vt_lifecycle_take_events(void)
{
    /* The main thread remains functional if thread creation was denied. */
    if (g_thread < 0) {
        for (int i = 0; i < 8 && lifecycle_receive_one(); i++) { }
    }
    return __sync_lock_test_and_set(&g_events, 0);
}

/**
 * @brief Report if a quit request was seen while the main thread was busy.
 * @return Nonzero if the system requested termination.
 */
int vt_lifecycle_quit_pending(void)
{
    return g_quit != 0;
}
