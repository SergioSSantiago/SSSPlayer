/* Exact VitaShell package promote path (GPL-3.0, TheFloW).
 * loadScePaf → PromoterUtil → scePromoterUtilityPromotePkgWithRif(path, 1). */

#include "system/pkg_promote.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/sha1.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/shellutil.h>
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

	memset(sha1, 0, sizeof(sha1));
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

static int file_exists(const char *path) {
	SceIoStat st;
	memset(&st, 0, sizeof(st));
	return path && path[0] && sceIoGetstat(path, &st) >= 0;
}

/* VitaShell loadScePaf — opt.flags must be sizeof(opt), unused = -1. */
static int load_sce_paf(void) {
	static uint32_t argp[] = {0x180000, (uint32_t)-1, (uint32_t)-1, 1,
	                          (uint32_t)-1, (uint32_t)-1};
	int result = -1;
	SceSysmoduleOpt opt;
	memset(&opt, 0, sizeof(opt));
	opt.flags = (int)sizeof(opt);
	opt.result = &result;
	opt.unused[0] = -1;
	opt.unused[1] = -1;
	return sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,
	                                             sizeof(argp), argp, &opt);
}

static int unload_sce_paf(void) {
	SceSysmoduleOpt opt;
	memset(&opt, 0, sizeof(opt));
	return sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0,
	                                               NULL, &opt);
}

/* VitaShell makeHeadBin — always rewrite head.bin for this package dir. */
static int make_head_bin(const char *path) {
	char tmp_path[512];
	uint8_t *sfo_buffer = NULL;
	uint8_t *head_bin = NULL;
	uint8_t hmac[16];
	char titleid[12];
	char contentid[48];
	char full_title_id[48];
	uint32_t off, len, out;
	SceUID fd;
	int size;
	int res = -1;
	size_t i;

	snprintf(tmp_path, sizeof(tmp_path), "%s/sce_sys/package/head.bin", path);
	sceIoRemove(tmp_path);

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
	/* VitaShell enforces 9-char uppercase TITLE_ID. */
	if (strlen(titleid) != 9) {
		free(sfo_buffer);
		return -2;
	}
	for (i = 0; i < 9; i++) {
		if (titleid[i] >= 'a' && titleid[i] <= 'z')
			titleid[i] = (char)(titleid[i] - 'a' + 'A');
	}
	sfo_string(sfo_buffer, "CONTENT_ID", contentid, sizeof(contentid));
	free(sfo_buffer);

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

/* VitaShell promoteApp() — sync=1, PAF required, power lock during promote. */
int sss_pkg_promote(const char *path) {
	char eboot[512];
	char sfo[512];
	int ret;

	if (!path || !path[0]) return -1;

	snprintf(eboot, sizeof(eboot), "%s/eboot.bin", path);
	snprintf(sfo, sizeof(sfo), "%s/sce_sys/param.sfo", path);
	if (!file_exists(eboot) || !file_exists(sfo)) return -2;

	ret = make_head_bin(path);
	if (ret < 0) return ret;

	/* VitaShell powerLock(): keep PS button / suspend from interrupting. */
	sceShellUtilInitEvents(0);
	sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);

	ret = load_sce_paf();
	if (ret < 0) {
		sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
		return ret;
	}

	ret = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	if (ret < 0) {
		unload_sce_paf();
		sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
		return ret;
	}
	ret = scePromoterUtilityInit();
	if (ret < 0) {
		sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
		unload_sce_paf();
		sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
		return ret;
	}

	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
	/* Exact VitaShell: PromotePkgWithRif("ux0:data/pkg", 1). */
	ret = scePromoterUtilityPromotePkgWithRif(path, 1);

	scePromoterUtilityExit();
	sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	unload_sce_paf();
	sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
	return ret;
}
