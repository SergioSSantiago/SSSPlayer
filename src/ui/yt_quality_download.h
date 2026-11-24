/**
 * @file yt_quality_download.h
 * @brief Expose the experimental YouTube quality chooser to the UI.
 */

#ifndef SSSPLAYER_UI_YT_QUALITY_DOWNLOAD_H
#define SSSPLAYER_UI_YT_QUALITY_DOWNLOAD_H

#include "network/yt_client.h"

/**
 * @brief Select an available video resolution and save a playable MP4.
 * @param media Resolved YouTube URLs, including optional adaptive streams.
 * @param basename Sanitized title without its filename extension.
 * @return Zero after the user closes the dialog or finishes a download.
 * @warning 480p and 720p are experimental and need separate AVC/AAC streams.
 * Network failures or format changes can still prevent a usable MP4.
 */
int ui_yt_quality_download(const YtResolvedMedia *media, const char *basename);

#endif /* SSSPLAYER_UI_YT_QUALITY_DOWNLOAD_H */
