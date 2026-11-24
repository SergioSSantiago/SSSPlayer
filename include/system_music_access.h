#ifndef SSSPLAYER_SYSTEM_MUSIC_ACCESS_H
#define SSSPLAYER_SYSTEM_MUSIC_ACCESS_H
/* Explicit, session-scoped private Sony Music access.
 * Disabled on every launch. Never alters the Sony Music database. */
int sss_system_music_enabled(void);
int sss_system_music_set_enabled(int enabled);
#endif
