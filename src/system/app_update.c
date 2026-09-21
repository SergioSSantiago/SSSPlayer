#include "system/app_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include <vita_https.h>

#include "common/zip_extract.h"
#include "i18n/i18n.h"
#include "network/download_manager.h"
#include "settings/preferences.h"
#include "system/pkg_promote.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/theme.h"

#ifndef SSSPLAYER_VERSION_LABEL
#define SSSPLAYER_VERSION_LABEL "0.0.0"
#endif

#define UPDATE_API_URL \
	"https://api.github.com/repos/SergioSSantiago/SSSPlayer/releases/latest"
#define UPDATE_VPK_PATH "ux0:data/SSSPlayer/update/SSSPlayer-update.vpk"
#define UPDATE_VPK_FALLBACK "ux0:SSSPlayer-update.vpk"
/* Same short package dir VitaShell uses — promoter is picky about paths. */
#define UPDATE_PKG_DIR "ux0:data/pkg"
#define UPDATE_DIR "ux0:data/SSSPlayer/update"

typedef struct {
	char tag[32];
	char asset_url[512];
	char asset_name[128];
} UpdateInfo;

static int version_parts(const char *text, int *a, int *b, int *c) {
	*a = *b = *c = 0;
	if (!text || !text[0]) return -1;
	if (text[0] == 'v' || text[0] == 'V') text++;
	return sscanf(text, "%d.%d.%d", a, b, c) >= 1 ? 0 : -1;
}

static int version_is_newer(const char *remote, const char *local) {
	int ra, rb, rc, la, lb, lc;
	if (version_parts(remote, &ra, &rb, &rc) < 0) return 0;
	if (version_parts(local, &la, &lb, &lc) < 0) return 1;
	if (ra != la) return ra > la;
	if (rb != lb) return rb > lb;
	return rc > lc;
}

typedef struct {
	char *data;
	size_t size;
	size_t capacity;
} MemBuffer;

static size_t mem_write(const void *data, size_t size, void *opaque) {
	MemBuffer *buf = opaque;
	if (!buf) return 0;
	if (buf->size + size + 1 > buf->capacity) {
		size_t next = buf->capacity ? buf->capacity * 2 : 8192;
		while (next < buf->size + size + 1) next *= 2;
		char *grown = realloc(buf->data, next);
		if (!grown) return 0;
		buf->data = grown;
		buf->capacity = next;
	}
	memcpy(buf->data + buf->size, data, size);
	buf->size += size;
	buf->data[buf->size] = '\0';
	return size;
}

static int fetch_latest(UpdateInfo *out) {
	VitaHttpsClientConfig config;
	VitaHttpsClient *client;
	VitaHttpsRequest request;
	VitaHttpsResponse response;
	MemBuffer buffer;
	const char *headers[3];
	json_t *root;
	json_error_t error;
	json_t *tag;
	json_t *assets;
	size_t i;

	memset(out, 0, sizeof(*out));
	memset(&config, 0, sizeof(config));
	config.user_agent = "SSSPlayer-Updater/" SSSPLAYER_VERSION_LABEL;
	config.connect_timeout_ms = 10000;
	config.request_timeout_ms = 20000;
	client = vita_https_client_create(&config);
	if (!client) return -1;

	memset(&buffer, 0, sizeof(buffer));
	headers[0] = "Accept: application/vnd.github+json";
	headers[1] = "X-GitHub-Api-Version: 2022-11-28";
	headers[2] = NULL;
	memset(&request, 0, sizeof(request));
	request.method = "GET";
	request.url = UPDATE_API_URL;
	request.headers = headers;
	request.write = mem_write;
	request.write_opaque = &buffer;
	memset(&response, 0, sizeof(response));
	if (vita_https_perform(client, &request, &response) < 0 ||
	    response.status_code < 200 || response.status_code >= 300 ||
	    !buffer.data) {
		free(buffer.data);
		vita_https_client_destroy(client);
		return -1;
	}
	vita_https_client_destroy(client);

	root = json_loads(buffer.data, 0, &error);
	free(buffer.data);
	if (!root) return -1;
	tag = json_object_get(root, "tag_name");
	if (!json_is_string(tag)) {
		json_decref(root);
		return -1;
	}
	snprintf(out->tag, sizeof(out->tag), "%s", json_string_value(tag));
	assets = json_object_get(root, "assets");
	if (json_is_array(assets)) {
		for (i = 0; i < json_array_size(assets); i++) {
			json_t *asset = json_array_get(assets, i);
			const char *name =
			    json_string_value(json_object_get(asset, "name"));
			const char *url = json_string_value(
			    json_object_get(asset, "browser_download_url"));
			if (!name || !url) continue;
			if (!strstr(name, ".vpk")) continue;
			snprintf(out->asset_name, sizeof(out->asset_name), "%s", name);
			snprintf(out->asset_url, sizeof(out->asset_url), "%s", url);
			break;
		}
	}
	json_decref(root);
	return out->asset_url[0] ? 0 : -1;
}

/* VitaShell asks twice: install? then unsafe/homebrew warning. */
static int prompt_yes_no(const char *title, const char *detail) {
	SceCtrlData controls, previous;

	memset(&controls, 0, sizeof(controls));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	for (;;) {
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		vita2d_start_drawing();
		vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
		ui_brand_draw_header_placeholder(NULL, title);
		ui_panel(160, 160, 640, 220, VT_THEME_SURFACE_RAISED,
		         VT_THEME_BLUE_LIGHT, 0);
		if (body)
			ui_font_draw_text(body, 196, 220, VT_THEME_TEXT, UI_FONT_BODY,
			                  title);
		if (small)
			ui_font_draw_text(small, 196, 258, VT_THEME_TEXT_MUTED,
			                  UI_FONT_SMALL, detail);
		ui_action_button(196, 300, 260, 48, VT_THEME_SIGNAL,
		                 "Cross", vt_i18n_str(VT_STR_UPDATE_INSTALL), 1);
		ui_action_button(484, 300, 260, 48, VT_THEME_SURFACE,
		                 "Circle", vt_i18n_str(VT_STR_UPDATE_LATER), 0);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();

		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned int pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		if (pressed & SCE_CTRL_CROSS) return 1;
		if (pressed & SCE_CTRL_CIRCLE) return 0;
		sceKernelDelayThread(1000);
	}
}

static int prompt_install(const char *tag) {
	char title[96];
	char detail[160];

	snprintf(title, sizeof(title), "%s",
	         vt_i18n_str(VT_STR_UPDATE_AVAILABLE_TITLE));
	snprintf(detail, sizeof(detail),
	         vt_i18n_str(VT_STR_UPDATE_AVAILABLE_DETAIL), tag,
	         SSSPLAYER_VERSION_LABEL);
	if (!prompt_yes_no(title, detail)) return 0;
	/* Second confirm — same pattern as VitaShell INSTALL_WARNING. */
	return prompt_yes_no(vt_i18n_str(VT_STR_UPDATE_WARNING_TITLE),
	                     vt_i18n_str(VT_STR_UPDATE_WARNING_DETAIL));
}

static void remove_tree(const char *path) {
	SceUID dir = sceIoDopen(path);
	if (dir >= 0) {
		SceIoDirent ent;
		while (sceIoDread(dir, &ent) > 0) {
			char child[512];
			if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, ".."))
				continue;
			snprintf(child, sizeof(child), "%s/%s", path, ent.d_name);
			if (SCE_S_ISDIR(ent.d_stat.st_mode)) remove_tree(child);
			else sceIoRemove(child);
		}
		sceIoDclose(dir);
		sceIoRmdir(path);
	} else {
		sceIoRemove(path);
	}
}

static int copy_file(const char *src, const char *dst) {
	char buf[64 * 1024];
	SceUID in, out;
	int n;
	in = sceIoOpen(src, SCE_O_RDONLY, 0);
	if (in < 0) return in;
	sceIoRemove(dst);
	out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (out < 0) {
		sceIoClose(in);
		return out;
	}
	while ((n = sceIoRead(in, buf, sizeof(buf))) > 0) {
		if (sceIoWrite(out, buf, (SceSize)n) != n) {
			sceIoClose(out);
			sceIoClose(in);
			sceIoRemove(dst);
			return -1;
		}
	}
	sceIoClose(out);
	sceIoClose(in);
	return n < 0 ? n : 0;
}

typedef struct {
	char vpk_path[512];
	volatile long stage; /* 1=extract 2=promote */
	volatile int cancel;
	int result;
} InstallJob;

static int install_job_run(void *opaque) {
	InstallJob *job = opaque;
	char eboot_check[320];
	SceIoStat st;

	if (!job) return -1;
	job->stage = 1;
	sceIoMkdir("ux0:data", 0777);
	/* VitaShell: removePath(PACKAGE_DIR) then extract into PACKAGE_DIR. */
	remove_tree(UPDATE_PKG_DIR);
	if (job->cancel) return -1;
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
	if (sss_zip_extract(job->vpk_path, UPDATE_PKG_DIR) < 0) {
		job->result = -10;
		return -1;
	}
	snprintf(eboot_check, sizeof(eboot_check), "%s/eboot.bin", UPDATE_PKG_DIR);
	memset(&st, 0, sizeof(st));
	if (sceIoGetstat(eboot_check, &st) < 0) {
		job->result = -11;
		return -1;
	}
	/* Do not honour cancel once promote starts (VitaShell cannot abort mid-promote). */
	job->stage = 2;
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
	job->result = sss_pkg_promote(UPDATE_PKG_DIR);
	/* VitaShell cleans PACKAGE_DIR after promote (moved/emptied). */
	remove_tree(UPDATE_PKG_DIR);
	return job->result < 0 ? -1 : 0;
}

static int install_update(const UpdateInfo *info) {
	VtDownloadJob job;
	InstallJob install;
	int result;

	sceIoMkdir("ux0:data/SSSPlayer", 0777);
	sceIoMkdir(UPDATE_DIR, 0777);
	remove_tree(UPDATE_PKG_DIR);
	sceIoRemove(UPDATE_VPK_PATH);

	vt_download_job_init_url(&job, info->asset_url);
	vt_download_job_set_destination(&job, UPDATE_DIR);
	vt_download_job_set_filename(&job, "SSSPlayer-update.vpk");
	result = ui_loading_run_download(
	    vt_i18n_str(VT_STR_UPDATE_DOWNLOADING), vt_download_run, &job,
	    &job.paused, &job.cancel, &job.progress_current, &job.progress_total);
	if (result != 0) {
		ui_message_show(vt_i18n_str(VT_STR_UPDATE_FAILED_TITLE),
		                job.cancel ? vt_i18n_str(VT_STR_UPDATE_CANCELLED)
		                           : job.detail, 3200);
		return -1;
	}

	/* Keep a VitaShell-ready copy before promote (self-update can hang). */
	copy_file(job.destination, UPDATE_VPK_FALLBACK);

	memset(&install, 0, sizeof(install));
	snprintf(install.vpk_path, sizeof(install.vpk_path), "%s", job.destination);
	install.stage = 1;
	result = ui_loading_run(vt_i18n_str(VT_STR_UPDATE_INSTALLING), install_job_run,
	                       &install, &install.cancel, &install.stage, NULL);
	if (result != 0 || install.result < 0) {
		char detail[96];
		if (install.result == -10 || install.result == -11)
			snprintf(detail, sizeof(detail), "%s",
			         vt_i18n_str(VT_STR_UPDATE_EXTRACT_FAILED));
		else
			snprintf(detail, sizeof(detail), "0x%08X",
			         (unsigned)install.result);
		ui_message_show(vt_i18n_str(VT_STR_UPDATE_FAILED_TITLE), detail, 3200);
		ui_message_show(vt_i18n_str(VT_STR_UPDATE_MANUAL_TITLE),
		                vt_i18n_str(VT_STR_UPDATE_MANUAL_DETAIL), 5000);
		/* Keep ux0:SSSPlayer-update.vpk for VitaShell; drop the cache copy. */
		sceIoRemove(job.destination);
		return -1;
	}
	sceIoRemove(job.destination);
	sceIoRemove(UPDATE_VPK_FALLBACK);
	ui_message_show(vt_i18n_str(VT_STR_UPDATE_DONE_TITLE),
	                vt_i18n_str(VT_STR_UPDATE_DONE_DETAIL), 2000);
	/* Fresh eboot is on disk; exit so LiveArea relaunches the new build. */
	sceKernelDelayThread(800 * 1000);
	sceKernelExitProcess(0);
	return 0;
}

void sss_app_update_check_on_launch(void) {
	UpdateInfo info;

	vt_preferences_init();
	vt_i18n_init();
	if (ui_runtime_attach_existing() < 0) {
		if (ui_runtime_init() < 0) return;
	}
	ui_runtime_load_boot_assets();
	ui_runtime_load_assets();
	if (vita_https_init() < 0) return;
	if (!vita_https_is_connected()) return;
	if (fetch_latest(&info) < 0) return;
	if (!version_is_newer(info.tag, SSSPLAYER_VERSION_LABEL)) return;
	if (!prompt_install(info.tag)) return;
	install_update(&info);
}

void sss_app_update_check_manual(void) {
	UpdateInfo info;

	vt_preferences_init();
	vt_i18n_init();
	if (ui_runtime_attach_existing() < 0) {
		if (ui_runtime_init() < 0) {
			ui_message_show(vt_i18n_str(VT_STR_UPDATE_OFFLINE_TITLE),
			                vt_i18n_str(VT_STR_UPDATE_OFFLINE_DETAIL), 2800);
			return;
		}
	}
	ui_runtime_load_boot_assets();
	ui_runtime_load_assets();
	ui_message_show(vt_i18n_str(VT_STR_UPDATE_CHECKING),
	                SSSPLAYER_VERSION_LABEL, 900);
	if (vita_https_init() < 0 || !vita_https_is_connected() ||
	    fetch_latest(&info) < 0) {
		ui_message_show(vt_i18n_str(VT_STR_UPDATE_OFFLINE_TITLE),
		                vt_i18n_str(VT_STR_UPDATE_OFFLINE_DETAIL), 3000);
		return;
	}
	if (!version_is_newer(info.tag, SSSPLAYER_VERSION_LABEL)) {
		char detail[96];
		snprintf(detail, sizeof(detail),
		         vt_i18n_str(VT_STR_UPDATE_UP_TO_DATE_DETAIL),
		         SSSPLAYER_VERSION_LABEL);
		ui_message_show(vt_i18n_str(VT_STR_UPDATE_UP_TO_DATE_TITLE), detail,
		                2800);
		return;
	}
	if (!prompt_install(info.tag)) return;
	install_update(&info);
}
