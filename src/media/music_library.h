#ifndef SSSPLAYER_MUSIC_LIBRARY_H
#define SSSPLAYER_MUSIC_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#define SSS_MUSIC_PATH_MAX 512
#define SSS_MUSIC_TITLE_MAX 200
#define SSS_MUSIC_ARTIST_MAX 96
#define SSS_MUSIC_ALBUM_MAX 128
#define SSS_MUSIC_MAX_TRACKS 2500

typedef struct {
	char path[SSS_MUSIC_PATH_MAX];
	char title[SSS_MUSIC_TITLE_MAX];
	char artist[SSS_MUSIC_ARTIST_MAX];
	char album[SSS_MUSIC_ALBUM_MAX];
	char artwork[SSS_MUSIC_PATH_MAX];
	uint64_t duration_ms;
	int track_no;
} SssMusicTrack;

typedef struct {
	char name[SSS_MUSIC_ARTIST_MAX];
	char artwork[SSS_MUSIC_PATH_MAX];
	int track_count;
	int album_count;
} SssMusicArtist;

typedef struct {
	char name[SSS_MUSIC_ALBUM_MAX];
	char artist[SSS_MUSIC_ARTIST_MAX];
	char artwork[SSS_MUSIC_PATH_MAX];
	int track_count;
	int first_track; /* index into tracks[] (sorted by album) */
} SssMusicAlbum;

typedef struct {
	char root[SSS_MUSIC_PATH_MAX];
	SssMusicTrack *tracks;
	int track_count;
	SssMusicArtist *artists;
	int artist_count;
	SssMusicAlbum *albums;
	int album_count;
} SssMusicLibrary;

/* True for ux0:/music, uma0:/music and any path under them. */
int sss_path_is_music_tree(const char *path);

/* Recursively scan root for audio, fill tags from ID3/folder/filename,
 * clean labels, and build artist/album aggregations. */
int sss_music_library_scan(SssMusicLibrary *lib, const char *root);
void sss_music_library_free(SssMusicLibrary *lib);

/* Ensure album cover on disk (local folder art or iTunes Search). */
int sss_music_cover_ensure(const char *artist, const char *album,
                           const char *hint_path, char out[SSS_MUSIC_PATH_MAX]);

#endif
