#include "network/http_url_stream.h"

#include <stdlib.h>
#include <string.h>

#include <vita_https.h>

typedef struct {
	VitaHttpsClient *client;
	VitaHttpsStream stream;
	volatile int *cancel;
} HttpUrlStream;

static int http_url_read(void *opaque, void *buffer, size_t size)
{
	HttpUrlStream *stream = opaque;
	return stream && stream->stream.read
	     ? stream->stream.read(stream->stream.opaque, buffer, size) : -1;
}

static int64_t http_url_seek(void *opaque, int64_t offset, int whence)
{
	HttpUrlStream *stream = opaque;
	return stream && stream->stream.seek
	     ? stream->stream.seek(stream->stream.opaque, offset, whence) : -1;
}

static void http_url_abort(void *opaque)
{
	HttpUrlStream *stream = opaque;
	if (!stream || !stream->cancel) return;
	*stream->cancel = 1;
	__sync_synchronize();
}

static void http_url_close(void *opaque)
{
	HttpUrlStream *stream = opaque;
	if (!stream) return;
	if (stream->stream.close) stream->stream.close(stream->stream.opaque);
	vita_https_client_destroy(stream->client);
	memset(stream, 0, sizeof(*stream));
	free(stream);
}

static int http_url_open_cancelable(void *opaque, VtDecoderStreamHandle *out,
                                    volatile int *cancel_flag)
{
	HttpUrlStreamFactory *factory = opaque;
	HttpUrlStream *stream;
	VitaHttpsClientConfig config;
	int result;

	if (!factory || !factory->url || !out) return -1;
	stream = calloc(1, sizeof(*stream));
	if (!stream) return -1;

	memset(&config, 0, sizeof(config));
	config.user_agent =
	    "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip";
	config.connect_timeout_ms = 15000;
	config.request_timeout_ms = 0;
	config.low_speed_bytes_per_second = 256;
	config.low_speed_seconds = 60;
	config.allow_http = 1;
	stream->client = vita_https_client_create(&config);
	if (!stream->client) {
		http_url_close(stream);
		return -1;
	}
	stream->cancel = cancel_flag;
	result = vita_https_open_range_stream(stream->client, factory->url,
	                                      cancel_flag, &stream->stream);
	if (result < 0) {
		http_url_close(stream);
		return result;
	}
	memset(out, 0, sizeof(*out));
	out->opaque = stream;
	out->read = http_url_read;
	out->seek = http_url_seek;
	out->abort = http_url_abort;
	out->close = http_url_close;
	out->size = stream->stream.size;
	return 0;
}

static int http_url_open(void *opaque, VtDecoderStreamHandle *out)
{
	return http_url_open_cancelable(opaque, out, NULL);
}

int http_url_stream_factory_init(HttpUrlStreamFactory *factory, const char *url)
{
	size_t len;
	if (!factory || !url || !url[0]) return -1;
	memset(factory, 0, sizeof(*factory));
	len = strlen(url);
	factory->url = malloc(len + 1);
	if (!factory->url) return -1;
	memcpy(factory->url, url, len + 1);
	factory->factory.opaque = factory;
	factory->factory.open = http_url_open;
	factory->factory.open_cancelable = http_url_open_cancelable;
	return 0;
}

void http_url_stream_factory_free(HttpUrlStreamFactory *factory)
{
	if (!factory) return;
	free(factory->url);
	memset(factory, 0, sizeof(*factory));
}
