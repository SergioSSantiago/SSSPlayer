#ifndef SSSPLAYER_UI_MUSIC_LIBRARY_SCREEN_H
#define SSSPLAYER_UI_MUSIC_LIBRARY_SCREEN_H

#include <stddef.h>

#define UI_MUSIC_LIB_BACK 0
#define UI_MUSIC_LIB_PLAY 1

/* Spotify-style Artists / Albums / Songs browser for a music folder tree.
 * Returns UI_MUSIC_LIB_PLAY and fills play_path, or UI_MUSIC_LIB_BACK. */
int ui_music_library_run(const char *root, char *play_path, size_t play_path_size);

#endif
