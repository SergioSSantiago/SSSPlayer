#ifndef SSSPLAYER_APP_UPDATE_H
#define SSSPLAYER_APP_UPDATE_H

/* Checks GitHub for a newer release than SSSPLAYER_VERSION_LABEL. If found,
 * prompts the user and can download + install the VPK. Safe to call after
 * UI init; initializes HTTPS as needed. Non-blocking for the happy path
 * (no update / offline) within a few seconds. */
void sss_app_update_check_on_launch(void);

/* Manual Settings entry: always reports online status, up-to-date, or offers
 * install when a newer release exists. */
void sss_app_update_check_manual(void);

#endif
