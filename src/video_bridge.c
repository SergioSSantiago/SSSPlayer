/*
 * SSSPlayer – bridge from the music shell into the video player stack.
 */
#include "video_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>

#include "app_paths.h"
#include "common/text_log.h"
#include "history/playback_history.h"
#include "i18n/i18n.h"
#include "media/background_playback.h"
#include "media/hw_player_screen.h"
#include "media/video_thumbnail.h"
#include "media/vita_decoder.h"
#include "network/network_source.h"
#include "settings/preferences.h"
#include "ui/components.h"
#include "ui/image_viewer.h"
#include "ui/local_files_screen.h"
#include "ui/local_media_screen.h"
#include "ui/loading_screen.h"
#include "ui/network_sources_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/touch.h"

#include <vita_https.h>

static int g_video_ready;
static int g_network_ready;

static void str_tolower_copy(char *dst, const char *src, size_t n)
{
	size_t i;
	for (i = 0; i + 1 < n && src[i]; i++) {
		char c = src[i];
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		dst[i] = c;
	}
	dst[i] = '\0';
}

int sss_video_is_path(const char *path)
{
	const char *ext = NULL;
	const char *p;
	char lext[8];
	if (!path) return 0;
	for (p = path; *p; p++)
		if (*p == '.') ext = p + 1;
	if (!ext) return 0;
	str_tolower_copy(lext, ext, sizeof(lext));
	return strcmp(lext, "mp4") == 0 || strcmp(lext, "m4v") == 0 ||
	       strcmp(lext, "mkv") == 0 || strcmp(lext, "avi") == 0 ||
	       strcmp(lext, "mov") == 0 || strcmp(lext, "webm") == 0 ||
	       strcmp(lext, "m2ts") == 0 || strcmp(lext, "ts") == 0;
}

static void media_id(const char *path, char out[16])
{
	vt_playback_history_local_id(path, out);
}

static void remote_media_id(const UiNetworkSelection *selection, char out[16])
{
	vt_network_media_history_id(selection ? &selection->source : NULL,
	                            selection ? selection->path : NULL, out);
}

static int ensure_video_runtime(void)
{
	if (g_video_ready) return 0;

	log_init();
	vt_preferences_init();
	vt_i18n_init();
	ui_touch_init();
	if (ui_runtime_attach_existing() < 0) {
		if (ui_runtime_init() < 0) return -1;
	}
	ui_runtime_load_boot_assets();
	ui_runtime_load_assets();
	vt_background_playback_init();
	vt_playback_history_init();
	vt_video_thumbnail_init();
	g_network_ready = vita_https_init() >= 0;
	if (g_network_ready) g_network_ready = vt_network_init() >= 0;
	g_video_ready = 1;
	return 0;
}

static int play_local_video_path(const char *path, const char *title)
{
	VtDecoderStreamFactory factory;
	VtHwPlayerScreenSource source;
	char id[16];
	uint64_t last_position;
	uint64_t last_duration = 0;
	int last_audio = 0, last_subtitle = 0;
	int ret;

	if (!path || !path[0]) return -1;
	if (ensure_video_runtime() < 0) return -1;

	vt_video_thumbnail_prepare_playback();
	media_id(path, id);
	vt_decoder_file_stream_factory(path, &factory);
	memset(&source, 0, sizeof(source));
	source.stream = factory;
	source.title = title && title[0] ? title : path;
	source.location = path;
	source.history_id = id;
	source.allow_minimize = 0;
	last_position = vt_playback_history_position(id, 0);
	source.start_position_ms = last_position;

	ret = vt_hw_player_screen_run(&source, &last_position, &last_duration,
	                              &last_audio, &last_subtitle);
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	vt_playback_history_update(id, last_position, last_duration);
	return ret;
}

int sss_video_play_local(const char *path, const char *title)
{
	return play_local_video_path(path, title);
}

static int run_remote_video(const UiNetworkSelection *selection)
{
	VtNetworkStreamFactory remote;
	VtHwPlayerScreenSource source;
	char id[16];
	uint64_t last_position;
	uint64_t last_duration = 0;
	int last_audio = 0, last_subtitle = 0;
	int ret;

	if (!selection) return -1;
	vt_video_thumbnail_prepare_playback();
	ret = vt_network_stream_factory_init(&remote, &selection->source,
	                                     &selection->credential,
	                                     selection->path);
	if (ret < 0) return ret;
	remote_media_id(selection, id);
	memset(&source, 0, sizeof(source));
	source.stream = remote.factory;
	source.title = selection->title;
	source.location = selection->source.name;
	source.history_id = id;
	source.authenticated_remote = 1;
	source.allow_minimize = 0;
	last_position = vt_playback_history_position(id, 0);
	source.start_position_ms = last_position;
	ret = vt_hw_player_screen_run(&source, &last_position, &last_duration,
	                              &last_audio, &last_subtitle);
	memset(&remote.credential, 0, sizeof(remote.credential));
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	vt_playback_history_update(id, last_position, last_duration);
	return ret;
}

int sss_video_browse_library(void)
{
	int folder_browser = 0;
	int folder_resume = 0;
	char folder_root[VT_LOCAL_MEDIA_PATH_MAX] = "";
	VtLocalMediaType folder_filter = 0;

	if (ensure_video_runtime() < 0) return -1;

	for (;;) {
		VtLocalMediaItem item;
		int action = folder_browser
		    ? ui_local_files_screen_open(folder_resume ? NULL : folder_root,
		                                 folder_filter, &item)
		    : ui_local_media_screen(&item);
		if (folder_browser) folder_resume = 1;
		if (action >= UI_LOCAL_MEDIA_ACTION_SECTION_BASE)
			return 0;
		if (action == UI_LOCAL_MEDIA_ACTION_BACK) {
			if (folder_browser) {
				folder_browser = folder_resume = 0;
				folder_root[0] = '\0';
				continue;
			}
			return 0;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_BROWSE_FILES) {
			folder_browser = 1;
			folder_resume = 0;
			folder_root[0] = '\0';
			folder_filter = 0;
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_BROWSE_FOLDER) {
			folder_browser = 1;
			folder_resume = 0;
			snprintf(folder_root, sizeof(folder_root), "%s", item.path);
			folder_filter = item.type;
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_PLAY) {
			if (item.type == VT_LOCAL_MEDIA_IMAGE) {
				ui_image_viewer_show(item.path, item.name);
			} else if (item.type == VT_LOCAL_MEDIA_VIDEO) {
				play_local_video_path(item.path, item.name);
			}
			/* Audio stays on the music engine. */
		}
	}
}

int sss_video_browse_network(void)
{
	if (ensure_video_runtime() < 0) return -1;
	if (!g_network_ready) {
		ui_message_show(vt_i18n_str(VT_STR_MAIN_NETWORK_UNAVAILABLE),
		                vt_i18n_str(VT_STR_MAIN_LOCAL_AVAILABLE), 2600);
		return -1;
	}

	for (;;) {
		UiNetworkSelection selection;
		int action = ui_network_sources_screen(&selection);
		if (action >= UI_NETWORK_ACTION_SECTION_BASE) return 0;
		if (action != UI_NETWORK_ACTION_PLAY) return 0;
		if (run_remote_video(&selection) < 0)
			ui_message_show(vt_i18n_str(VT_STR_MAIN_STREAMING_FAILED),
			                vt_i18n_str(VT_STR_MAIN_STREAMING_DETAIL), 3200);
		memset(&selection.credential, 0, sizeof(selection.credential));
	}
}
