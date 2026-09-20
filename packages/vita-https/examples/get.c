#include <string.h>

#include "vita_https.h"

typedef struct Buffer {
	char *data;
	size_t used;
	size_t capacity;
} Buffer;

static size_t copy_response(const void *data, size_t size, void *opaque) {
	Buffer *buffer = opaque;
	if (size > buffer->capacity - buffer->used) return 0;
	memcpy(buffer->data + buffer->used, data, size);
	buffer->used += size;
	return size;
}

int download_json(const char *url, char *output, size_t capacity) {
	int result = vita_https_init();
	if (result < 0) return result;
	VitaHttpsClient *client = vita_https_client_create(NULL);
	if (!client) {
		vita_https_shutdown();
		return VITA_HTTPS_ERROR_OUT_OF_MEMORY;
	}
	Buffer buffer = { output, 0, capacity > 0 ? capacity - 1 : 0 };
	VitaHttpsRequest request = {
		.method = "GET",
		.url = url,
		.write = copy_response,
		.write_opaque = &buffer
	};
	VitaHttpsResponse response;
	result = vita_https_perform(client, &request, &response);
	if (capacity) output[buffer.used] = '\0';
	vita_https_client_destroy(client);
	vita_https_shutdown();
	return result;
}
