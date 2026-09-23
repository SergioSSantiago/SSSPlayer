#include "network/download_manager.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include <vita_https.h>

#define DOWNLOAD_ROOT "ux0:download"
#define DOWNLOAD_BUFFER_SIZE (64 * 1024)

static void secure_zero(void *memory, size_t size) {
	volatile unsigned char *bytes = memory;
	while (size--) *bytes++ = 0;
}

static void update_progress(VtDownloadJob *job, int64_t value) {
	if (!job) return;
	job->progress_current = value > LONG_MAX ? LONG_MAX : (long)value;
}

/* A pause is cooperative: never begin another transport read while paused,
 * but allow Abort to wake the worker even when the user pauses mid-transfer. */
static int wait_if_paused(VtDownloadJob *job) {
	while (job && job->paused && !job->cancel)
		sceKernelDelayThread(20 * 1000);
	return job && job->cancel ? -1 : 0;
}

static void filename_from_value(const char *value, char *out, size_t out_size) {
	const char *name = value ? strrchr(value, '/') : NULL;
	name = name ? name + 1 : value;
	if (!name || !name[0]) name = "download";
	char plain[128];
	size_t n = 0;
	while (*name && *name != '?' && *name != '#' && n + 1 < sizeof(plain)) {
		unsigned char c = (unsigned char)*name++;
		plain[n++] = (isalnum(c) || c == '.' || c == '-' || c == '_') ? c : '_';
	}
	plain[n] = '\0';
	if (!plain[0] || !strcmp(plain, ".") || !strcmp(plain, ".."))
		snprintf(plain, sizeof(plain), "download");
	snprintf(out, out_size, "%s", plain);
}

static int make_destination(VtDownloadJob *job, const char *value,
	                        char *part, size_t part_size) {
	char filename[128];
	if (job->preferred_name[0])
		snprintf(filename, sizeof(filename), "%s", job->preferred_name);
	else
		filename_from_value(value, filename, sizeof(filename));
	const char *directory = job->destination_directory[0]
	                      ? job->destination_directory : DOWNLOAD_ROOT;
	sceIoMkdir("ux0:download", 0777);
	for (int suffix = 0; suffix < 1000; suffix++) {
		char candidate[VT_NETWORK_PATH_MAX];
		if (suffix == 0) snprintf(candidate, sizeof(candidate), "%s/%s", directory, filename);
		else snprintf(candidate, sizeof(candidate), "%s/%d_%s", directory, suffix, filename);
		SceIoStat stat;
		memset(&stat, 0, sizeof(stat));
		if (sceIoGetstat(candidate, &stat) >= 0) continue;
		snprintf(job->destination, sizeof(job->destination), "%s", candidate);
		int written = snprintf(part, part_size, "%s.part", candidate);
		return written > 0 && written < (int)part_size ? 0 : -1;
	}
	snprintf(job->detail, sizeof(job->detail), "No free filename in %s", directory);
	return -1;
}

static int write_chunk(SceUID fd, const void *data, size_t size) {
	const unsigned char *cursor = data;
	while (size) {
		int wrote = sceIoWrite(fd, cursor, size);
		if (wrote <= 0) return -1;
		cursor += wrote;
		size -= (size_t)wrote;
	}
	return 0;
}

static int finish_file(VtDownloadJob *job, SceUID fd, const char *part, int result) {
	if (result == 0 && sceIoSyncByFd(fd, 0) < 0) result = -1;
	if (sceIoClose(fd) < 0 && result == 0) result = -1;
	if (result == 0 && sceIoRename(part, job->destination) < 0) result = -1;
	if (result != 0) sceIoRemove(part);
	if (result < 0 && !job->detail[0])
		snprintf(job->detail, sizeof(job->detail), job->cancel ? "Download aborted" : "Could not save the download");
	return result;
}

static int download_network(VtDownloadJob *job, const char *part) {
	VtNetworkStreamFactory factory;
	VtDecoderStreamHandle stream;
	if (vt_network_stream_factory_init(&factory, &job->source, &job->credential,
	                                  job->remote_path) < 0) return -1;
	memset(&stream, 0, sizeof(stream));
	int result = factory.factory.open_cancelable(factory.factory.opaque, &stream, &job->cancel);
	if (result < 0) {
		snprintf(job->detail, sizeof(job->detail), "Could not open the remote file");
		return result;
	}
	job->progress_total = stream.size > LONG_MAX ? LONG_MAX : (long)stream.size;
	SceUID fd = sceIoOpen(part, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) { stream.close(stream.opaque); return -1; }
	unsigned char buffer[DOWNLOAD_BUFFER_SIZE];
	int64_t transferred = 0;
	while (!job->cancel) {
		if (wait_if_paused(job) < 0) { result = -1; break; }
		int read = stream.read(stream.opaque, buffer, sizeof(buffer));
		if (read < 0) { result = -1; break; }
		if (read == 0) { result = 0; break; }
		if (write_chunk(fd, buffer, (size_t)read) < 0) { result = -1; break; }
		transferred += read;
		update_progress(job, transferred);
	}
	if (job->cancel) {
		if (stream.abort) stream.abort(stream.opaque);
		result = -1;
	}
	stream.close(stream.opaque);
	return finish_file(job, fd, part, result);
}

typedef struct { VtDownloadJob *job; SceUID fd; int64_t transferred; } UrlWrite;

static size_t url_write(const void *data, size_t size, void *opaque) {
	UrlWrite *writer = opaque;
	if (!writer || !writer->job) return 0;
	if (writer->job->cancel) return 0;
	if (wait_if_paused(writer->job) < 0) return 0;
	if (write_chunk(writer->fd, data, size) < 0) return 0;
	writer->transferred += (int64_t)size;
	update_progress(writer->job, writer->transferred);
	return size;
}

/* YouTube adaptive googlevideo URLs reject a plain GET (HTTP 403) but accept
 * Range windows. Progressive muxed URLs (itag 18) still allow a full GET. */
static int youtube_url_needs_range(const char *url) {
	if (!url || !url[0]) return 0;
	if (!strstr(url, "googlevideo.com")) return 0;
	return strstr(url, "gir=yes") != NULL || strstr(url, "gir%3Dyes") != NULL;
}

static int64_t youtube_url_clen(const char *url) {
	const char *p;
	char *end = NULL;
	long long value;
	if (!url) return 0;
	p = strstr(url, "clen=");
	if (!p) p = strstr(url, "clen%3D");
	if (!p) return 0;
	p = strchr(p, '=');
	if (!p) return 0;
	p++;
	if (p[0] == '%' && p[1] == '3' && (p[2] == 'D' || p[2] == 'd')) p += 3;
	value = strtoll(p, &end, 10);
	if (value <= 0 || (end && end == p)) return 0;
	return (int64_t)value;
}

#define YT_RANGE_CHUNK (256 * 1024)

static int download_url_ranged(VtDownloadJob *job, const char *part,
                               const VitaHttpsClientConfig *config,
                               const char *const *base_headers) {
	int64_t total = youtube_url_clen(job->url);
	int64_t offset = 0;
	SceUID fd;
	VitaHttpsClient *client;
	char range_header[64];
	const char *headers[6];
	int header_n = 0;
	int i;

	if (total <= 0) {
		snprintf(job->detail, sizeof(job->detail),
		         "Adaptive stream missing size (clen)");
		return -1;
	}

	job->progress_total = total > LONG_MAX ? LONG_MAX : (long)total;
	update_progress(job, 0);

	fd = sceIoOpen(part, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) {
		snprintf(job->detail, sizeof(job->detail), "Could not create file");
		return -1;
	}

	client = vita_https_client_create(config);
	if (!client) {
		sceIoClose(fd);
		sceIoRemove(part);
		snprintf(job->detail, sizeof(job->detail), "HTTPS unavailable");
		return -1;
	}

	for (i = 0; base_headers && base_headers[i]; i++)
		headers[header_n++] = base_headers[i];

	while (offset < total) {
		int64_t end = offset + YT_RANGE_CHUNK - 1;
		UrlWrite writer;
		VitaHttpsRequest request;
		VitaHttpsResponse response;
		int result;
		int attempt;
		int chunk_ok = 0;

		if (end >= total) end = total - 1;
		if (job->cancel) {
			vita_https_client_destroy(client);
			sceIoClose(fd);
			sceIoRemove(part);
			snprintf(job->detail, sizeof(job->detail), "Download cancelled");
			return -1;
		}
		if (wait_if_paused(job) < 0) {
			vita_https_client_destroy(client);
			sceIoClose(fd);
			sceIoRemove(part);
			snprintf(job->detail, sizeof(job->detail), "Download cancelled");
			return -1;
		}

		snprintf(range_header, sizeof(range_header),
		         "Range: bytes=%lld-%lld", (long long)offset, (long long)end);
		headers[header_n] = range_header;
		headers[header_n + 1] = NULL;

		for (attempt = 0; attempt < 4 && !chunk_ok; attempt++) {
			if (attempt > 0) {
				snprintf(job->detail, sizeof(job->detail),
				         "Retrying range (%d/4)…", attempt + 1);
				sceKernelDelayThread((500 << (attempt - 1)) * 1000);
			}
			writer.job = job;
			writer.fd = fd;
			writer.transferred = offset;
			memset(&request, 0, sizeof(request));
			request.method = "GET";
			request.url = job->url;
			request.headers = headers;
			request.write = url_write;
			request.write_opaque = &writer;
			request.cancel_flag = &job->cancel;
			memset(&response, 0, sizeof(response));
			result = vita_https_perform(client, &request, &response);
			if (job->cancel) {
				vita_https_client_destroy(client);
				sceIoClose(fd);
				sceIoRemove(part);
				snprintf(job->detail, sizeof(job->detail), "Download cancelled");
				return -1;
			}
			if (result == 0 &&
			    (response.status_code == 206 || response.status_code == 200) &&
			    writer.transferred > offset) {
				offset = writer.transferred;
				update_progress(job, offset);
				chunk_ok = 1;
				job->detail[0] = '\0';
				break;
			}
			if (!job->detail[0])
				snprintf(job->detail, sizeof(job->detail),
				         result < 0 ? vita_https_error_string(result)
				                    : "HTTP %ld on ranged download",
				         response.status_code);
			/* Rewind file to last good offset for the next attempt. */
			if (sceIoLseek(fd, offset, SCE_SEEK_SET) < 0) {
				vita_https_client_destroy(client);
				sceIoClose(fd);
				sceIoRemove(part);
				snprintf(job->detail, sizeof(job->detail),
				         "Could not resume ranged download");
				return -1;
			}
			update_progress(job, offset);
		}
		if (!chunk_ok) {
			vita_https_client_destroy(client);
			sceIoClose(fd);
			sceIoRemove(part);
			if (!job->detail[0])
				snprintf(job->detail, sizeof(job->detail),
				         "Ranged download failed");
			return -1;
		}
	}

	vita_https_client_destroy(client);
	if (offset < total) {
		sceIoClose(fd);
		sceIoRemove(part);
		snprintf(job->detail, sizeof(job->detail),
		         "Incomplete download (%lld / %lld bytes)",
		         (long long)offset, (long long)total);
		return -1;
	}
	return finish_file(job, fd, part, 0);
}

static int download_url(VtDownloadJob *job, const char *part) {
	VitaHttpsClientConfig config;
	const char *headers[4];
	int header_n = 0;
	int attempt;
	int result = -1;
	const int max_attempts = 3;

	const char *yt_android_ua =
	    "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip";
	const char *yt_vr_ua =
	    "com.google.android.apps.youtube.vr.oculus/1.60.19 "
	    "(Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip";
	int is_youtube = strstr(job->url, "googlevideo.com") ||
	                 strstr(job->url, "youtube.com");

	memset(&config, 0, sizeof(config));
	/* Plain HTTP is an explicit direct-download choice. vita-https still keeps
	 * HTTPS verification enabled and forbids HTTPS-to-HTTP redirect downgrades. */
	config.allow_http = 1;
	if (job->user_agent[0])
		config.user_agent = job->user_agent;
	else if (is_youtube)
		/* Default ANDROID UA; adaptive VR URLs should set user_agent explicitly. */
		config.user_agent = yt_android_ua;
	else
		config.user_agent =
		    "Mozilla/5.0 (PlayStation Vita) SSSPlayer/1.0 AppleWebKit/531.22.8";
	(void)yt_vr_ua;
	/* YouTube progressive files are large; do not apply a short overall
	 * CURLOPT_TIMEOUT. Detect dead links with a generous low-speed window. */
	config.connect_timeout_ms = 30000;
	config.request_timeout_ms = 0;
	config.low_speed_bytes_per_second = 256;
	config.low_speed_seconds = 120;

	headers[header_n++] = "Accept: */*";
	if (is_youtube)
		headers[header_n++] = "Referer: https://www.youtube.com/";
	headers[header_n] = NULL;

	if (youtube_url_needs_range(job->url))
		return download_url_ranged(job, part, &config, headers);

	for (attempt = 0; attempt < max_attempts; attempt++) {
		VitaHttpsClient *client;
		SceUID fd;
		UrlWrite writer;
		VitaHttpsRequest request;
		VitaHttpsResponse response;

		if (job->cancel) return -1;
		if (attempt > 0) {
			snprintf(job->detail, sizeof(job->detail),
			         "Retrying download (%d/%d)…", attempt + 1, max_attempts);
			sceKernelDelayThread(1500 * 1000);
			sceIoRemove(part);
		}

		client = vita_https_client_create(&config);
		if (!client) {
			snprintf(job->detail, sizeof(job->detail), "HTTPS unavailable");
			return -1;
		}

		/* Skip HEAD: googlevideo often rejects it and it burns connect time. */
		fd = sceIoOpen(part, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
		if (fd < 0) {
			vita_https_client_destroy(client);
			snprintf(job->detail, sizeof(job->detail), "Could not create file");
			return -1;
		}
		writer.job = job;
		writer.fd = fd;
		writer.transferred = 0;
		memset(&request, 0, sizeof(request));
		request.method = "GET";
		request.url = job->url;
		request.headers = headers;
		request.write = url_write;
		request.write_opaque = &writer;
		request.cancel_flag = &job->cancel;
		request.progress_total = &job->progress_total;
		memset(&response, 0, sizeof(response));
		result = vita_https_perform(client, &request, &response);
		if (response.content_length > 0)
			job->progress_total = response.content_length > LONG_MAX
			                          ? LONG_MAX
			                          : (long)response.content_length;
		if (result < 0 || response.status_code < 200 ||
		    response.status_code >= 300) {
			if (!job->detail[0])
				snprintf(job->detail, sizeof(job->detail), "%s",
				         job->cancel ? "Download cancelled"
				                     : vita_https_error_string(result));
			result = -1;
		} else if (response.content_length > 0 &&
		           writer.transferred + 8192 <
		               (int64_t)response.content_length) {
			snprintf(job->detail, sizeof(job->detail),
			         "Incomplete download (%lld / %lld bytes)",
			         (long long)writer.transferred,
			         (long long)response.content_length);
			result = -1;
		} else {
			result = 0;
		}
		vita_https_client_destroy(client);
		result = finish_file(job, fd, part, result);
		if (result == 0 || job->cancel) return result;
		/* Retry timeouts / stalls; other errors fail immediately. */
		if (!job->detail[0] ||
		    (!strstr(job->detail, "Timeout") &&
		     !strstr(job->detail, "timeout") &&
		     !strstr(job->detail, "timed out") &&
		     !strstr(job->detail, "too slow") &&
		     !strstr(job->detail, "low speed")))
			return result;
		job->detail[0] = '\0';
		job->progress_current = 0;
	}
	if (!job->detail[0])
		snprintf(job->detail, sizeof(job->detail), "Timeout was reached");
	return result;
}

void vt_download_job_init_network(VtDownloadJob *job, const VtNetworkSource *source,
	                              const VtNetworkCredential *credential, const char *path) {
	if (!job) return;
	memset(job, 0, sizeof(*job));
	if (source) job->source = *source;
	if (credential) job->credential = *credential;
	if (path) snprintf(job->remote_path, sizeof(job->remote_path), "%s", path);
}

void vt_download_job_init_url(VtDownloadJob *job, const char *url) {
	if (!job) return;
	memset(job, 0, sizeof(*job));
	job->direct_url = 1;
	if (url) snprintf(job->url, sizeof(job->url), "%s", url);
}

void vt_download_job_set_destination(VtDownloadJob *job, const char *directory) {
	if (!job) return;
	job->destination_directory[0] = '\0';
	if (directory && directory[0])
		snprintf(job->destination_directory, sizeof(job->destination_directory), "%s",
		         directory);
}

void vt_download_job_set_filename(VtDownloadJob *job, const char *filename) {
	size_t n = 0;
	if (!job) return;
	job->preferred_name[0] = '\0';
	if (!filename || !filename[0]) return;
	while (filename[n] && n + 1 < sizeof(job->preferred_name)) {
		unsigned char c = (unsigned char)filename[n];
		job->preferred_name[n] =
		    (isalnum(c) || c == '.' || c == '-' || c == '_') ? (char)c : '_';
		n++;
	}
	job->preferred_name[n] = '\0';
	if (!job->preferred_name[0] || !strcmp(job->preferred_name, ".") ||
	    !strcmp(job->preferred_name, ".."))
		snprintf(job->preferred_name, sizeof(job->preferred_name), "download");
}

void vt_download_job_set_user_agent(VtDownloadJob *job, const char *user_agent) {
	if (!job) return;
	job->user_agent[0] = '\0';
	if (user_agent && user_agent[0])
		snprintf(job->user_agent, sizeof(job->user_agent), "%s", user_agent);
}

int vt_download_run(void *opaque) {
	VtDownloadJob *job = opaque;
	if (!job) return -1;
	const char *value = job->direct_url ? job->url : job->remote_path;
	if (!value || !value[0]) return -1;
	if (job->direct_url && strncmp(value, "https://", 8) != 0 &&
	    strncmp(value, "http://", 7) != 0) {
		snprintf(job->detail, sizeof(job->detail), "Only HTTP and HTTPS URLs are accepted");
		return -1;
	}
	char part[VT_NETWORK_PATH_MAX + 8];
	if (make_destination(job, value, part, sizeof(part)) < 0) return -1;
	int result = job->direct_url ? download_url(job, part) : download_network(job, part);
	secure_zero(&job->credential, sizeof(job->credential));
	return result;
}
