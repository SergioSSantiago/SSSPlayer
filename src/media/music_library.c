#include "media/music_library.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <jansson.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <vita_https.h>

#include "app_paths.h"
#include "media/music_metadata.h"

#define COVER_CACHE_DIR VITAMEDIADECK_DATA_DIR "/music_art/covers"

static int starts_ci(const char *s, const char *prefix) {
	size_t n;
	if (!s || !prefix) return 0;
	n = strlen(prefix);
	return strncasecmp(s, prefix, n) == 0;
}

int sss_path_is_music_tree(const char *path) {
	if (!path || !path[0]) return 0;
	return starts_ci(path, "ux0:/music") || starts_ci(path, "uma0:/music") ||
	       starts_ci(path, "ux0:music") || starts_ci(path, "uma0:music");
}

static int is_audio_name(const char *name) {
	const char *dot;
	if (!name) return 0;
	dot = strrchr(name, '.');
	if (!dot || !dot[1]) return 0;
	return !strcasecmp(dot, ".mp3") || !strcasecmp(dot, ".flac") ||
	       !strcasecmp(dot, ".ogg") || !strcasecmp(dot, ".m4a") ||
	       !strcasecmp(dot, ".aac") || !strcasecmp(dot, ".wav");
}

static void trim_inplace(char *s) {
	char *start, *end;
	if (!s) return;
	start = s;
	while (*start && isspace((unsigned char)*start)) start++;
	if (start != s) memmove(s, start, strlen(start) + 1);
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1])) end--;
	*end = '\0';
}

static void collapse_spaces(char *s) {
	char *r = s, *w = s;
	int space = 0;
	if (!s) return;
	while (*r) {
		if (isspace((unsigned char)*r)) {
			if (!space && w != s) {
				*w++ = ' ';
				space = 1;
			}
			r++;
			continue;
		}
		space = 0;
		*w++ = *r++;
	}
	*w = '\0';
}

static void sss_music_clean_label(char *s) {
	char *p;
	if (!s) return;
	for (p = s; *p; p++) {
		if (*p == '_' || *p == '+') *p = ' ';
	}
	/* Strip leading track numbers: "01 ", "01.", "01 -", "1-" */
	p = s;
	while (isdigit((unsigned char)*p)) p++;
	if (p > s && (*p == '.' || *p == '-' || *p == ')' || isspace((unsigned char)*p))) {
		while (*p && (isspace((unsigned char)*p) || *p == '.' || *p == '-' ||
		              *p == ')'))
			p++;
		if (*p) memmove(s, p, strlen(p) + 1);
	}
	/* Drop trailing " - Copy" / " (1)" noise lightly: only empty brackets */
	trim_inplace(s);
	collapse_spaces(s);
}

static void basename_no_ext(const char *path, char *out, size_t out_size) {
	const char *base, *dot;
	size_t n;
	if (!out || out_size == 0) return;
	out[0] = '\0';
	if (!path) return;
	base = strrchr(path, '/');
	base = base ? base + 1 : path;
	dot = strrchr(base, '.');
	n = dot && dot > base ? (size_t)(dot - base) : strlen(base);
	if (n >= out_size) n = out_size - 1;
	memcpy(out, base, n);
	out[n] = '\0';
}

static int parse_track_no(const char *name) {
	int n = 0;
	if (!name) return 0;
	while (isdigit((unsigned char)*name)) {
		n = n * 10 + (*name - '0');
		name++;
		if (n > 999) return 0;
	}
	if (n > 0 && (*name == '.' || *name == '-' || *name == ' ' || *name == ')'))
		return n;
	return 0;
}

static void split_dashes(const char *src, char *a, size_t a_sz, char *b,
                         size_t b_sz, char *c, size_t c_sz) {
	char buf[256];
	char *p1, *p2;
	snprintf(buf, sizeof(buf), "%s", src ? src : "");
	a[0] = b[0] = c[0] = '\0';
	p1 = strstr(buf, " - ");
	if (!p1) {
		snprintf(a, a_sz, "%s", buf);
		return;
	}
	*p1 = '\0';
	snprintf(a, a_sz, "%s", buf);
	p1 += 3;
	p2 = strstr(p1, " - ");
	if (!p2) {
		snprintf(b, b_sz, "%s", p1);
		return;
	}
	*p2 = '\0';
	snprintf(b, b_sz, "%s", p1);
	snprintf(c, c_sz, "%s", p2 + 3);
}

static void parent_names(const char *path, const char *root, char *artist_dir,
                         size_t artist_sz, char *album_dir, size_t album_sz) {
	char rel[SSS_MUSIC_PATH_MAX];
	char *slash;
	const char *r = root ? root : "";
	size_t root_len = strlen(r);
	artist_dir[0] = album_dir[0] = '\0';
	if (!path) return;
	if (root_len && starts_ci(path, r)) {
		const char *rest = path + root_len;
		while (*rest == '/') rest++;
		snprintf(rel, sizeof(rel), "%s", rest);
	} else {
		snprintf(rel, sizeof(rel), "%s", path);
	}
	slash = strrchr(rel, '/');
	if (slash) *slash = '\0';
	else {
		rel[0] = '\0';
		return;
	}
	slash = strrchr(rel, '/');
	if (slash) {
		snprintf(album_dir, album_sz, "%s", slash + 1);
		*slash = '\0';
		slash = strrchr(rel, '/');
		snprintf(artist_dir, artist_sz, "%s", slash ? slash + 1 : rel);
	} else {
		snprintf(artist_dir, artist_sz, "%s", rel);
	}
	sss_music_clean_label(artist_dir);
	sss_music_clean_label(album_dir);
}

static void find_folder_cover(const char *track_path, char out[SSS_MUSIC_PATH_MAX]) {
	static const char *names[] = {
	    "cover.jpg", "cover.jpeg", "cover.png", "folder.jpg", "folder.jpeg",
	    "folder.png", "AlbumArt.jpg", "AlbumArt.jpeg", "front.jpg", "front.png",
	    NULL};
	char dir[SSS_MUSIC_PATH_MAX];
	char candidate[SSS_MUSIC_PATH_MAX];
	char *slash;
	int i;
	out[0] = '\0';
	snprintf(dir, sizeof(dir), "%s", track_path ? track_path : "");
	slash = strrchr(dir, '/');
	if (slash) *slash = '\0';
	else return;
	for (i = 0; names[i]; i++) {
		SceIoStat st;
		snprintf(candidate, sizeof(candidate), "%s/%s", dir, names[i]);
		memset(&st, 0, sizeof(st));
		if (sceIoGetstat(candidate, &st) >= 0) {
			snprintf(out, SSS_MUSIC_PATH_MAX, "%s", candidate);
			return;
		}
	}
}

static void fill_track_from_path(SssMusicTrack *track, const char *root) {
	VtMusicMetadata meta;
	char base[SSS_MUSIC_TITLE_MAX];
	char part_a[96], part_b[128], part_c[200];
	char artist_dir[96], album_dir[128];
	char folder_art[SSS_MUSIC_PATH_MAX];

	memset(&meta, 0, sizeof(meta));
	vt_music_metadata_load(track->path, &meta);

	basename_no_ext(track->path, base, sizeof(base));
	track->track_no = parse_track_no(base);
	sss_music_clean_label(base);
	split_dashes(base, part_a, sizeof(part_a), part_b, sizeof(part_b),
	             part_c, sizeof(part_c));
	sss_music_clean_label(part_a);
	sss_music_clean_label(part_b);
	sss_music_clean_label(part_c);
	parent_names(track->path, root, artist_dir, sizeof(artist_dir), album_dir,
	             sizeof(album_dir));

	if (meta.title[0])
		snprintf(track->title, sizeof(track->title), "%s", meta.title);
	else if (part_c[0])
		snprintf(track->title, sizeof(track->title), "%s", part_c);
	else if (part_b[0] && !part_c[0])
		snprintf(track->title, sizeof(track->title), "%s", part_b);
	else
		snprintf(track->title, sizeof(track->title), "%s", part_a[0] ? part_a : "Track");

	if (meta.artist[0])
		snprintf(track->artist, sizeof(track->artist), "%s", meta.artist);
	else if (part_c[0] && part_a[0])
		snprintf(track->artist, sizeof(track->artist), "%s", part_a);
	else if (part_b[0] && part_a[0] && !part_c[0])
		snprintf(track->artist, sizeof(track->artist), "%s", part_a);
	else if (artist_dir[0])
		snprintf(track->artist, sizeof(track->artist), "%s", artist_dir);
	else
		snprintf(track->artist, sizeof(track->artist), "Unknown Artist");

	if (meta.album[0])
		snprintf(track->album, sizeof(track->album), "%s", meta.album);
	else if (part_c[0] && part_b[0])
		snprintf(track->album, sizeof(track->album), "%s", part_b);
	else if (album_dir[0])
		snprintf(track->album, sizeof(track->album), "%s", album_dir);
	else
		snprintf(track->album, sizeof(track->album), "Unknown Album");

	sss_music_clean_label(track->title);
	sss_music_clean_label(track->artist);
	sss_music_clean_label(track->album);
	track->duration_ms = meta.duration_ms;

	if (meta.artwork_path[0])
		snprintf(track->artwork, sizeof(track->artwork), "%s",
		         meta.artwork_path);
	else {
		find_folder_cover(track->path, folder_art);
		if (folder_art[0])
			snprintf(track->artwork, sizeof(track->artwork), "%s",
			         folder_art);
	}
}

static int track_cmp(const void *a, const void *b) {
	const SssMusicTrack *ta = a, *tb = b;
	int c = strcasecmp(ta->artist, tb->artist);
	if (c) return c;
	c = strcasecmp(ta->album, tb->album);
	if (c) return c;
	if (ta->track_no != tb->track_no) return ta->track_no - tb->track_no;
	return strcasecmp(ta->title, tb->title);
}

static int scan_dir(SssMusicLibrary *lib, const char *dir, const char *root,
                    int depth) {
	SceUID dh;
	SceIoDirent ent;
	if (!lib || !dir || depth > 12) return 0;
	if (lib->track_count >= SSS_MUSIC_MAX_TRACKS) return 0;
	dh = sceIoDopen(dir);
	if (dh < 0) return 0;
	memset(&ent, 0, sizeof(ent));
	while (sceIoDread(dh, &ent) > 0) {
		char child[SSS_MUSIC_PATH_MAX];
		if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) continue;
		if (ent.d_name[0] == '.') continue;
		snprintf(child, sizeof(child), "%s/%s", dir, ent.d_name);
		if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
			scan_dir(lib, child, root, depth + 1);
		} else if (is_audio_name(ent.d_name) &&
		           lib->track_count < SSS_MUSIC_MAX_TRACKS) {
			SssMusicTrack *t = &lib->tracks[lib->track_count];
			memset(t, 0, sizeof(*t));
			snprintf(t->path, sizeof(t->path), "%s", child);
			fill_track_from_path(t, root);
			lib->track_count++;
		}
		memset(&ent, 0, sizeof(ent));
	}
	sceIoDclose(dh);
	return 0;
}

static void rebuild_aggregates(SssMusicLibrary *lib) {
	int i, a, b;
	free(lib->artists);
	free(lib->albums);
	lib->artists = NULL;
	lib->albums = NULL;
	lib->artist_count = 0;
	lib->album_count = 0;
	if (lib->track_count <= 0) return;

	qsort(lib->tracks, (size_t)lib->track_count, sizeof(SssMusicTrack),
	      track_cmp);

	lib->artists = calloc((size_t)lib->track_count, sizeof(SssMusicArtist));
	lib->albums = calloc((size_t)lib->track_count, sizeof(SssMusicAlbum));
	if (!lib->artists || !lib->albums) return;

	for (i = 0; i < lib->track_count; i++) {
		SssMusicTrack *t = &lib->tracks[i];
		if (lib->artist_count == 0 ||
		    strcasecmp(lib->artists[lib->artist_count - 1].name, t->artist)) {
			SssMusicArtist *art = &lib->artists[lib->artist_count++];
			memset(art, 0, sizeof(*art));
			snprintf(art->name, sizeof(art->name), "%s", t->artist);
			if (t->artwork[0])
				snprintf(art->artwork, sizeof(art->artwork), "%s",
				         t->artwork);
		}
		lib->artists[lib->artist_count - 1].track_count++;

		if (lib->album_count == 0 ||
		    strcasecmp(lib->albums[lib->album_count - 1].name, t->album) ||
		    strcasecmp(lib->albums[lib->album_count - 1].artist, t->artist)) {
			SssMusicAlbum *alb = &lib->albums[lib->album_count++];
			memset(alb, 0, sizeof(*alb));
			snprintf(alb->name, sizeof(alb->name), "%s", t->album);
			snprintf(alb->artist, sizeof(alb->artist), "%s", t->artist);
			alb->first_track = i;
			if (t->artwork[0])
				snprintf(alb->artwork, sizeof(alb->artwork), "%s",
				         t->artwork);
		}
		lib->albums[lib->album_count - 1].track_count++;
		if (!lib->albums[lib->album_count - 1].artwork[0] && t->artwork[0])
			snprintf(lib->albums[lib->album_count - 1].artwork,
			         sizeof(lib->albums[0].artwork), "%s", t->artwork);
		if (!lib->artists[lib->artist_count - 1].artwork[0] && t->artwork[0])
			snprintf(lib->artists[lib->artist_count - 1].artwork,
			         sizeof(lib->artists[0].artwork), "%s", t->artwork);
	}

	for (a = 0; a < lib->artist_count; a++)
		lib->artists[a].album_count = 0;
	for (b = 0; b < lib->album_count; b++) {
		for (a = 0; a < lib->artist_count; a++) {
			if (!strcasecmp(lib->artists[a].name, lib->albums[b].artist)) {
				lib->artists[a].album_count++;
				break;
			}
		}
	}
}

int sss_music_library_scan(SssMusicLibrary *lib, const char *root) {
	char normalized[SSS_MUSIC_PATH_MAX];
	size_t n;
	if (!lib || !root || !root[0]) return -1;
	snprintf(normalized, sizeof(normalized), "%s", root);
	n = strlen(normalized);
	while (n > 0 && normalized[n - 1] == '/') {
		normalized[--n] = '\0';
	}
	memset(lib, 0, sizeof(*lib));
	snprintf(lib->root, sizeof(lib->root), "%s", normalized);
	lib->tracks = calloc(SSS_MUSIC_MAX_TRACKS, sizeof(SssMusicTrack));
	if (!lib->tracks) return -1;
	scan_dir(lib, normalized, normalized, 0);
	rebuild_aggregates(lib);
	return 0;
}

void sss_music_library_free(SssMusicLibrary *lib) {
	if (!lib) return;
	free(lib->tracks);
	free(lib->artists);
	free(lib->albums);
	memset(lib, 0, sizeof(*lib));
}

/* ── Cover art (iTunes Search + local cache) ─────────────────────────────── */

typedef struct {
	char *data;
	size_t size;
	size_t capacity;
} CoverBuf;

static size_t cover_write(const void *data, size_t size, void *opaque) {
	CoverBuf *buf = opaque;
	if (!buf) return 0;
	if (buf->size + size + 1 > buf->capacity) {
		size_t next = buf->capacity ? buf->capacity * 2 : 8192;
		char *grown;
		while (next < buf->size + size + 1) next *= 2;
		if (next > 2 * 1024 * 1024) return 0;
		grown = realloc(buf->data, next);
		if (!grown) return 0;
		buf->data = grown;
		buf->capacity = next;
	}
	memcpy(buf->data + buf->size, data, size);
	buf->size += size;
	buf->data[buf->size] = '\0';
	return size;
}

static void url_encode(const char *in, char *out, size_t out_size) {
	static const char *hex = "0123456789ABCDEF";
	size_t o = 0;
	if (!out || out_size == 0) return;
	out[0] = '\0';
	if (!in) return;
	while (*in && o + 4 < out_size) {
		unsigned char c = (unsigned char)*in++;
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out[o++] = (char)c;
		} else if (c == ' ') {
			out[o++] = '+';
		} else {
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
		}
	}
	out[o] = '\0';
}

static uint32_t cover_hash(const char *artist, const char *album) {
	uint32_t h = 2166136261u;
	const char *p;
	for (p = artist ? artist : ""; *p; p++) {
		h ^= (uint32_t)tolower((unsigned char)*p);
		h *= 16777619u;
	}
	h ^= '|';
	h *= 16777619u;
	for (p = album ? album : ""; *p; p++) {
		h ^= (uint32_t)tolower((unsigned char)*p);
		h *= 16777619u;
	}
	return h;
}

static int http_get_to_file(const char *url, const char *path) {
	VitaHttpsClientConfig config;
	VitaHttpsClient *client;
	VitaHttpsRequest request;
	VitaHttpsResponse response;
	CoverBuf buffer;
	SceUID fd;
	int ok = -1;
	const char *headers[2];

	memset(&config, 0, sizeof(config));
	config.user_agent = "SSSPlayer/1.0 (PS Vita music library)";
	config.connect_timeout_ms = 8000;
	config.request_timeout_ms = 20000;
	config.allow_http = 1;
	client = vita_https_client_create(&config);
	if (!client) return -1;
	memset(&buffer, 0, sizeof(buffer));
	headers[0] = "Accept: */*";
	headers[1] = NULL;
	memset(&request, 0, sizeof(request));
	request.method = "GET";
	request.url = url;
	request.headers = headers;
	request.write = cover_write;
	request.write_opaque = &buffer;
	memset(&response, 0, sizeof(response));
	if (vita_https_perform(client, &request, &response) >= 0 &&
	    response.status_code >= 200 && response.status_code < 300 &&
	    buffer.data && buffer.size > 64) {
		sceIoMkdir(VITAMEDIADECK_DATA_DIR, 0777);
		sceIoMkdir(VITAMEDIADECK_DATA_DIR "/music_art", 0777);
		sceIoMkdir(COVER_CACHE_DIR, 0777);
		fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
		if (fd >= 0) {
			if (sceIoWrite(fd, buffer.data, buffer.size) == (int)buffer.size)
				ok = 0;
			sceIoClose(fd);
			if (ok < 0) sceIoRemove(path);
		}
	}
	free(buffer.data);
	vita_https_client_destroy(client);
	return ok;
}

static int itunes_cover_url(const char *artist, const char *album,
                            char url_out[512]) {
	char term[256], enc[512], api[768];
	CoverBuf buffer;
	VitaHttpsClientConfig config;
	VitaHttpsClient *client;
	VitaHttpsRequest request;
	VitaHttpsResponse response;
	const char *headers[2];
	json_t *root, *results, *item, *art;
	json_error_t error;
	const char *art_url;
	char big[512];
	char *p;
	int ok = -1;

	url_out[0] = '\0';
	snprintf(term, sizeof(term), "%s %s", artist ? artist : "",
	         album ? album : "");
	url_encode(term, enc, sizeof(enc));
	snprintf(api, sizeof(api),
	         "https://itunes.apple.com/search?term=%s&media=music&entity=album&limit=1",
	         enc);

	memset(&config, 0, sizeof(config));
	config.user_agent = "SSSPlayer/1.0";
	config.connect_timeout_ms = 8000;
	config.request_timeout_ms = 15000;
	client = vita_https_client_create(&config);
	if (!client) return -1;
	memset(&buffer, 0, sizeof(buffer));
	headers[0] = "Accept: application/json";
	headers[1] = NULL;
	memset(&request, 0, sizeof(request));
	request.method = "GET";
	request.url = api;
	request.headers = headers;
	request.write = cover_write;
	request.write_opaque = &buffer;
	memset(&response, 0, sizeof(response));
	if (vita_https_perform(client, &request, &response) < 0 ||
	    response.status_code < 200 || response.status_code >= 300 ||
	    !buffer.data) {
		free(buffer.data);
		vita_https_client_destroy(client);
		return -1;
	}
	vita_https_client_destroy(client);
	root = json_loads(buffer.data, 0, &error);
	free(buffer.data);
	if (!root) return -1;
	results = json_object_get(root, "results");
	if (json_is_array(results) && json_array_size(results) > 0) {
		item = json_array_get(results, 0);
		art = json_object_get(item, "artworkUrl100");
		if (!json_is_string(art))
			art = json_object_get(item, "artworkUrl60");
		if (json_is_string(art)) {
			art_url = json_string_value(art);
			snprintf(big, sizeof(big), "%s", art_url);
			p = strstr(big, "100x100");
			if (p) memcpy(p, "600x600", 7);
			else {
				p = strstr(big, "60x60");
				if (p) {
					/* replace with 600x600 — different length, rebuild */
					char tmp[512];
					*p = '\0';
					snprintf(tmp, sizeof(tmp), "%s600x600%s", big, p + 5);
					snprintf(big, sizeof(big), "%s", tmp);
				}
			}
			snprintf(url_out, 512, "%s", big);
			ok = 0;
		}
	}
	json_decref(root);
	return ok;
}

int sss_music_cover_ensure(const char *artist, const char *album,
                           const char *hint_path, char out[SSS_MUSIC_PATH_MAX]) {
	char cache[SSS_MUSIC_PATH_MAX];
	char remote[512];
	SceIoStat st;
	uint32_t hash;

	if (!out) return -1;
	out[0] = '\0';
	if (hint_path && hint_path[0]) {
		memset(&st, 0, sizeof(st));
		if (sceIoGetstat(hint_path, &st) >= 0) {
			snprintf(out, SSS_MUSIC_PATH_MAX, "%s", hint_path);
			return 0;
		}
	}
	hash = cover_hash(artist, album);
	snprintf(cache, sizeof(cache), "%s/%08x.jpg", COVER_CACHE_DIR, hash);
	memset(&st, 0, sizeof(st));
	if (sceIoGetstat(cache, &st) >= 0 && st.st_size > 64) {
		snprintf(out, SSS_MUSIC_PATH_MAX, "%s", cache);
		return 0;
	}
	if (!vita_https_is_connected()) return -1;
	if (itunes_cover_url(artist, album, remote) < 0) return -1;
	if (http_get_to_file(remote, cache) < 0) return -1;
	snprintf(out, SSS_MUSIC_PATH_MAX, "%s", cache);
	return 0;
}
