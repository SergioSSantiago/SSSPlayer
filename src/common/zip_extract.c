#include "common/zip_extract.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <zlib.h>

#pragma pack(push, 1)
typedef struct {
	uint32_t signature;
	uint16_t version;
	uint16_t flags;
	uint16_t method;
	uint16_t mod_time;
	uint16_t mod_date;
	uint32_t crc32;
	uint32_t compressed_size;
	uint32_t uncompressed_size;
	uint16_t name_len;
	uint16_t extra_len;
} ZipLocalHeader;
#pragma pack(pop)

static int ensure_parent_dirs(char *path) {
	for (char *p = path; *p; p++) {
		if (*p != '/') continue;
		if (p == path) continue;
		char save = *p;
		*p = '\0';
		if (path[0]) sceIoMkdir(path, 0777);
		*p = save;
	}
	return 0;
}

static int write_all(SceUID fd, const void *data, size_t size) {
	const unsigned char *cursor = data;
	while (size) {
		int n = sceIoWrite(fd, cursor, size);
		if (n <= 0) return -1;
		cursor += (size_t)n;
		size -= (size_t)n;
	}
	return 0;
}

static int inflate_entry(SceUID zip_fd, SceUID out_fd, uint32_t compressed,
	                     uint32_t uncompressed) {
	z_stream stream;
	unsigned char in[16 * 1024];
	unsigned char out[16 * 1024];
	uint32_t remaining = compressed;
	uint32_t written = 0;
	int ret;

	memset(&stream, 0, sizeof(stream));
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return -1;
	do {
		size_t chunk = remaining > sizeof(in) ? sizeof(in) : remaining;
		int got = 0;
		if (chunk) {
			got = sceIoRead(zip_fd, in, chunk);
			if (got <= 0) {
				inflateEnd(&stream);
				return -1;
			}
			remaining -= (uint32_t)got;
			stream.next_in = in;
			stream.avail_in = (uInt)got;
		} else {
			stream.next_in = in;
			stream.avail_in = 0;
		}
		do {
			stream.next_out = out;
			stream.avail_out = sizeof(out);
			ret = inflate(&stream, remaining ? Z_NO_FLUSH : Z_FINISH);
			if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
				inflateEnd(&stream);
				return -1;
			}
			size_t produced = sizeof(out) - stream.avail_out;
			if (produced && write_all(out_fd, out, produced) < 0) {
				inflateEnd(&stream);
				return -1;
			}
			written += (uint32_t)produced;
		} while (stream.avail_out == 0);
	} while (ret != Z_STREAM_END && remaining);
	inflateEnd(&stream);
	return written == uncompressed || uncompressed == 0 ? 0 : -1;
}

int sss_zip_extract(const char *zip_path, const char *dest_dir) {
	if (!zip_path || !dest_dir || !dest_dir[0]) return -1;
	SceUID zip_fd = sceIoOpen(zip_path, SCE_O_RDONLY, 0);
	if (zip_fd < 0) return -1;
	sceIoMkdir(dest_dir, 0777);

	for (;;) {
		ZipLocalHeader header;
		int n = sceIoRead(zip_fd, &header, sizeof(header));
		if (n == 0) break;
		if (n != (int)sizeof(header) || header.signature != 0x04034b50U) break;

		if (header.name_len == 0 || header.name_len >= 400) {
			sceIoClose(zip_fd);
			return -1;
		}
		char name[400];
		if (sceIoRead(zip_fd, name, header.name_len) != header.name_len) {
			sceIoClose(zip_fd);
			return -1;
		}
		name[header.name_len] = '\0';
		if (header.extra_len)
			sceIoLseek(zip_fd, header.extra_len, SCE_SEEK_CUR);

		char out_path[512];
		int path_n = snprintf(out_path, sizeof(out_path), "%s/%s", dest_dir, name);
		if (path_n <= 0 || path_n >= (int)sizeof(out_path)) {
			sceIoClose(zip_fd);
			return -1;
		}
		int is_dir = name[header.name_len - 1] == '/';
		if (is_dir) {
			ensure_parent_dirs(out_path);
			sceIoMkdir(out_path, 0777);
			continue;
		}
		ensure_parent_dirs(out_path);
		SceUID out_fd = sceIoOpen(out_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
		                          0666);
		if (out_fd < 0) {
			sceIoClose(zip_fd);
			return -1;
		}
		int ok = 0;
		if (header.method == 0) {
			uint32_t left = header.compressed_size;
			unsigned char buf[16 * 1024];
			ok = 1;
			while (left) {
				size_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
				int got = sceIoRead(zip_fd, buf, chunk);
				if (got <= 0 || write_all(out_fd, buf, (size_t)got) < 0) {
					ok = 0;
					break;
				}
				left -= (uint32_t)got;
			}
		} else if (header.method == 8) {
			ok = inflate_entry(zip_fd, out_fd, header.compressed_size,
			                   header.uncompressed_size) == 0;
		}
		sceIoClose(out_fd);
		if (!ok) {
			sceIoRemove(out_path);
			sceIoClose(zip_fd);
			return -1;
		}
	}
	sceIoClose(zip_fd);
	return 0;
}
