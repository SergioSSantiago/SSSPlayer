#include "vita_https.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include "ca_bundle.h"
#include "net_runtime.h"

#define RANGE_CACHE_SIZE (512 * 1024)

struct VitaHttpsClient {
	char user_agent[128];
	char username[256];
	char password[256];
	long connect_timeout_ms;
	long request_timeout_ms;
	long low_speed_limit;
	long low_speed_time;
	char pinned_public_key[128];
	int allow_untrusted_ca_with_pin;
	int allow_http;
};

typedef struct RangeStream {
	VitaHttpsClient *client;
	char *url;
	volatile int *cancel;
	uint64_t size;
	uint64_t position;
	uint64_t cache_start;
	size_t cache_size;
	unsigned char *cache;
} RangeStream;

typedef struct WriteBridge {
	VitaHttpsWriteCallback callback;
	void *opaque;
} WriteBridge;

typedef struct FixedBuffer {
	unsigned char *data;
	size_t size;
	size_t capacity;
} FixedBuffer;

static int s_init_count;

static int curl_error(CURLcode code) {
	if (code == CURLE_PEER_FAILED_VERIFICATION ||
	    code == CURLE_SSL_CACERT_BADFILE)
		return VITA_HTTPS_ERROR_UNTRUSTED_CERTIFICATE;
	if (code == CURLE_SSL_PINNEDPUBKEYNOTMATCH)
		return VITA_HTTPS_ERROR_PIN_MISMATCH;
	return VITA_HTTPS_ERROR_CURL_BASE - (int)code;
}

static int valid_request_url(const char *url, int allow_http) {
	if (!url || url[0] == '\0') return 0;
	if (strncmp(url, "https://", 8) == 0 && url[8] != '\0') return 1;
	if (allow_http && strncmp(url, "http://", 7) == 0 && url[7] != '\0')
		return 1;
	return 0;
}

static size_t discard_write(char *data, size_t size, size_t count, void *opaque) {
	(void)data;
	(void)opaque;
	return size * count;
}

static size_t bridge_write(char *data, size_t size, size_t count, void *opaque) {
	WriteBridge *bridge = (WriteBridge *)opaque;
	size_t bytes = size * count;
	if (!bridge || !bridge->callback) return bytes;
	return bridge->callback(data, bytes, bridge->opaque);
}

static size_t fixed_write(char *data, size_t size, size_t count, void *opaque) {
	FixedBuffer *buffer = (FixedBuffer *)opaque;
	size_t bytes = size * count;
	if (!buffer || bytes > buffer->capacity - buffer->size) return 0;
	memcpy(buffer->data + buffer->size, data, bytes);
	buffer->size += bytes;
	return bytes;
}

static int progress_cancel(void *opaque, curl_off_t down_total,
	                       curl_off_t down_now, curl_off_t up_total,
	                       curl_off_t up_now) {
	(void)down_total;
	(void)down_now;
	(void)up_total;
	(void)up_now;
	volatile int *cancel = (volatile int *)opaque;
	return cancel && *cancel;
}

static void apply_common(CURL *curl, const VitaHttpsClient *client,
	                     volatile int *cancel) {
	struct curl_blob ca = {
		.data = (void *)ca_bundle_pem,
		.len = ca_bundle_pem_len,
		.flags = CURL_BLOB_NOCOPY
	};
	curl_easy_setopt(curl, CURLOPT_USERAGENT, client->user_agent);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR,
	                 client->allow_http ? "https,http" : "https");
	/* Never allow an HTTPS response to redirect into clear-text HTTP. */
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, client->connect_timeout_ms);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, client->request_timeout_ms);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, client->low_speed_limit);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, client->low_speed_time);
	int pinned_self_signed = client->allow_untrusted_ca_with_pin &&
	                         client->pinned_public_key[0];
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,
	                 pinned_self_signed ? 0L : 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
	if (client->pinned_public_key[0])
		curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY,
		                 client->pinned_public_key);
	if (client->username[0]) {
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
		curl_easy_setopt(curl, CURLOPT_USERNAME, client->username);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, client->password);
	}
	if (cancel) {
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cancel);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)cancel);
	}
}

int vita_https_init(void) {
	if (s_init_count > 0) {
		s_init_count++;
		return 0;
	}
	int result = vita_https_net_init();
	if (result < 0) return result;
	CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (code != CURLE_OK) {
		vita_https_net_term();
		return curl_error(code);
	}
	s_init_count = 1;
	return 0;
}

void vita_https_shutdown(void) {
	if (s_init_count <= 0 || --s_init_count > 0) return;
	curl_global_cleanup();
	vita_https_net_term();
}

int vita_https_is_connected(void) {
	return s_init_count > 0 && vita_https_net_is_connected();
}

int vita_https_wifi_signal_percent(int *percent) {
	return s_init_count > 0
	     ? vita_https_net_wifi_signal_percent(percent)
	     : VITA_HTTPS_ERROR_NOT_INITIALIZED;
}

VitaHttpsClient *vita_https_client_create(const VitaHttpsClientConfig *config) {
	if (s_init_count <= 0) return NULL;
	VitaHttpsClient *client = calloc(1, sizeof(*client));
	if (!client) return NULL;
	snprintf(client->user_agent, sizeof(client->user_agent), "%s",
	         config && config->user_agent ? config->user_agent : "vita-https/1.0");
	if (config && config->username)
		snprintf(client->username, sizeof(client->username), "%s", config->username);
	if (config && config->password)
		snprintf(client->password, sizeof(client->password), "%s", config->password);
	if (config && config->pinned_public_key)
		snprintf(client->pinned_public_key,
		         sizeof(client->pinned_public_key), "%s",
		         config->pinned_public_key);
	client->allow_untrusted_ca_with_pin =
		config && config->allow_untrusted_ca_with_pin &&
		client->pinned_public_key[0];
	client->allow_http = config && config->allow_http;
	client->connect_timeout_ms = config && config->connect_timeout_ms > 0
	                           ? config->connect_timeout_ms : 10000;
	client->request_timeout_ms = config && config->request_timeout_ms > 0
	                           ? config->request_timeout_ms : 20000;
	client->low_speed_limit = config && config->low_speed_bytes_per_second > 0
	                         ? config->low_speed_bytes_per_second : 1024;
	client->low_speed_time = config && config->low_speed_seconds > 0
	                        ? config->low_speed_seconds : 15;
	return client;
}

void vita_https_client_destroy(VitaHttpsClient *client) {
	if (!client) return;
	memset(client->password, 0, sizeof(client->password));
	free(client);
}

static int public_key_pin_from_pem(const char *pem, char *pin,
	                               size_t pin_size) {
	if (!pem || !pin || pin_size < 53) return VITA_HTTPS_ERROR_INVALID_ARGUMENT;
	mbedtls_x509_crt certificate;
	mbedtls_x509_crt_init(&certificate);
	int result = mbedtls_x509_crt_parse(&certificate,
		                              (const unsigned char *)pem,
		                              strlen(pem) + 1);
	if (result < 0) {
		mbedtls_x509_crt_free(&certificate);
		return VITA_HTTPS_ERROR_CERTIFICATE_INFO;
	}
	unsigned char der[2048];
	int der_size = mbedtls_pk_write_pubkey_der(&certificate.pk, der, sizeof(der));
	if (der_size <= 0 || (size_t)der_size > sizeof(der)) {
		mbedtls_x509_crt_free(&certificate);
		return VITA_HTTPS_ERROR_CERTIFICATE_INFO;
	}
	unsigned char digest[32];
	result = mbedtls_sha256(der + sizeof(der) - (size_t)der_size,
	                        (size_t)der_size, digest, 0);
	mbedtls_x509_crt_free(&certificate);
	if (result < 0) return VITA_HTTPS_ERROR_CERTIFICATE_INFO;
	unsigned char encoded[48];
	size_t encoded_size = 0;
	result = mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_size,
	                               digest, sizeof(digest));
	if (result < 0 || encoded_size != 44 || pin_size < 9 + encoded_size)
		return VITA_HTTPS_ERROR_CERTIFICATE_INFO;
	memcpy(pin, "sha256//", 8);
	memcpy(pin + 8, encoded, encoded_size);
	pin[8 + encoded_size] = '\0';
	return 0;
}

int vita_https_probe_public_key(VitaHttpsClient *client, const char *url,
	                            char *pin, size_t pin_size) {
	if (s_init_count <= 0) return VITA_HTTPS_ERROR_NOT_INITIALIZED;
	if (!client || !valid_request_url(url, client->allow_http) || !pin || pin_size < 53)
		return VITA_HTTPS_ERROR_INVALID_ARGUMENT;
	pin[0] = '\0';
	CURL *curl = curl_easy_init();
	if (!curl) return VITA_HTTPS_ERROR_OUT_OF_MEMORY;
	apply_common(curl, client, NULL);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, NULL);
	curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_write);
	CURLcode code = curl_easy_perform(curl);
	int result = code == CURLE_OK ? VITA_HTTPS_ERROR_CERTIFICATE_INFO
	                             : curl_error(code);
	if (code == CURLE_OK) {
		struct curl_certinfo *info = NULL;
		if (curl_easy_getinfo(curl, CURLINFO_CERTINFO, &info) == CURLE_OK &&
		    info && info->num_of_certs > 0) {
			for (struct curl_slist *item = info->certinfo[0]; item;
			     item = item->next) {
				if (item->data && strncmp(item->data, "Cert:", 5) == 0) {
					result = public_key_pin_from_pem(item->data + 5,
					                                 pin, pin_size);
					break;
				}
			}
		}
	}
	curl_easy_cleanup(curl);
	return result;
}

int vita_https_perform(VitaHttpsClient *client,
	                   const VitaHttpsRequest *request,
	                   VitaHttpsResponse *response) {
	if (s_init_count <= 0) return VITA_HTTPS_ERROR_NOT_INITIALIZED;
	if (!client || !request || !valid_request_url(request->url, client->allow_http))
		return VITA_HTTPS_ERROR_INVALID_ARGUMENT;
	CURL *curl = curl_easy_init();
	if (!curl) return VITA_HTTPS_ERROR_OUT_OF_MEMORY;
	apply_common(curl, client, request->cancel_flag);
	curl_easy_setopt(curl, CURLOPT_URL, request->url);
	const char *method = request->method ? request->method : "GET";
	if (strcmp(method, "HEAD") == 0) {
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	} else if (strcmp(method, "GET") != 0) {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
	}
	if (request->body) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
		                 (curl_off_t)request->body_size);
	}
	struct curl_slist *headers = NULL;
	for (const char *const *header = request->headers; header && *header; header++)
		headers = curl_slist_append(headers, *header);
	if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	WriteBridge bridge = { request->write, request->write_opaque };
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
	                 request->write ? bridge_write : discard_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA,
	                 request->write ? (void *)&bridge : NULL);
	CURLcode code = curl_easy_perform(curl);
	long status = 0;
	curl_off_t length = -1;
	char *effective = NULL;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
	curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective);
	if (response) {
		memset(response, 0, sizeof(*response));
		response->status_code = status;
		response->content_length = (int64_t)length;
		if (effective)
			snprintf(response->effective_url, sizeof(response->effective_url),
			         "%s", effective);
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (code != CURLE_OK) return curl_error(code);
	return status >= 200 && status < 400 ? 0 : VITA_HTTPS_ERROR_HTTP_STATUS;
}

char *vita_https_escape(const char *text, size_t length) {
	if (s_init_count <= 0 || !text || length > INT_MAX) return NULL;
	CURL *curl = curl_easy_init();
	if (!curl) return NULL;
	char *escaped = curl_easy_escape(curl, text, (int)length);
	curl_easy_cleanup(curl);
	return escaped;
}

char *vita_https_unescape(const char *text, size_t *length) {
	if (s_init_count <= 0 || !text) return NULL;
	CURL *curl = curl_easy_init();
	if (!curl) return NULL;
	int decoded_length = 0;
	char *decoded = curl_easy_unescape(curl, text, 0, &decoded_length);
	curl_easy_cleanup(curl);
	if (length) *length = decoded_length > 0 ? (size_t)decoded_length : 0;
	return decoded;
}

void vita_https_free(void *pointer) {
	curl_free(pointer);
}

static int range_fetch(RangeStream *stream, uint64_t start) {
	if (!stream || start >= stream->size) return 0;
	uint64_t end = start + RANGE_CACHE_SIZE - 1;
	if (end >= stream->size) end = stream->size - 1;
	char range[64];
	snprintf(range, sizeof(range), "%llu-%llu",
	         (unsigned long long)start, (unsigned long long)end);
	CURL *curl = curl_easy_init();
	if (!curl) return VITA_HTTPS_ERROR_OUT_OF_MEMORY;
	apply_common(curl, stream->client, stream->cancel);
	curl_easy_setopt(curl, CURLOPT_URL, stream->url);
	curl_easy_setopt(curl, CURLOPT_RANGE, range);
	FixedBuffer buffer = { stream->cache, 0, RANGE_CACHE_SIZE };
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fixed_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
	CURLcode code = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);
	if (code != CURLE_OK) return curl_error(code);
	if (status != 206) return VITA_HTTPS_ERROR_RANGE_UNSUPPORTED;
	stream->cache_start = start;
	stream->cache_size = buffer.size;
	return buffer.size > 0 ? 0 : VITA_HTTPS_ERROR_RANGE_UNSUPPORTED;
}

static int range_read(void *opaque, void *data, size_t size) {
	RangeStream *stream = (RangeStream *)opaque;
	if (!stream || !data) return VITA_HTTPS_ERROR_INVALID_ARGUMENT;
	if (stream->position >= stream->size) return 0;
	size_t total = 0;
	while (total < size && stream->position < stream->size) {
		if (stream->position < stream->cache_start ||
		    stream->position >= stream->cache_start + stream->cache_size) {
			int result = range_fetch(stream, stream->position);
			if (result < 0) return total ? (int)total : result;
		}
		size_t offset = (size_t)(stream->position - stream->cache_start);
		size_t available = stream->cache_size - offset;
		size_t wanted = size - total;
		if (wanted > available) wanted = available;
		memcpy((unsigned char *)data + total, stream->cache + offset, wanted);
		stream->position += wanted;
		total += wanted;
	}
	return (int)total;
}

static int64_t range_seek(void *opaque, int64_t offset, int origin) {
	RangeStream *stream = (RangeStream *)opaque;
	if (!stream) return VITA_HTTPS_ERROR_INVALID_ARGUMENT;
	int64_t base = origin == SEEK_SET ? 0 :
	               origin == SEEK_CUR ? (int64_t)stream->position :
	               origin == SEEK_END ? (int64_t)stream->size : -1;
	if (base < 0 || offset < -base) return VITA_HTTPS_ERROR_INVALID_ARGUMENT;
	int64_t next = base + offset;
	if (next < 0 || (uint64_t)next > stream->size)
		return VITA_HTTPS_ERROR_INVALID_ARGUMENT;
	stream->position = (uint64_t)next;
	return next;
}

static void range_close(void *opaque) {
	RangeStream *stream = (RangeStream *)opaque;
	if (!stream) return;
	free(stream->cache);
	free(stream->url);
	free(stream);
}

int vita_https_open_range_stream(VitaHttpsClient *client, const char *url,
	                             volatile int *cancel_flag,
	                             VitaHttpsStream *out) {
	if (!client || !out || !valid_request_url(url, client->allow_http))
		return VITA_HTTPS_ERROR_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	VitaHttpsResponse head = {0};
	VitaHttpsRequest request = {
		.method = "HEAD", .url = url, .cancel_flag = cancel_flag
	};
	int result = vita_https_perform(client, &request, &head);
	if (result < 0) return result;
	if (head.content_length <= 0) return VITA_HTTPS_ERROR_RANGE_UNSUPPORTED;
	RangeStream *stream = calloc(1, sizeof(*stream));
	if (!stream) return VITA_HTTPS_ERROR_OUT_OF_MEMORY;
	stream->url = strdup(url);
	stream->cache = malloc(RANGE_CACHE_SIZE);
	if (!stream->url || !stream->cache) {
		range_close(stream);
		return VITA_HTTPS_ERROR_OUT_OF_MEMORY;
	}
	stream->client = client;
	stream->cancel = cancel_flag;
	stream->size = (uint64_t)head.content_length;
	CURL *curl = curl_easy_init();
	if (!curl) result = VITA_HTTPS_ERROR_OUT_OF_MEMORY;
	else {
		apply_common(curl, client, cancel_flag);
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");
		FixedBuffer probe = { stream->cache, 0, 1 };
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fixed_write);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &probe);
		CURLcode code = curl_easy_perform(curl);
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_cleanup(curl);
		result = code == CURLE_OK && status == 206 && probe.size == 1
		       ? 0 : VITA_HTTPS_ERROR_RANGE_UNSUPPORTED;
		stream->cache_start = 0;
		stream->cache_size = result == 0 ? 1 : 0;
	}
	if (result < 0) {
		range_close(stream);
		return result;
	}
	out->opaque = stream;
	out->read = range_read;
	out->seek = range_seek;
	out->close = range_close;
	out->size = (int64_t)stream->size;
	return 0;
}

const char *vita_https_error_string(int error) {
	if (error <= VITA_HTTPS_ERROR_CURL_BASE)
		return curl_easy_strerror((CURLcode)(VITA_HTTPS_ERROR_CURL_BASE - error));
	switch (error) {
	case 0: return "success";
	case VITA_HTTPS_ERROR_INVALID_ARGUMENT: return "invalid argument or non-HTTPS URL";
	case VITA_HTTPS_ERROR_NOT_INITIALIZED: return "vita_https_init was not called";
	case VITA_HTTPS_ERROR_OUT_OF_MEMORY: return "out of memory";
	case VITA_HTTPS_ERROR_HTTP_STATUS: return "HTTP request failed";
	case VITA_HTTPS_ERROR_RANGE_UNSUPPORTED: return "verified byte ranges are unavailable";
	case VITA_HTTPS_ERROR_UNTRUSTED_CERTIFICATE: return "TLS certificate is not trusted";
	case VITA_HTTPS_ERROR_PIN_MISMATCH: return "TLS public-key pin mismatch";
	case VITA_HTTPS_ERROR_CERTIFICATE_INFO: return "TLS public-key fingerprint unavailable";
	default: return "Vita network initialization failed";
	}
}
