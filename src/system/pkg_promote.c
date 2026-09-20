/* head.bin generation adapted from VitaShell / VitaDeploy (GPL-3.0). */

#include "system/pkg_promote.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/sha1.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>

#include "system/head_bin.h"

#define ntohl __builtin_bswap32
#define SFO_MAGIC 0x46535000U

typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t keyofs;
	uint32_t valofs;
	uint32_t count;
} SfoHeader;

typedef struct {
	uint16_t nameofs;
	uint8_t alignment;
	uint8_t type;
	uint32_t valsize;
	uint32_t totalsize;
	uint32_t dataofs;
} SfoEntry;

static int sfo_string(const void *buffer, const char *name, char *out,
                      size_t out_len) {
	const SfoHeader *header = buffer;
	const SfoEntry *entries;
	uint32_t i;

	if (!buffer || !name || !out || out_len == 0) return -1;
	if (header->magic != SFO_MAGIC) return -1;
	entries = (const SfoEntry *)((const uint8_t *)buffer + sizeof(SfoHeader));
	for (i = 0; i < header->count; i++) {
		const char *key =
		    (const char *)buffer + header->keyofs + entries[i].nameofs;
		if (strcmp(key, name) != 0) continue;
		memset(out, 0, out_len);
		strncpy(out,
		        (const char *)buffer + header->valofs + entries[i].dataofs,
		        out_len - 1);
		return 0;
	}
	return -2;
}

static void fpkg_hmac(const uint8_t *data, unsigned int len, uint8_t hmac[16]) {
	uint8_t sha1[20];
	uint8_t buf[64];

	mbedtls_sha1(data, len, sha1);
	memset(buf, 0, sizeof(buf));
	memcpy(&buf[0], &sha1[4], 8);
	memcpy(&buf[8], &sha1[4], 8);
	memcpy(&buf[16], &sha1[12], 4);
	buf[20] = sha1[16];
	buf[21] = sha1[1];
	buf[22] = sha1[2];
	buf[23] = sha1[3];
	memcpy(&buf[24], &buf[16], 8);
	mbedtls_sha1(buf, 64, sha1);
	memcpy(hmac, sha1, 16);
}

static int make_head_bin(const char *path) {
	char tmp_path[512];
	uint8_t *sfo_buffer = NULL;
	uint8_t *head_bin = NULL;
	uint8_t hmac[16];
	char titleid[16];
	char contentid[48];
	char full_title_id[48];
	uint32_t off, len, out;
	SceUID fd;
	int size;
	int res = -1;

	snprintf(tmp_path, sizeof(tmp_path), "%s/sce_sys/param.sfo", path);
	fd = sceIoOpen(tmp_path, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	size = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
	sceIoLseek(fd, 0, SCE_SEEK_SET);
	if (size <= 0) {
		sceIoClose(fd);
		return -1;
	}
	sfo_buffer = malloc((size_t)size);
	if (!sfo_buffer || sceIoRead(fd, sfo_buffer, (size_t)size) != size) {
		free(sfo_buffer);
		sceIoClose(fd);
		return -1;
	}
	sceIoClose(fd);

	memset(titleid, 0, sizeof(titleid));
	memset(contentid, 0, sizeof(contentid));
	if (sfo_string(sfo_buffer, "TITLE_ID", titleid, sizeof(titleid)) < 0) {
		free(sfo_buffer);
		return -1;
	}
	sfo_string(sfo_buffer, "CONTENT_ID", contentid, sizeof(contentid));
	free(sfo_buffer);
	sfo_buffer = NULL;

	head_bin = malloc(tpl_head_bin_len);
	if (!head_bin) return -1;
	memcpy(head_bin, tpl_head_bin, tpl_head_bin_len);

	snprintf(full_title_id, sizeof(full_title_id),
	         "EP9000-%s_00-0000000000000000", titleid);
	strncpy((char *)&head_bin[0x30],
	        contentid[0] ? contentid : full_title_id, 48);

	len = ntohl(*(uint32_t *)&head_bin[0xD0]);
	fpkg_hmac(&head_bin[0], len, hmac);
	memcpy(&head_bin[len], hmac, 16);

	off = ntohl(*(uint32_t *)&head_bin[0x8]);
	len = ntohl(*(uint32_t *)&head_bin[0x10]);
	out = ntohl(*(uint32_t *)&head_bin[0xD4]);
	fpkg_hmac(&head_bin[off], len - 64, hmac);
	memcpy(&head_bin[out], hmac, 16);

	len = ntohl(*(uint32_t *)&head_bin[0xE8]);
	fpkg_hmac(&head_bin[0], len, hmac);
	memcpy(&head_bin[len], hmac, 16);

	snprintf(tmp_path, sizeof(tmp_path), "%s/sce_sys/package", path);
	sceIoMkdir(tmp_path, 0777);
	snprintf(tmp_path, sizeof(tmp_path), "%s/sce_sys/package/head.bin", path);
	fd = sceIoOpen(tmp_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) {
		free(head_bin);
		return fd;
	}
	if (sceIoWrite(fd, head_bin, tpl_head_bin_len) == (int)tpl_head_bin_len)
		res = 0;
	sceIoClose(fd);
	free(head_bin);
	return res;
}

int sss_pkg_promote(const char *path) {
	int ret;

	if (!path || !path[0]) return -1;
	ret = make_head_bin(path);
	if (ret < 0) return ret;

	ret = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	if (ret < 0) return ret;
	ret = scePromoterUtilityInit();
	if (ret < 0) {
		sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
		return ret;
	}
	/* Sync install; WithRif matches VitaShell / VitaDeploy for homebrew VPKs. */
	ret = scePromoterUtilityPromotePkgWithRif(path, 1);
	scePromoterUtilityExit();
	sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	return ret;
}
