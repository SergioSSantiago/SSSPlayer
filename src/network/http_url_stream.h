#ifndef SSSPLAYER_NETWORK_HTTP_URL_STREAM_H
#define SSSPLAYER_NETWORK_HTTP_URL_STREAM_H

#include "media/vita_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Seekable HTTP(S) Range stream for direct media URLs (e.g. resolved video).
 * The factory owns a copy of the URL; destroy with http_url_stream_factory_free. */
typedef struct {
	VtDecoderStreamFactory factory;
	char *url;
} HttpUrlStreamFactory;

int http_url_stream_factory_init(HttpUrlStreamFactory *factory, const char *url);
void http_url_stream_factory_free(HttpUrlStreamFactory *factory);

#ifdef __cplusplus
}
#endif

#endif
