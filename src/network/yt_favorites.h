#ifndef SSSPLAYER_NETWORK_YT_FAVORITES_H
#define SSSPLAYER_NETWORK_YT_FAVORITES_H

#include "network/yt_client.h"

#define YT_FAVORITES_LIMIT 128

/** A saved channel and the newest upload date last verified on YouTube. */
typedef struct {
    char id[YT_CHANNEL_ID_MAX];
    char name[YT_AUTHOR_MAX];
    char latest_date[YT_DATE_MAX];
    char latest_video_id[YT_ID_MAX];
} YtFavoriteChannel;

/** Two independent persistent favorite collections. */
typedef struct {
    YtSearchResult videos[YT_FAVORITES_LIMIT];
    YtFavoriteChannel channels[YT_FAVORITES_LIMIT];
    int video_count;
    int channel_count;
} YtFavorites;

/** Load video and channel favorites from a versioned JSON database. */
int yt_favorites_load(YtFavorites *favorites);

/** Write a complete favorites snapshot using temporary and backup files. */
int yt_favorites_save(const YtFavorites *favorites);

/** Add or update a saved video using its stable video ID as the key. */
int yt_favorites_add_video(YtFavorites *favorites, const YtSearchResult *video);

/** Add or update a channel using its stable UC channel ID as the key. */
int yt_favorites_add_channel(YtFavorites *favorites, const YtFavoriteChannel *channel);

/** Delete a video favorite by video ID. */
int yt_favorites_remove_video(YtFavorites *favorites, const char *id);

/** Delete a channel favorite by UC channel ID. */
int yt_favorites_remove_channel(YtFavorites *favorites, const char *id);

#endif
