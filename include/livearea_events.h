#ifndef SSSPLAYER_LIVEAREA_EVENTS_H
#define SSSPLAYER_LIVEAREA_EVENTS_H
/* Firmware event numbers are stable across SDK revisions.
 * Event delivery before suspension is not guaranteed. */
#define SSS_LIVEAREA_DEACTIVATE 1
#define SSS_LIVEAREA_RESUME     2
#define SSS_LIVEAREA_QUIT       4
/**
 * Consume pending AppMgr events from the current UI owner.
 * Returns a bitmask of the transitions delivered during this call.
 */
int sss_livearea_poll(int ui_ready);

/**
 * Request an orderly exit without tearing resources down in a nested screen.
 * Called for an AppMgr quit event or the emergency controller shortcut.
 */
void sss_livearea_request_exit(void);

/**
 * Poll the shutdown request and hidden START+SELECT shortcut on any screen.
 * The main thread performs resource teardown after nested screens return.
 */
int sss_livearea_exit_requested(void);
#endif
