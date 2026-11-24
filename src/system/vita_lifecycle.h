/**
 * @file vita_lifecycle.h
 * @brief Declare Vita system transitions shared with the main UI.
 */

#ifndef SSSPLAYER_VITA_LIFECYCLE_H
#define SSSPLAYER_VITA_LIFECYCLE_H

/** @brief Events from AppMgr, collected independently of the UI loop.
 * System suspension may delay delivery; never rely on a callback alone
 * to stop foreground audio when the user presses PS/Home. */
enum {
    VT_LIFECYCLE_DEACTIVATE = 1,
    VT_LIFECYCLE_RESUME = 2,
    VT_LIFECYCLE_QUIT = 4
};

/**
 * @brief Start a small AppMgr listener independent of media/UI work.
 * @return Zero on success or a negative Vita thread error.
 */
int vt_lifecycle_start(void);

/**
 * @brief Stop and join the listener before any subsystem shutdown.
 */
void vt_lifecycle_stop(void);

/**
 * @brief Retrieve and clear transitions observed by the listener.
 * @return Bitmask of VT_LIFECYCLE_* events since the last call.
 */
int vt_lifecycle_take_events(void);

/**
 * @brief Check whether AppMgr has requested a final process exit.
 * @return Nonzero after an observed quit event.
 */
int vt_lifecycle_quit_pending(void);

/**
 * @brief Start a fresh, debug-only lifecycle trace with a build signature.
 * This separate file survives a frozen main-thread logger.
 */
void vt_lifecycle_trace_begin(void);

/**
 * @brief Append one immediately closed diagnostic checkpoint in debug builds.
 * @param stage Short static name identifying the operation.
 * @param detail Result code or event number to record.
 */
void vt_lifecycle_trace(const char *stage, int detail);

#endif
