#ifndef SSSPLAYER_UI_YT_FAVORITES_SCREEN_H
#define SSSPLAYER_UI_YT_FAVORITES_SCREEN_H

#include "network/yt_favorites.h"

typedef enum {
    UI_YT_FAVORITES_VIDEOS = 0,
    UI_YT_FAVORITES_CHANNELS = 1
} UiYtFavoritesKind;

/**
 * Browse and filter saved YouTube videos or channels.
 * Returns 1 with the selected item, or 0 when the user goes back.
 */
int ui_yt_favorites_screen(YtFavorites *favorites, UiYtFavoritesKind kind,
                           YtSearchResult *video, YtFavoriteChannel *channel);

#endif
