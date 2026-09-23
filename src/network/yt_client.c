/*
 * YouTube client via InnerTube (ViTube / yt-dlp Android-style clients).
 * Search: ANDROID + continuation pages.
 * Resolve: ANDROID for progressive + adaptive (≤720p H.264). Prefer an older
 * ANDROID build that still returns plain CDN URLs; newer builds (21.x+) often
 * omit adaptive URLs (SABR). ANDROID_VR remains a fallback when the bot-gate
 * allows it. GPL-3.0 — approach inspired by vitube-vpk.
 */
#include "network/yt_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <vita_https.h>

#define YT_RESPONSE_MAX (1536 * 1024)
#define YT_CONTINUATION_MAX 1024
#define YT_SEARCH_PAGES 4

#define IT_BASE "https://youtubei.googleapis.com/youtubei/v1/"

/* 21.x often lists adaptiveFormats without playable URLs. 20.10.x still
 * returns googlevideo CDN links for H.264 ≤720p + AAC. */
#define IT_ANDROID_VERSION "20.10.38"
#define IT_ANDROID_UA \
	"com.google.android.youtube/" IT_ANDROID_VERSION \
	" (Linux; U; Android 11) gzip"

#define IT_VR_VERSION "1.60.19"
#define IT_VR_UA \
	"com.google.android.apps.youtube.vr.oculus/" IT_VR_VERSION \
	" (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip"

/* responseContext keeps visitorData; continuation lives under contents. */
#define IT_LIST_FIELDS "contents,continuationContents,responseContext"

typedef struct {
	unsigned char *data;
	size_t size;
	size_t capacity;
	size_t limit;
} YtBuffer;

static char g_visitor_data[768];
static char g_visitor_header[832];

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

static void copy_field(char *dst, size_t dst_size, const char *src)
{
	if (!dst || !dst_size) return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	snprintf(dst, dst_size, "%s", src);
}

static void harvest_visitor_data(const char *body)
{
	static const char key[] = "\"visitorData\":\"";
	const char *start;
	const char *end;
	size_t len;

	if (!body) return;
	start = strstr(body, key);
	if (!start) return;
	start += sizeof(key) - 1;
	end = strchr(start, '"');
	if (!end || end == start) return;
	len = (size_t)(end - start);
	if (len >= sizeof(g_visitor_data)) return;
	if (memcmp(g_visitor_data, start, len) == 0 && g_visitor_data[len] == '\0')
		return;
	memcpy(g_visitor_data, start, len);
	g_visitor_data[len] = '\0';
	snprintf(g_visitor_header, sizeof(g_visitor_header),
	         "X-Goog-Visitor-Id: %s", g_visitor_data);
}

static int looks_like_video_id(const char *id)
{
	size_t n;
	if (!id) return 0;
	n = strlen(id);
	if (n != 11) return 0;
	for (; *id; id++) {
		if (!isalnum((unsigned char)*id) && *id != '-' && *id != '_')
			return 0;
	}
	return 1;
}

static void json_text_into(json_t *node, char *out, size_t out_size)
{
	const char *simple;
	json_t *runs;
	size_t i;
	size_t n = 0;

	if (!out || !out_size) return;
	out[0] = '\0';
	if (!node) return;
	simple = json_string_value(json_object_get(node, "simpleText"));
	if (simple) {
		snprintf(out, out_size, "%s", simple);
		return;
	}
	runs = json_object_get(node, "runs");
	if (!json_is_array(runs)) {
		simple = json_string_value(node);
		if (simple) snprintf(out, out_size, "%s", simple);
		return;
	}
	for (i = 0; i < json_array_size(runs); i++) {
		const char *piece =
		    json_string_value(json_object_get(json_array_get(runs, i), "text"));
		if (!piece) continue;
		n += (size_t)snprintf(out + n, out_size - n, "%s", piece);
		if (n >= out_size - 1) break;
	}
}

static int parse_duration_text(const char *text)
{
	int parts[3] = {0, 0, 0};
	int n = 0;
	const char *p = text;
	if (!text || !text[0]) return 0;
	while (*p && n < 3) {
		if (!isdigit((unsigned char)*p)) {
			p++;
			continue;
		}
		parts[n++] = (int)strtol(p, (char **)&p, 10);
		if (*p == ':') p++;
	}
	if (n == 1) return parts[0];
	if (n == 2) return parts[0] * 60 + parts[1];
	if (n == 3) return parts[0] * 3600 + parts[1] * 60 + parts[2];
	return 0;
}

static int pick_ext_from_mime(const char *mime, const char *fallback,
                              char *out, size_t out_size)
{
	if (mime) {
		/* Video first: progressive mime is often
		 * video/mp4; codecs="avc1..., mp4a..." — do NOT treat mp4a as audio. */
		if (strncmp(mime, "video/", 6) == 0) {
			if (strstr(mime, "mp4") || strstr(mime, "avc"))
				return snprintf(out, out_size, "mp4") > 0;
			if (strstr(mime, "webm"))
				return snprintf(out, out_size, "webm") > 0;
		}
		if (strncmp(mime, "audio/", 6) == 0) {
			if (strstr(mime, "mpeg") || strstr(mime, "mp3"))
				return snprintf(out, out_size, "mp3") > 0;
			if (strstr(mime, "mp4") || strstr(mime, "aac") ||
			    strstr(mime, "mp4a"))
				return snprintf(out, out_size, "m4a") > 0;
			if (strstr(mime, "webm") || strstr(mime, "opus"))
				return snprintf(out, out_size, "webm") > 0;
		}
	}
	return snprintf(out, out_size, "%s", fallback ? fallback : "bin") > 0;
}

typedef enum {
	IT_CLIENT_ANDROID = 0,
	IT_CLIENT_ANDROID_VR
} ItClient;

static json_t *client_context(ItClient client)
{
	if (client == IT_CLIENT_ANDROID_VR) {
		return json_pack(
		    "{s:{s:s,s:s,s:s,s:s,s:i,s:s,s:s,s:s,s:s,s:s}}",
		    "client",
		    "clientName", "ANDROID_VR",
		    "clientVersion", IT_VR_VERSION,
		    "deviceMake", "Oculus",
		    "deviceModel", "Quest 3",
		    "androidSdkVersion", 32,
		    "userAgent", IT_VR_UA,
		    "osName", "Android",
		    "osVersion", "12L",
		    "hl", "en",
		    "gl", "US");
	}
	return json_pack(
	    "{s:{s:s,s:s,s:i,s:s,s:s,s:s,s:s,s:s}}",
	    "client",
	    "clientName", "ANDROID",
	    "clientVersion", IT_ANDROID_VERSION,
	    "androidSdkVersion", 30,
	    "userAgent", IT_ANDROID_UA,
	    "osName", "Android",
	    "osVersion", "11",
	    "hl", "en",
	    "gl", "US");
}

static int it_post(const char *endpoint, const char *fields, json_t *body_extra,
                   ItClient client, YtBuffer *buffer, char *detail,
                   size_t detail_size)
{
	json_t *body = body_extra ? body_extra : json_object();
	char *payload;
	char url[640];
	char *escaped_fields = NULL;
	VitaHttpsClientConfig config;
	VitaHttpsClient *https = NULL;
	VitaHttpsRequest request;
	VitaHttpsResponse response;
	const char *headers[8];
	int n = 0;
	int result;
	const char *ua =
	    client == IT_CLIENT_ANDROID_VR ? IT_VR_UA : IT_ANDROID_UA;

	json_object_set_new(body, "context", client_context(client));
	payload = json_dumps(body, JSON_COMPACT);
	json_decref(body);
	if (!payload) {
		set_detail(detail, detail_size, "Out of memory");
		return -1;
	}

	if (fields && fields[0]) {
		escaped_fields = vita_https_escape(fields, strlen(fields));
		snprintf(url, sizeof(url), "%s%s?prettyPrint=false&fields=%s",
		         IT_BASE, endpoint,
		         escaped_fields ? escaped_fields : "");
	} else {
		snprintf(url, sizeof(url), "%s%s?prettyPrint=false", IT_BASE,
		         endpoint);
	}

	memset(&config, 0, sizeof(config));
	config.user_agent = ua;
	config.connect_timeout_ms = 10000;
	config.request_timeout_ms = 30000;
	config.allow_http = 0;
	https = vita_https_client_create(&config);
	if (!https) {
		free(payload);
		vita_https_free(escaped_fields);
		set_detail(detail, detail_size, "HTTPS unavailable");
		return -1;
	}

	headers[n++] = "Content-Type: application/json";
	if (client == IT_CLIENT_ANDROID_VR) {
		headers[n++] = "X-Youtube-Client-Name: 28";
		headers[n++] = "X-Youtube-Client-Version: " IT_VR_VERSION;
	} else {
		headers[n++] = "X-Youtube-Client-Name: 3";
		headers[n++] = "X-Youtube-Client-Version: " IT_ANDROID_VERSION;
	}
	if (g_visitor_header[0]) headers[n++] = g_visitor_header;
	headers[n] = NULL;

	memset(buffer, 0, sizeof(*buffer));
	buffer->limit = YT_RESPONSE_MAX;
	memset(&request, 0, sizeof(request));
	request.method = "POST";
	request.url = url;
	request.headers = headers;
	request.body = payload;
	request.body_size = strlen(payload);
	request.write = buffer_write;
	request.write_opaque = buffer;
	memset(&response, 0, sizeof(response));
	result = vita_https_perform(https, &request, &response);
	vita_https_client_destroy(https);
	free(payload);
	vita_https_free(escaped_fields);

	if (result < 0 || response.status_code < 200 ||
	    response.status_code >= 300 || !buffer->data || !buffer->size) {
		set_detail(detail, detail_size,
		           result < 0 ? vita_https_error_string(result)
		                      : "YouTube API returned an error");
		buffer_free(buffer);
		return -1;
	}
	harvest_visitor_data((const char *)buffer->data);
	return 0;
}

static int already_have_id(const YtSearchResult *out, int count, const char *id)
{
	int i;
	for (i = 0; i < count; i++) {
		if (strcmp(out[i].id, id) == 0) return 1;
	}
	return 0;
}

static void try_add_compact_video(json_t *renderer, YtSearchResult *out,
                                  int max_out, int *count)
{
	const char *id;
	char title[YT_TITLE_MAX];
	char author[YT_AUTHOR_MAX];
	char length[32];
	YtSearchResult *dst;

	if (!renderer || !count || *count >= max_out) return;
	id = json_string_value(json_object_get(renderer, "videoId"));
	if (!looks_like_video_id(id) || already_have_id(out, *count, id)) return;
	json_text_into(json_object_get(renderer, "title"), title, sizeof(title));
	json_text_into(json_object_get(renderer, "shortBylineText"), author,
	               sizeof(author));
	if (!author[0])
		json_text_into(json_object_get(renderer, "longBylineText"), author,
		               sizeof(author));
	json_text_into(json_object_get(renderer, "lengthText"), length,
	               sizeof(length));

	dst = &out[(*count)++];
	memset(dst, 0, sizeof(*dst));
	copy_field(dst->id, sizeof(dst->id), id);
	copy_field(dst->title, sizeof(dst->title),
	           title[0] ? title : "Untitled");
	copy_field(dst->author, sizeof(dst->author), author);
	dst->length_seconds = parse_duration_text(length);
}

static void walk_search(json_t *node, YtSearchResult *out, int max_out,
                        int *count, char *continuation, size_t cont_size)
{
	if (!node || !count) return;
	if (json_is_object(node)) {
		const char *key;
		json_t *value;
		json_t *compact = json_object_get(node, "compactVideoRenderer");
		json_t *video = json_object_get(node, "videoRenderer");
		json_t *ncd = json_object_get(node, "nextContinuationData");
		if (compact && *count < max_out)
			try_add_compact_video(compact, out, max_out, count);
		if (video && *count < max_out)
			try_add_compact_video(video, out, max_out, count);
		if (ncd && continuation && cont_size) {
			const char *token =
			    json_string_value(json_object_get(ncd, "continuation"));
			if (token && token[0])
				copy_field(continuation, cont_size, token);
		}
		json_object_foreach(node, key, value) {
			(void)key;
			walk_search(value, out, max_out, count, continuation, cont_size);
		}
	} else if (json_is_array(node)) {
		size_t i;
		for (i = 0; i < json_array_size(node); i++) {
			walk_search(json_array_get(node, i), out, max_out, count,
			            continuation, cont_size);
		}
	}
}

static int parse_innertube_search(const char *json, YtSearchResult *out,
                                  int max_out, char *continuation,
                                  size_t cont_size)
{
	json_t *root;
	json_error_t error;
	int count = 0;

	if (continuation && cont_size) continuation[0] = '\0';
	root = json_loads(json, 0, &error);
	if (!root) return -1;
	walk_search(root, out, max_out, &count, continuation, cont_size);
	json_decref(root);
	return count;
}

static int parse_innertube_player(const char *json, YtResolvedMedia *out)
{
	json_t *root;
	json_error_t error;
	json_t *details;
	json_t *sd;
	json_t *formats;
	json_t *adaptive;
	json_t *playability;
	size_t i;
	int best_audio = -1;
	const char *status;

	root = json_loads(json, 0, &error);
	if (!root) return -1;
	memset(out, 0, sizeof(*out));

	playability = json_object_get(root, "playabilityStatus");
	status = json_string_value(json_object_get(playability, "status"));
	if (status && strcmp(status, "OK") != 0) {
		json_decref(root);
		return -1;
	}

	details = json_object_get(root, "videoDetails");
	if (details) {
		copy_field(out->title, sizeof(out->title),
		           json_string_value(json_object_get(details, "title")));
		copy_field(out->author, sizeof(out->author),
		           json_string_value(json_object_get(details, "author")));
		{
			const char *len =
			    json_string_value(json_object_get(details, "lengthSeconds"));
			if (len) out->length_seconds = (int)strtol(len, NULL, 10);
		}
	}

	sd = json_object_get(root, "streamingData");
	formats = sd ? json_object_get(sd, "formats") : NULL;
	if (json_is_array(formats)) {
		int best_score = -1;
		for (i = 0; i < json_array_size(formats); i++) {
			json_t *f = json_array_get(formats, i);
			const char *url = json_string_value(json_object_get(f, "url"));
			const char *mime =
			    json_string_value(json_object_get(f, "mimeType"));
			json_t *height = json_object_get(f, "height");
			json_t *itag_node = json_object_get(f, "itag");
			int h = json_is_integer(height) ? (int)json_integer_value(height)
			                                : 0;
			int itag = json_is_integer(itag_node)
			         ? (int)json_integer_value(itag_node) : 0;
			int score;
			if (!url || !url[0]) continue;
			/* Vita hardware decode needs H.264 (avc1). Reject AV1/VP9. */
			if (!mime || !strstr(mime, "avc1")) continue;
			if (h <= 0) {
				const char *label =
				    json_string_value(json_object_get(f, "qualityLabel"));
				if (label) sscanf(label, "%dp", &h);
			}
			/* Cap at 360p: 720p High-profile progressive often opens then
			 * dies on sceVideodec after the first GOP. Prefer itag 18. */
			if (h > 360) continue;
			if (itag == 18) score = 10000;
			else if (h > 0) score = 1000 + h;
			else score = 1;
			if (score > best_score) {
				best_score = score;
				copy_field(out->video_url, sizeof(out->video_url), url);
				snprintf(out->video_ext, sizeof(out->video_ext), "mp4");
				out->progressive_height = h > 0 ? h : (itag == 18 ? 360 : 0);
			}
		}
	}

	adaptive = sd ? json_object_get(sd, "adaptiveFormats") : NULL;
	if (json_is_array(adaptive)) {
		int best_video = -1;
		for (i = 0; i < json_array_size(adaptive); i++) {
			json_t *f = json_array_get(adaptive, i);
			const char *url = json_string_value(json_object_get(f, "url"));
			const char *mime =
			    json_string_value(json_object_get(f, "mimeType"));
			json_t *height = json_object_get(f, "height");
			json_t *bitrate = json_object_get(f, "bitrate");
			int h = json_is_integer(height) ? (int)json_integer_value(height)
			                                : 0;
			int br = json_is_integer(bitrate) ? (int)json_integer_value(bitrate)
			                                  : 0;
			int prefer_mp3;
			int prefer_m4a;
			int score;
			if (!url || !url[0] || !mime) continue;
			if (strncmp(mime, "video/", 6) == 0 && strstr(mime, "avc1")) {
				if (h <= 0) {
					const char *label =
					    json_string_value(json_object_get(f, "qualityLabel"));
					if (label) sscanf(label, "%dp", &h);
				}
				/* Vita HW decode ceiling is 1280x720. */
				if (h <= 0 || h > 720) continue;
				score = h * 10000 + br;
				if (score > best_video) {
					best_video = score;
					copy_field(out->download_video_url,
					           sizeof(out->download_video_url), url);
					out->download_height = h;
				}
				continue;
			}
			if (strncmp(mime, "audio/", 6) != 0) continue;
			prefer_mp3 = strstr(mime, "mpeg") || strstr(mime, "mp3");
			prefer_m4a = strstr(mime, "mp4") || strstr(mime, "mp4a");
			if (prefer_mp3 ||
			    (prefer_m4a && best_audio < 1000000) ||
			    br > best_audio) {
				if (prefer_mp3) best_audio = 2000000;
				else if (prefer_m4a && br < 1000000)
					best_audio = 1000000 + br;
				else
					best_audio = br;
				copy_field(out->audio_url, sizeof(out->audio_url), url);
				pick_ext_from_mime(mime, "m4a", out->audio_ext,
				                   sizeof(out->audio_ext));
			}
		}
	}

	if (out->video_url[0] && !out->audio_url[0])
		out->audio_via_progressive = 1;

	json_decref(root);
	return out->video_url[0] || out->audio_url[0] ||
	               out->download_video_url[0]
	           ? 0
	           : -1;
}

static void merge_resolved(YtResolvedMedia *dst, const YtResolvedMedia *src)
{
	if (!dst || !src) return;
	if (!dst->video_url[0] && src->video_url[0]) {
		copy_field(dst->video_url, sizeof(dst->video_url), src->video_url);
		copy_field(dst->video_ext, sizeof(dst->video_ext), src->video_ext);
		dst->progressive_height = src->progressive_height;
	} else if (dst->progressive_height <= 0 && src->progressive_height > 0) {
		dst->progressive_height = src->progressive_height;
	}
	if (!dst->download_video_url[0] && src->download_video_url[0]) {
		copy_field(dst->download_video_url, sizeof(dst->download_video_url),
		           src->download_video_url);
		dst->download_height = src->download_height;
	} else if (src->download_height > dst->download_height &&
	           src->download_video_url[0]) {
		copy_field(dst->download_video_url, sizeof(dst->download_video_url),
		           src->download_video_url);
		dst->download_height = src->download_height;
	}
	if (!dst->audio_url[0] && src->audio_url[0]) {
		copy_field(dst->audio_url, sizeof(dst->audio_url), src->audio_url);
		copy_field(dst->audio_ext, sizeof(dst->audio_ext), src->audio_ext);
		dst->audio_via_progressive = 0;
	}
	if (!dst->title[0] && src->title[0])
		copy_field(dst->title, sizeof(dst->title), src->title);
	if (!dst->author[0] && src->author[0])
		copy_field(dst->author, sizeof(dst->author), src->author);
	if (dst->length_seconds <= 0 && src->length_seconds > 0)
		dst->length_seconds = src->length_seconds;
	if (dst->video_url[0] && !dst->audio_url[0])
		dst->audio_via_progressive = 1;
}

int yt_client_search(const char *query, YtSearchResult *out, int max_out,
                     char *detail, size_t detail_size)
{
	YtBuffer buffer;
	char continuation[YT_CONTINUATION_MAX];
	int total = 0;
	int page;

	if (!query || !query[0] || !out || max_out <= 0) {
		set_detail(detail, detail_size, "Empty search");
		return -1;
	}
	if (!vita_https_is_connected()) {
		set_detail(detail, detail_size, "No Wi-Fi connection");
		return -1;
	}

	continuation[0] = '\0';
	for (page = 0; page < YT_SEARCH_PAGES && total < max_out; page++) {
		json_t *body;
		int got;
		char next_cont[YT_CONTINUATION_MAX];

		if (page == 0)
			body = json_pack("{s:s}", "query", query);
		else
			body = json_pack("{s:s}", "continuation", continuation);
		if (!body) {
			set_detail(detail, detail_size, "Out of memory");
			return total > 0 ? total : -1;
		}
		if (it_post("search", IT_LIST_FIELDS, body, IT_CLIENT_ANDROID, &buffer,
		            detail, detail_size) < 0)
			return total > 0 ? total : -1;

		next_cont[0] = '\0';
		got = parse_innertube_search((const char *)buffer.data, out + total,
		                             max_out - total, next_cont,
		                             sizeof(next_cont));
		buffer_free(&buffer);
		if (got < 0) {
			set_detail(detail, detail_size, "Could not parse search results");
			return total > 0 ? total : -1;
		}
		total += got;
		if (!next_cont[0] || got == 0) break;
		copy_field(continuation, sizeof(continuation), next_cont);
	}

	if (total == 0) {
		set_detail(detail, detail_size, "No results");
		return 0;
	}
	set_detail(detail, detail_size, "");
	return total;
}

int yt_client_resolve(const char *video_id, YtResolvedMedia *out,
                      char *detail, size_t detail_size)
{
	YtBuffer buffer;
	json_t *body;
	YtResolvedMedia partial;
	int have = 0;

	if (!looks_like_video_id(video_id) || !out) {
		set_detail(detail, detail_size, "Invalid video id");
		return -1;
	}
	if (!vita_https_is_connected()) {
		set_detail(detail, detail_size, "No Wi-Fi connection");
		return -1;
	}

	memset(out, 0, sizeof(*out));

	/* ANDROID first: progressive itag 18 + adaptive ≤720p when CDN URLs exist. */
	body = json_pack("{s:s,s:b,s:b}",
	                 "videoId", video_id,
	                 "contentCheckOk", 1,
	                 "racyCheckOk", 1);
	if (!body) {
		set_detail(detail, detail_size, "Out of memory");
		return -1;
	}
	if (it_post("player", NULL, body, IT_CLIENT_ANDROID, &buffer, detail,
	            detail_size) == 0) {
		if (parse_innertube_player((const char *)buffer.data, &partial) == 0) {
			merge_resolved(out, &partial);
			have = 1;
		}
		buffer_free(&buffer);
	}

	/* ANDROID_VR fallback when ANDROID omitted adaptive audio/video URLs. */
	if (!out->audio_url[0] || !out->download_video_url[0]) {
		body = json_pack("{s:s,s:b,s:b}",
		                 "videoId", video_id,
		                 "contentCheckOk", 1,
		                 "racyCheckOk", 1);
		if (body &&
		    it_post("player", NULL, body, IT_CLIENT_ANDROID_VR, &buffer,
		            detail, detail_size) == 0) {
			if (parse_innertube_player((const char *)buffer.data,
			                           &partial) == 0) {
				merge_resolved(out, &partial);
				have = 1;
			}
			buffer_free(&buffer);
		}
	}

	if (!have ||
	    (!out->video_url[0] && !out->audio_url[0] &&
	     !out->download_video_url[0])) {
		set_detail(detail, detail_size, "Could not resolve media streams");
		return -1;
	}
	if (out->video_url[0] && !out->audio_url[0])
		out->audio_via_progressive = 1;
	set_detail(detail, detail_size, "");
	return 0;
}

int yt_client_remux_audio_m4a(const char *src_mp4, const char *dst_m4a,
                              char *detail, size_t detail_size)
{
	AVFormatContext *in = NULL;
	AVFormatContext *out = NULL;
	AVPacket *pkt = NULL;
	int audio_index = -1;
	int out_index = -1;
	unsigned i;
	int ret;

	if (!src_mp4 || !dst_m4a) {
		set_detail(detail, detail_size, "Invalid remux paths");
		return -1;
	}

	ret = avformat_open_input(&in, src_mp4, NULL, NULL);
	if (ret < 0) {
		set_detail(detail, detail_size, "Could not open downloaded video");
		return ret;
	}
	ret = avformat_find_stream_info(in, NULL);
	if (ret < 0) {
		set_detail(detail, detail_size, "Could not read media info");
		goto fail;
	}
	for (i = 0; i < in->nb_streams; i++) {
		if (in->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_index = (int)i;
			break;
		}
	}
	if (audio_index < 0) {
		set_detail(detail, detail_size, "No audio track in stream");
		ret = -1;
		goto fail;
	}

	ret = avformat_alloc_output_context2(&out, NULL, "mp4", dst_m4a);
	if (ret < 0 || !out) {
		set_detail(detail, detail_size, "Could not create M4A output");
		ret = -1;
		goto fail;
	}
	{
		AVStream *in_st = in->streams[audio_index];
		AVStream *out_st = avformat_new_stream(out, NULL);
		if (!out_st) {
			set_detail(detail, detail_size, "Out of memory");
			ret = -1;
			goto fail;
		}
		ret = avcodec_parameters_copy(out_st->codecpar, in_st->codecpar);
		if (ret < 0) {
			set_detail(detail, detail_size, "Could not copy audio codec");
			goto fail;
		}
		out_st->codecpar->codec_tag = 0;
		out_st->time_base = in_st->time_base;
		out_index = out_st->index;
	}

	if (!(out->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&out->pb, dst_m4a, AVIO_FLAG_WRITE);
		if (ret < 0) {
			set_detail(detail, detail_size, "Could not write M4A file");
			goto fail;
		}
	}
	ret = avformat_write_header(out, NULL);
	if (ret < 0) {
		set_detail(detail, detail_size, "Could not write M4A header");
		goto fail;
	}

	pkt = av_packet_alloc();
	if (!pkt) {
		set_detail(detail, detail_size, "Out of memory");
		ret = -1;
		goto fail;
	}
	while ((ret = av_read_frame(in, pkt)) >= 0) {
		if (pkt->stream_index != audio_index) {
			av_packet_unref(pkt);
			continue;
		}
		pkt->stream_index = out_index;
		av_packet_rescale_ts(pkt, in->streams[audio_index]->time_base,
		                     out->streams[out_index]->time_base);
		ret = av_interleaved_write_frame(out, pkt);
		av_packet_unref(pkt);
		if (ret < 0) {
			set_detail(detail, detail_size, "Audio remux failed");
			goto fail;
		}
	}
	if (ret == AVERROR_EOF) ret = 0;
	av_write_trailer(out);
	set_detail(detail, detail_size, "");

fail:
	if (pkt) av_packet_free(&pkt);
	if (out) {
		if (out->pb && !(out->oformat->flags & AVFMT_NOFILE))
			avio_closep(&out->pb);
		avformat_free_context(out);
	}
	if (in) avformat_close_input(&in);
	return ret;
}

int yt_client_remux_av_mp4(const char *video_path, const char *audio_path,
                           const char *dst_mp4, char *detail, size_t detail_size)
{
	AVFormatContext *vin = NULL;
	AVFormatContext *ain = NULL;
	AVFormatContext *out = NULL;
	AVPacket *pkt = NULL;
	int v_in = -1, a_in = -1;
	int v_out = -1, a_out = -1;
	unsigned i;
	int ret;

	if (!video_path || !audio_path || !dst_mp4) {
		set_detail(detail, detail_size, "Invalid remux paths");
		return -1;
	}
	ret = avformat_open_input(&vin, video_path, NULL, NULL);
	if (ret < 0) {
		set_detail(detail, detail_size, "Could not open video track");
		return ret;
	}
	ret = avformat_find_stream_info(vin, NULL);
	if (ret < 0) {
		set_detail(detail, detail_size, "Could not read video info");
		goto fail;
	}
	ret = avformat_open_input(&ain, audio_path, NULL, NULL);
	if (ret < 0) {
		set_detail(detail, detail_size, "Could not open audio track");
		goto fail;
	}
	ret = avformat_find_stream_info(ain, NULL);
	if (ret < 0) {
		set_detail(detail, detail_size, "Could not read audio info");
		goto fail;
	}
	for (i = 0; i < vin->nb_streams; i++) {
		if (vin->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
		    vin->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264) {
			v_in = (int)i;
			break;
		}
	}
	for (i = 0; i < ain->nb_streams; i++) {
		if (ain->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			a_in = (int)i;
			break;
		}
	}
	if (v_in < 0 || a_in < 0) {
		set_detail(detail, detail_size, "Missing H.264 or audio track");
		ret = -1;
		goto fail;
	}

	ret = avformat_alloc_output_context2(&out, NULL, "mp4", dst_mp4);
	if (ret < 0 || !out) {
		set_detail(detail, detail_size, "Could not create MP4 output");
		ret = -1;
		goto fail;
	}
	{
		AVStream *in_st = vin->streams[v_in];
		AVStream *out_st = avformat_new_stream(out, NULL);
		if (!out_st) {
			ret = -1;
			goto fail;
		}
		ret = avcodec_parameters_copy(out_st->codecpar, in_st->codecpar);
		if (ret < 0) goto fail;
		out_st->codecpar->codec_tag = 0;
		out_st->time_base = in_st->time_base;
		v_out = out_st->index;
	}
	{
		AVStream *in_st = ain->streams[a_in];
		AVStream *out_st = avformat_new_stream(out, NULL);
		if (!out_st) {
			ret = -1;
			goto fail;
		}
		ret = avcodec_parameters_copy(out_st->codecpar, in_st->codecpar);
		if (ret < 0) goto fail;
		out_st->codecpar->codec_tag = 0;
		out_st->time_base = in_st->time_base;
		a_out = out_st->index;
	}
	if (!(out->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&out->pb, dst_mp4, AVIO_FLAG_WRITE);
		if (ret < 0) {
			set_detail(detail, detail_size, "Could not write MP4 file");
			goto fail;
		}
	}
	ret = avformat_write_header(out, NULL);
	if (ret < 0) {
		set_detail(detail, detail_size, "Could not write MP4 header");
		goto fail;
	}
	pkt = av_packet_alloc();
	if (!pkt) {
		ret = -1;
		goto fail;
	}
	while ((ret = av_read_frame(vin, pkt)) >= 0) {
		if (pkt->stream_index != v_in) {
			av_packet_unref(pkt);
			continue;
		}
		pkt->stream_index = v_out;
		av_packet_rescale_ts(pkt, vin->streams[v_in]->time_base,
		                     out->streams[v_out]->time_base);
		ret = av_interleaved_write_frame(out, pkt);
		av_packet_unref(pkt);
		if (ret < 0) {
			set_detail(detail, detail_size, "Video remux failed");
			goto fail;
		}
	}
	if (ret != AVERROR_EOF && ret < 0) {
		set_detail(detail, detail_size, "Video remux read failed");
		goto fail;
	}
	while ((ret = av_read_frame(ain, pkt)) >= 0) {
		if (pkt->stream_index != a_in) {
			av_packet_unref(pkt);
			continue;
		}
		pkt->stream_index = a_out;
		av_packet_rescale_ts(pkt, ain->streams[a_in]->time_base,
		                     out->streams[a_out]->time_base);
		ret = av_interleaved_write_frame(out, pkt);
		av_packet_unref(pkt);
		if (ret < 0) {
			set_detail(detail, detail_size, "Audio remux failed");
			goto fail;
		}
	}
	if (ret != AVERROR_EOF && ret < 0) {
		set_detail(detail, detail_size, "Audio remux read failed");
		goto fail;
	}
	av_write_trailer(out);
	ret = 0;
	set_detail(detail, detail_size, "");

fail:
	if (pkt) av_packet_free(&pkt);
	if (out) {
		if (out->pb && !(out->oformat->flags & AVFMT_NOFILE))
			avio_closep(&out->pb);
		avformat_free_context(out);
	}
	if (ain) avformat_close_input(&ain);
	if (vin) avformat_close_input(&vin);
	return ret;
}

int yt_client_file_has_h264(const char *path)
{
	AVFormatContext *fmt = NULL;
	unsigned i;
	int found = 0;

	if (!path || !path[0]) return 0;
	if (avformat_open_input(&fmt, path, NULL, NULL) < 0) return 0;
	if (avformat_find_stream_info(fmt, NULL) < 0) {
		avformat_close_input(&fmt);
		return 0;
	}
	for (i = 0; i < fmt->nb_streams; i++) {
		AVCodecParameters *par = fmt->streams[i]->codecpar;
		if (par->codec_type == AVMEDIA_TYPE_VIDEO &&
		    par->codec_id == AV_CODEC_ID_H264) {
			if (par->height <= 0 || par->height <= 720)
				found = 1;
			break;
		}
	}
	avformat_close_input(&fmt);
	return found;
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
