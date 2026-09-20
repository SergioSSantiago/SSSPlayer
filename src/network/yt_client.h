#ifndef SSSPLAYER_NETWORK_YT_CLIENT_H
#define SSSPLAYER_NETWORK_YT_CLIENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YT_MAX_RESULTS 48
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
	/* When set, audio_url is empty but video_url is a muxed progressive MP4
	 * (itag 18) whose AAC track can be remuxed to .m4a after download. */
	int audio_via_progressive;
} YtResolvedMedia;

/* Search via YouTube InnerTube (ANDROID client), following continuation
 * tokens across pages until max_out is filled. Returns count (>=0) or <0. */
int yt_client_search(const char *query, YtSearchResult *out, int max_out,
                     char *detail, size_t detail_size);

/* Resolve progressive video + best audio. Tries ANDROID (reliable progressive)
 * then ANDROID_VR (adaptive audio when the bot-gate allows it). */
int yt_client_resolve(const char *video_id, YtResolvedMedia *out,
                      char *detail, size_t detail_size);

/* Stream-copy the first AAC track from a progressive MP4 into an M4A file. */
int yt_client_remux_audio_m4a(const char *src_mp4, const char *dst_m4a,
                              char *detail, size_t detail_size);

/* Returns 1 if path has an H.264 video track suitable for Vita playback. */
int yt_client_file_has_h264(const char *path);

/* Build a filesystem-safe base name from a title (no extension). */
void yt_client_safe_filename(const char *title, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
