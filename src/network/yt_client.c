#include "network/yt_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>
#include <vita_https.h>

#define YT_RESPONSE_MAX (512 * 1024)

typedef struct {
	unsigned char *data;
	size_t size;
	size_t capacity;
	size_t limit;
} YtBuffer;

static const char *const k_piped_apis[] = {
	"https://pipedapi.kavin.rocks",
	"https://pipedapi.adminforge.de",
	"https://pipedapi.nosebs.ru",
	NULL
};

static const char *const k_invidious_apis[] = {
	"https://inv.nadeko.net",
	"https://invidious.fdn.fr",
	"https://iv.ggtyler.dev",
	NULL
};

static size_t buffer_write(const void *contents, size_t bytes, void *opaque)
{
	YtBuffer *buffer = opaque;
	if (!buffer || !bytes) return bytes;
	if (bytes > buffer->limit - buffer->size) return 0;
	if (buffer->size + bytes + 1 > buffer->capacity) {
		size_t capacity = buffer->capacity ? buffer->capacity * 2 : 8192;
		while (capacity < buffer->size + bytes + 1) capacity *= 2;
		if (capacity > buffer->limit + 1) capacity = buffer->limit + 1;
		unsigned char *next = realloc(buffer->data, capacity);
		if (!next) return 0;
		buffer->data = next;
		buffer->capacity = capacity;
	}
	memcpy(buffer->data + buffer->size, contents, bytes);
	buffer->size += bytes;
	buffer->data[buffer->size] = '\0';
	return bytes;
}

static void buffer_free(YtBuffer *buffer)
{
	if (!buffer) return;
	free(buffer->data);
	memset(buffer, 0, sizeof(*buffer));
}

static void set_detail(char *detail, size_t detail_size, const char *text)
{
	if (!detail || !detail_size) return;
	snprintf(detail, detail_size, "%s", text ? text : "");
}

static int http_get_json(const char *url, YtBuffer *buffer, char *detail,
                         size_t detail_size)
{
	VitaHttpsClientConfig config;
	VitaHttpsClient *client;
	VitaHttpsRequest request;
	VitaHttpsResponse response;
	int result;

	memset(&config, 0, sizeof(config));
	config.user_agent = "Mozilla/5.0 (PlayStation Vita) SSSPlayer/1.0";
	config.connect_timeout_ms = 8000;
	config.request_timeout_ms = 20000;
	config.allow_http = 0;
	client = vita_https_client_create(&config);
	if (!client) {
		set_detail(detail, detail_size, "HTTPS unavailable");
		return -1;
	}

	memset(buffer, 0, sizeof(*buffer));
	buffer->limit = YT_RESPONSE_MAX;
	memset(&request, 0, sizeof(request));
	request.method = "GET";
	request.url = url;
	request.write = buffer_write;
	request.write_opaque = buffer;
	memset(&response, 0, sizeof(response));
	result = vita_https_perform(client, &request, &response);
	vita_https_client_destroy(client);
	if (result < 0 || response.status_code < 200 || response.status_code >= 300 ||
	    !buffer->data || !buffer->size) {
		set_detail(detail, detail_size,
		           result < 0 ? vita_https_error_string(result)
		                      : "Mirror returned an error");
		buffer_free(buffer);
		return -1;
	}
	return 0;
}

static void copy_field(char *dst, size_t dst_size, const char *src)
{
	if (!dst || !dst_size) return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	snprintf(dst, dst_size, "%s", src);
}

static int looks_like_video_id(const char *id)
{
	size_t n;
	if (!id) return 0;
	n = strlen(id);
	if (n < 6 || n >= YT_ID_MAX) return 0;
	for (; *id; id++) {
		if (!isalnum((unsigned char)*id) && *id != '-' && *id != '_')
			return 0;
	}
	return 1;
}

static int parse_piped_search(const char *json, YtSearchResult *out, int max_out)
{
	json_t *root;
	json_t *items;
	json_error_t error;
	size_t i;
	int count = 0;

	root = json_loads(json, 0, &error);
	if (!root) return -1;
	items = json_is_array(root) ? root : json_object_get(root, "items");
	if (!json_is_array(items)) {
		json_decref(root);
		return -1;
	}
	for (i = 0; i < json_array_size(items) && count < max_out; i++) {
		json_t *item = json_array_get(items, i);
		const char *type = json_string_value(json_object_get(item, "type"));
		const char *url = json_string_value(json_object_get(item, "url"));
		const char *title = json_string_value(json_object_get(item, "title"));
		const char *uploader = json_string_value(json_object_get(item, "uploaderName"));
		json_t *duration = json_object_get(item, "duration");
		json_t *views = json_object_get(item, "views");
		const char *id = NULL;
		YtSearchResult *dst;

		if (type && strcmp(type, "stream") != 0 && strcmp(type, "video") != 0)
			continue;
		if (url && strstr(url, "watch?v="))
			id = strstr(url, "watch?v=") + 8;
		else if (url && url[0] == '/')
			id = url + 1;
		if (!looks_like_video_id(id)) continue;

		dst = &out[count++];
		memset(dst, 0, sizeof(*dst));
		copy_field(dst->id, sizeof(dst->id), id);
		/* Strip query junk from id if any */
		{
			char *q = strchr(dst->id, '&');
			if (q) *q = '\0';
		}
		copy_field(dst->title, sizeof(dst->title), title ? title : "Untitled");
		copy_field(dst->author, sizeof(dst->author), uploader ? uploader : "");
		if (json_is_integer(duration))
			dst->length_seconds = (int)json_integer_value(duration);
		if (json_is_integer(views))
			dst->view_count = (int)json_integer_value(views);
	}
	json_decref(root);
	return count;
}

static int parse_invidious_search(const char *json, YtSearchResult *out, int max_out)
{
	json_t *root;
	json_error_t error;
	size_t i;
	int count = 0;

	root = json_loads(json, 0, &error);
	if (!root || !json_is_array(root)) {
		if (root) json_decref(root);
		return -1;
	}
	for (i = 0; i < json_array_size(root) && count < max_out; i++) {
		json_t *item = json_array_get(root, i);
		const char *type = json_string_value(json_object_get(item, "type"));
		const char *id = json_string_value(json_object_get(item, "videoId"));
		const char *title = json_string_value(json_object_get(item, "title"));
		const char *author = json_string_value(json_object_get(item, "author"));
		json_t *length = json_object_get(item, "lengthSeconds");
		json_t *views = json_object_get(item, "viewCount");
		YtSearchResult *dst;

		if (type && strcmp(type, "video") != 0) continue;
		if (!looks_like_video_id(id)) continue;
		dst = &out[count++];
		memset(dst, 0, sizeof(*dst));
		copy_field(dst->id, sizeof(dst->id), id);
		copy_field(dst->title, sizeof(dst->title), title ? title : "Untitled");
		copy_field(dst->author, sizeof(dst->author), author ? author : "");
		if (json_is_integer(length))
			dst->length_seconds = (int)json_integer_value(length);
		if (json_is_integer(views))
			dst->view_count = (int)json_integer_value(views);
	}
	json_decref(root);
	return count;
}

static int pick_ext_from_mime(const char *mime, const char *fallback,
                              char *out, size_t out_size)
{
	if (mime) {
		if (strstr(mime, "audio/mpeg") || strstr(mime, "audio/mp3"))
			return snprintf(out, out_size, "mp3") > 0;
		if (strstr(mime, "audio/mp4") || strstr(mime, "audio/aac"))
			return snprintf(out, out_size, "m4a") > 0;
		if (strstr(mime, "audio/webm") || strstr(mime, "audio/opus"))
			return snprintf(out, out_size, "webm") > 0;
		if (strstr(mime, "video/mp4"))
			return snprintf(out, out_size, "mp4") > 0;
		if (strstr(mime, "video/webm"))
			return snprintf(out, out_size, "webm") > 0;
	}
	return snprintf(out, out_size, "%s", fallback ? fallback : "bin") > 0;
}

static int parse_piped_streams(const char *json, YtResolvedMedia *out)
{
	json_t *root;
	json_error_t error;
	json_t *videos;
	json_t *audios;
	size_t i;
	int best_video_h = -1;
	int best_audio_br = -1;

	root = json_loads(json, 0, &error);
	if (!root) return -1;

	memset(out, 0, sizeof(*out));
	copy_field(out->title, sizeof(out->title),
	           json_string_value(json_object_get(root, "title")));
	copy_field(out->author, sizeof(out->author),
	           json_string_value(json_object_get(root, "uploader")));
	if (json_is_integer(json_object_get(root, "duration")))
		out->length_seconds = (int)json_integer_value(json_object_get(root, "duration"));

	videos = json_object_get(root, "videoStreams");
	if (json_is_array(videos)) {
		for (i = 0; i < json_array_size(videos); i++) {
			json_t *v = json_array_get(videos, i);
			const char *url = json_string_value(json_object_get(v, "url"));
			const char *mime = json_string_value(json_object_get(v, "mimeType"));
			const char *codec = json_string_value(json_object_get(v, "codec"));
			json_t *video_only = json_object_get(v, "videoOnly");
			json_t *height = json_object_get(v, "height");
			int h = json_is_integer(height) ? (int)json_integer_value(height) : 0;
			int is_video_only = json_is_true(video_only);
			/* Prefer progressive mp4 with audio (not videoOnly) for Vita HW decode. */
			if (!url || !url[0]) continue;
			if (is_video_only) continue;
			if (mime && !strstr(mime, "mp4") && !(codec && strstr(codec, "avc")))
				continue;
			if (h > best_video_h && h <= 720) {
				best_video_h = h;
				copy_field(out->video_url, sizeof(out->video_url), url);
				pick_ext_from_mime(mime, "mp4", out->video_ext, sizeof(out->video_ext));
			}
		}
		/* Fallback: any non-videoOnly stream */
		if (!out->video_url[0]) {
			for (i = 0; i < json_array_size(videos); i++) {
				json_t *v = json_array_get(videos, i);
				const char *url = json_string_value(json_object_get(v, "url"));
				json_t *video_only = json_object_get(v, "videoOnly");
				if (!url || json_is_true(video_only)) continue;
				copy_field(out->video_url, sizeof(out->video_url), url);
				pick_ext_from_mime(json_string_value(json_object_get(v, "mimeType")),
				                   "mp4", out->video_ext, sizeof(out->video_ext));
				break;
			}
		}
	}

	audios = json_object_get(root, "audioStreams");
	if (json_is_array(audios)) {
		for (i = 0; i < json_array_size(audios); i++) {
			json_t *a = json_array_get(audios, i);
			const char *url = json_string_value(json_object_get(a, "url"));
			const char *mime = json_string_value(json_object_get(a, "mimeType"));
			json_t *bitrate = json_object_get(a, "bitrate");
			int br = json_is_integer(bitrate) ? (int)json_integer_value(bitrate) : 0;
			int prefer_mp3 = mime && (strstr(mime, "mpeg") || strstr(mime, "mp3"));
			int prefer_m4a = mime && strstr(mime, "mp4");
			if (!url || !url[0]) continue;
			/* Prefer mp3, then m4a/aac, then highest bitrate. */
			if (prefer_mp3 ||
			    (prefer_m4a && best_audio_br < 1000000) ||
			    br > best_audio_br) {
				if (prefer_mp3) best_audio_br = 2000000;
				else if (prefer_m4a && br < 1000000) best_audio_br = 1000000 + br;
				else best_audio_br = br;
				copy_field(out->audio_url, sizeof(out->audio_url), url);
				pick_ext_from_mime(mime, "m4a", out->audio_ext, sizeof(out->audio_ext));
			}
		}
	}

	json_decref(root);
	return out->video_url[0] || out->audio_url[0] ? 0 : -1;
}

static int parse_invidious_video(const char *json, YtResolvedMedia *out)
{
	json_t *root;
	json_error_t error;
	json_t *formats;
	json_t *adaptive;
	size_t i;
	int best_h = -1;
	int best_audio = -1;

	root = json_loads(json, 0, &error);
	if (!root) return -1;
	memset(out, 0, sizeof(*out));
	copy_field(out->title, sizeof(out->title),
	           json_string_value(json_object_get(root, "title")));
	copy_field(out->author, sizeof(out->author),
	           json_string_value(json_object_get(root, "author")));
	if (json_is_integer(json_object_get(root, "lengthSeconds")))
		out->length_seconds =
		    (int)json_integer_value(json_object_get(root, "lengthSeconds"));

	formats = json_object_get(root, "formatStreams");
	if (json_is_array(formats)) {
		for (i = 0; i < json_array_size(formats); i++) {
			json_t *f = json_array_get(formats, i);
			const char *url = json_string_value(json_object_get(f, "url"));
			const char *type = json_string_value(json_object_get(f, "type"));
			const char *quality = json_string_value(json_object_get(f, "qualityLabel"));
			int h = 0;
			if (!url) continue;
			if (type && !strstr(type, "mp4")) continue;
			if (quality) sscanf(quality, "%dp", &h);
			if (h > best_h && h <= 720) {
				best_h = h;
				copy_field(out->video_url, sizeof(out->video_url), url);
				snprintf(out->video_ext, sizeof(out->video_ext), "mp4");
			}
		}
		if (!out->video_url[0]) {
			for (i = 0; i < json_array_size(formats); i++) {
				json_t *f = json_array_get(formats, i);
				const char *url = json_string_value(json_object_get(f, "url"));
				if (!url) continue;
				copy_field(out->video_url, sizeof(out->video_url), url);
				snprintf(out->video_ext, sizeof(out->video_ext), "mp4");
				break;
			}
		}
	}

	adaptive = json_object_get(root, "adaptiveFormats");
	if (json_is_array(adaptive)) {
		for (i = 0; i < json_array_size(adaptive); i++) {
			json_t *f = json_array_get(adaptive, i);
			const char *url = json_string_value(json_object_get(f, "url"));
			const char *type = json_string_value(json_object_get(f, "type"));
			json_t *bitrate = json_object_get(f, "bitrate");
			int br = json_is_integer(bitrate) ? (int)json_integer_value(bitrate) : 0;
			if (!url || !type || strncmp(type, "audio/", 6) != 0) continue;
			if (strstr(type, "mpeg") || strstr(type, "mp3")) {
				best_audio = 2000000;
				copy_field(out->audio_url, sizeof(out->audio_url), url);
				snprintf(out->audio_ext, sizeof(out->audio_ext), "mp3");
			} else if ((strstr(type, "mp4") || strstr(type, "aac")) &&
			           best_audio < 1000000) {
				best_audio = 1000000 + br;
				copy_field(out->audio_url, sizeof(out->audio_url), url);
				snprintf(out->audio_ext, sizeof(out->audio_ext), "m4a");
			} else if (br > best_audio) {
				best_audio = br;
				copy_field(out->audio_url, sizeof(out->audio_url), url);
				pick_ext_from_mime(type, "m4a", out->audio_ext, sizeof(out->audio_ext));
			}
		}
	}

	json_decref(root);
	return out->video_url[0] || out->audio_url[0] ? 0 : -1;
}

int yt_client_search(const char *query, YtSearchResult *out, int max_out,
                     char *detail, size_t detail_size)
{
	char *escaped;
	char url[1024];
	YtBuffer buffer;
	int i;
	int count = -1;

	if (!query || !query[0] || !out || max_out <= 0) {
		set_detail(detail, detail_size, "Empty search");
		return -1;
	}
	if (!vita_https_is_connected()) {
		set_detail(detail, detail_size, "No Wi-Fi connection");
		return -1;
	}

	escaped = vita_https_escape(query, strlen(query));
	if (!escaped) {
		set_detail(detail, detail_size, "Out of memory");
		return -1;
	}

	for (i = 0; k_piped_apis[i]; i++) {
		snprintf(url, sizeof(url), "%s/search?q=%s&filter=videos",
		         k_piped_apis[i], escaped);
		if (http_get_json(url, &buffer, detail, detail_size) < 0) continue;
		count = parse_piped_search((const char *)buffer.data, out, max_out);
		buffer_free(&buffer);
		if (count > 0) {
			vita_https_free(escaped);
			set_detail(detail, detail_size, "");
			return count;
		}
	}

	for (i = 0; k_invidious_apis[i]; i++) {
		snprintf(url, sizeof(url), "%s/api/v1/search?q=%s&type=video",
		         k_invidious_apis[i], escaped);
		if (http_get_json(url, &buffer, detail, detail_size) < 0) continue;
		count = parse_invidious_search((const char *)buffer.data, out, max_out);
		buffer_free(&buffer);
		if (count > 0) {
			vita_https_free(escaped);
			set_detail(detail, detail_size, "");
			return count;
		}
	}

	vita_https_free(escaped);
	if (detail && detail_size && !detail[0])
		set_detail(detail, detail_size, "No results from available mirrors");
	return count < 0 ? -1 : 0;
}

int yt_client_resolve(const char *video_id, YtResolvedMedia *out,
                      char *detail, size_t detail_size)
{
	char url[512];
	YtBuffer buffer;
	int i;

	if (!looks_like_video_id(video_id) || !out) {
		set_detail(detail, detail_size, "Invalid video id");
		return -1;
	}
	if (!vita_https_is_connected()) {
		set_detail(detail, detail_size, "No Wi-Fi connection");
		return -1;
	}

	for (i = 0; k_piped_apis[i]; i++) {
		snprintf(url, sizeof(url), "%s/streams/%s", k_piped_apis[i], video_id);
		if (http_get_json(url, &buffer, detail, detail_size) < 0) continue;
		if (parse_piped_streams((const char *)buffer.data, out) == 0) {
			buffer_free(&buffer);
			set_detail(detail, detail_size, "");
			return 0;
		}
		buffer_free(&buffer);
	}

	for (i = 0; k_invidious_apis[i]; i++) {
		snprintf(url, sizeof(url), "%s/api/v1/videos/%s",
		         k_invidious_apis[i], video_id);
		if (http_get_json(url, &buffer, detail, detail_size) < 0) continue;
		if (parse_invidious_video((const char *)buffer.data, out) == 0) {
			buffer_free(&buffer);
			set_detail(detail, detail_size, "");
			return 0;
		}
		buffer_free(&buffer);
	}

	set_detail(detail, detail_size, "Could not resolve media streams");
	return -1;
}

void yt_client_safe_filename(const char *title, char *out, size_t out_size)
{
	size_t n = 0;
	if (!out || !out_size) return;
	if (!title || !title[0]) {
		snprintf(out, out_size, "download");
		return;
	}
	while (*title && n + 1 < out_size && n < 80) {
		unsigned char c = (unsigned char)*title++;
		if (isalnum(c) || c == '-' || c == '_' || c == '.')
			out[n++] = (char)c;
		else if (c == ' ' || c == '\t')
			out[n++] = '_';
	}
	out[n] = '\0';
	if (!out[0]) snprintf(out, out_size, "download");
}
