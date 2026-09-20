#ifndef SSSPLAYER_NETWORK_YT_CLIENT_H
#define SSSPLAYER_NETWORK_YT_CLIENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YT_MAX_RESULTS 24
#define YT_ID_MAX 24
#define YT_TITLE_MAX 160
#define YT_AUTHOR_MAX 96
/* googlevideo playback URLs regularly exceed 2 KB once signed. */
#define YT_URL_MAX 4096

typedef struct {
	char id[YT_ID_MAX];
	char title[YT_TITLE_MAX];
	char author[YT_AUTHOR_MAX];
	int length_seconds;
	int view_count;
} YtSearchResult;

typedef struct {
	char video_url[YT_URL_MAX];
	char audio_url[YT_URL_MAX];
	char video_ext[8];   /* mp4, webm, … */
	char audio_ext[8];   /* m4a, webm, mp3, … */
	char title[YT_TITLE_MAX];
	char author[YT_AUTHOR_MAX];
	int length_seconds;
} YtResolvedMedia;

/* Search via YouTube InnerTube (ANDROID client). Returns count (>=0) or <0
 * on error. detail may receive a short human-readable reason. */
int yt_client_search(const char *query, YtSearchResult *out, int max_out,
                     char *detail, size_t detail_size);

/* Resolve progressive video + best audio stream URLs (ANDROID_VR client
 * returns plain signed URLs without a JS cipher). */
int yt_client_resolve(const char *video_id, YtResolvedMedia *out,
                      char *detail, size_t detail_size);

/* Build a filesystem-safe base name from a title (no extension). */
void yt_client_safe_filename(const char *title, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
