#include "ui/yt_screen.h"

#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "network/download_manager.h"
#include "network/yt_client.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/destination_picker.h"
#include "ui/font.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define YT_LIST_X 52
#define YT_LIST_Y 128
#define YT_LIST_W 856
#define YT_ROW_H 52
#define YT_VISIBLE 6

typedef struct {
	YtSearchResult results[YT_MAX_RESULTS];
	int count;
	int selected;
	int top;
	char query[128];
	char status[160];
} YtScreenState;

typedef struct {
	const char *query;
	YtSearchResult *results;
	int max_results;
	int *count;
	char *detail;
	size_t detail_size;
} YtSearchJob;

static int yt_search_worker(void *opaque)
{
	YtSearchJob *job = opaque;
	int count;
	if (!job) return -1;
	count = yt_client_search(job->query, job->results, job->max_results,
	                         job->detail, job->detail_size);
	if (count < 0) return -1;
	*job->count = count;
	return 0;
}

typedef struct {
	const char *video_id;
	YtResolvedMedia *media;
	char *detail;
	size_t detail_size;
} YtResolveJob;

static int yt_resolve_worker(void *opaque)
{
	YtResolveJob *job = opaque;
	if (!job) return -1;
	return yt_client_resolve(job->video_id, job->media, job->detail,
	                         job->detail_size);
}

typedef struct {
	const char *src;
	const char *dst;
	char *detail;
	size_t detail_size;
} YtRemuxJob;

static int yt_remux_worker(void *opaque)
{
	YtRemuxJob *job = opaque;
	if (!job) return -1;
	return yt_client_remux_audio_m4a(job->src, job->dst, job->detail,
	                                 job->detail_size);
}

static void format_duration(int seconds, char *out, size_t out_size)
{
	if (seconds <= 0) {
		snprintf(out, out_size, "--:--");
		return;
	}
	snprintf(out, out_size, "%d:%02d", seconds / 60, seconds % 60);
}

static void draw_yt_screen(const YtScreenState *state)
{
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	int i;

	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
	ui_brand_draw_header("YouTube");
	ui_scene_identity(52, 68, 760, "MEDIA/YT", "YouTube",
	                  state->query[0] ? state->query : "Search videos");

	ui_panel(YT_LIST_X, 108, YT_LIST_W, 36, VT_THEME_SURFACE_RAISED,
	         VT_THEME_BLUE_LIGHT, 0);
	if (small) {
		ui_font_draw_text(small, YT_LIST_X + 14, 132, VT_THEME_SIGNAL_LIGHT,
		                  UI_FONT_SMALL,
		                  state->status[0] ? state->status
		                                   : "Square=Search  Cross=Open  Circle=Back");
	}

	if (state->count <= 0) {
		ui_panel(YT_LIST_X, YT_LIST_Y + 20, YT_LIST_W, 220, VT_THEME_SURFACE,
		         VT_THEME_BLUE_LIGHT, 0);
		if (body)
			ui_font_draw_text(body, YT_LIST_X + 24, YT_LIST_Y + 120, VT_THEME_TEXT,
			                  UI_FONT_BODY,
			                  state->query[0] ? "No results" : "Press Square to search");
	} else {
		for (i = state->top; i < state->count && i < state->top + YT_VISIBLE; i++) {
			const YtSearchResult *item = &state->results[i];
			char meta[96];
			char duration[16];
			int y = YT_LIST_Y + 20 + (i - state->top) * YT_ROW_H;
			int selected = (i == state->selected);
			ui_panel(YT_LIST_X, y, YT_LIST_W, YT_ROW_H - 4,
			         selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
			         VT_THEME_BLUE_LIGHT, 0);
			if (selected)
				vita2d_draw_rectangle(YT_LIST_X, y, 4, YT_ROW_H - 4,
				                      VT_THEME_SIGNAL_BRIGHT);
			format_duration(item->length_seconds, duration, sizeof(duration));
			snprintf(meta, sizeof(meta), "%s  ·  %s",
			         item->author[0] ? item->author : "Unknown", duration);
			if (body)
				ui_font_draw_text(body, YT_LIST_X + 18, y + 28, VT_THEME_TEXT,
				                  UI_FONT_BODY, item->title);
			if (small)
				ui_font_draw_text(small, YT_LIST_X + 18, y + 46,
				                  VT_THEME_TEXT_DIM, UI_FONT_SMALL, meta);
		}
	}

	ui_action_button(52, 468, 210, 40, VT_THEME_BLUE_BRIGHT, "Square",
	                 "Search", 0);
	ui_action_button(278, 468, 210, 40, VT_THEME_SURFACE_RAISED, "Cross",
	                 "Open", 0);
	ui_action_button(504, 468, 210, 40, VT_THEME_SURFACE, "Triangle",
	                 "Download", 0);
	ui_action_button(730, 468, 178, 40, VT_THEME_SURFACE, "Circle", "Back", 0);

	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

static int draw_action_menu(const YtSearchResult *item, int *choice)
{
	/* choice: 0 play, 1 download video, 2 download audio, -1 cancel */
	SceCtrlData previous;
	int selected = 0;
	const char *labels[] = {
		"Play video",
		"Download video",
		"Download audio (M4A)",
		"Cancel"
	};

	memset(&previous, 0, sizeof(previous));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	for (;;) {
		int i;
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		vita2d_start_drawing();
		vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
		ui_brand_draw_header("YouTube");
		ui_panel(80, 90, 800, 70, VT_THEME_SURFACE_RAISED, VT_THEME_BLUE_LIGHT, 0);
		if (body)
			ui_font_draw_text(body, 100, 130, VT_THEME_TEXT, UI_FONT_BODY,
			                  item->title);
		if (small)
			ui_font_draw_text(small, 100, 150, VT_THEME_TEXT_DIM, UI_FONT_SMALL,
			                  item->author);
		for (i = 0; i < 4; i++) {
			int y = 190 + i * 56;
			ui_panel(80, y, 800, 50,
			         i == selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
			         VT_THEME_BLUE_LIGHT, 0);
			if (i == selected)
				vita2d_draw_rectangle(80, y, 4, 50, VT_THEME_SIGNAL_BRIGHT);
			if (body)
				ui_font_draw_text(body, 110, y + 32, VT_THEME_TEXT, UI_FONT_BODY,
				                  labels[i]);
		}
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();

		{
			SceCtrlData controls;
			unsigned pressed;
			sceCtrlPeekBufferPositive(0, &controls, 1);
			pressed = controls.buttons & ~previous.buttons;
			previous = controls;
			if ((pressed & SCE_CTRL_UP) && selected > 0) selected--;
			if ((pressed & SCE_CTRL_DOWN) && selected < 3) selected++;
			if (pressed & SCE_CTRL_CROSS) {
				*choice = selected == 3 ? -1 : selected;
				return 1;
			}
			if (pressed & SCE_CTRL_CIRCLE) {
				*choice = -1;
				return 1;
			}
		}
		sceKernelDelayThread(16 * 1000);
	}
}

static int run_yt_download(const char *url, const char *filename,
                            UiDestKind kind, int require_h264)
{
	VtDownloadJob job;
	char destination[512];

	if (!url || !url[0]) {
		ui_message_show("Download failed", "No media URL available", 2800);
		return -1;
	}
	if (!ui_destination_picker_kind(kind, NULL, destination, sizeof(destination)))
		return -1;

	vt_download_job_init_url(&job, url);
	vt_download_job_set_destination(&job, destination);
	if (filename && filename[0])
		vt_download_job_set_filename(&job, filename);

	{
		int result = ui_loading_run_download(
		    vt_i18n_str(VT_STR_NETWORK_DOWNLOADING), vt_download_run, &job,
		    &job.paused, &job.cancel, &job.progress_current, &job.progress_total);
		if (result == 0) {
			if (require_h264 && !yt_client_file_has_h264(job.destination)) {
				sceIoRemove(job.destination);
				ui_message_show(
				    "Unsupported video",
				    "Need progressive 360p H.264 MP4 (itag 18). Re-download.",
				    3600);
				return -1;
			}
			ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_COMPLETE),
			                job.destination, 2800);
		} else if (job.cancel)
			ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_ABORTED),
			                job.destination, 2400);
		else
			ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_FAILED),
			                job.detail[0] ? job.detail : "Transfer failed", 3000);
		return result;
	}
}

static int resolve_and_act(const YtSearchResult *item, UiYtSelection *selection,
                           int *played)
{
	YtResolvedMedia media;
	YtResolveJob job;
	char detail[192];
	char base[96];
	char filename[128];
	int choice = -1;
	int resolve_ok;

	*played = 0;
	if (!draw_action_menu(item, &choice) || choice < 0) return 0;

	memset(&media, 0, sizeof(media));
	detail[0] = '\0';
	job.video_id = item->id;
	job.media = &media;
	job.detail = detail;
	job.detail_size = sizeof(detail);
	resolve_ok = ui_loading_run("Resolving streams…", yt_resolve_worker, &job,
	                            NULL, NULL, NULL);
	if (resolve_ok != 0) {
		ui_message_show("Resolve failed",
		                detail[0] ? detail : "No playable streams", 3200);
		return 0;
	}

	yt_client_safe_filename(media.title[0] ? media.title : item->title, base,
	                        sizeof(base));

	if (choice == 0) {
		if (!media.video_url[0]) {
			ui_message_show(
			    "Playback failed",
			    "No H.264 progressive stream for this video", 3200);
			return 0;
		}
		if (!selection) return 0;
		memset(selection, 0, sizeof(*selection));
		snprintf(selection->video_url, sizeof(selection->video_url), "%s",
		         media.video_url);
		snprintf(selection->title, sizeof(selection->title), "%s",
		         media.title[0] ? media.title : item->title);
		snprintf(selection->author, sizeof(selection->author), "%s",
		         media.author[0] ? media.author : item->author);
		snprintf(selection->video_id, sizeof(selection->video_id), "%s",
		         item->id);
		*played = 1;
		return 1;
	}

	if (choice == 1) {
		if (!media.video_url[0]) {
			ui_message_show(
			    "Download failed",
			    "No H.264 progressive stream for this video", 3200);
			return 0;
		}
		/* Always .mp4: progressive itag 18 is muxed H.264+AAC. */
		snprintf(filename, sizeof(filename), "%s.mp4", base);
		run_yt_download(media.video_url, filename, UI_DEST_KIND_VIDEO, 1);
		return 0;
	}

	if (choice == 2) {
		if (media.audio_url[0]) {
			snprintf(filename, sizeof(filename), "%s.%s", base,
			         media.audio_ext[0] ? media.audio_ext : "m4a");
			run_yt_download(media.audio_url, filename, UI_DEST_KIND_AUDIO, 0);
			return 0;
		}

		if (media.audio_via_progressive && media.video_url[0]) {
			char temp_name[128];
			char folder[512];
			char final_path[512];
			char remux_detail[160];
			VtDownloadJob dl;
			YtRemuxJob remux;
			char *yt;

			snprintf(temp_name, sizeof(temp_name), "%s.yt.mp4", base);
			if (!ui_destination_picker_kind(UI_DEST_KIND_AUDIO, NULL, folder,
			                               sizeof(folder)))
				return 0;
			vt_download_job_init_url(&dl, media.video_url);
			vt_download_job_set_destination(&dl, folder);
			vt_download_job_set_filename(&dl, temp_name);
			if (ui_loading_run_download(
			        "Downloading audio…", vt_download_run, &dl, &dl.paused,
			        &dl.cancel, &dl.progress_current, &dl.progress_total) != 0) {
				if (!dl.cancel)
					ui_message_show(
					    vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_FAILED),
					    dl.detail[0] ? dl.detail : "Transfer failed", 3000);
				return 0;
			}

			snprintf(final_path, sizeof(final_path), "%s", dl.destination);
			yt = strstr(final_path, ".yt.mp4");
			if (!yt) {
				ui_message_show("Audio extract failed", "Bad temp path", 2800);
				sceIoRemove(dl.destination);
				return 0;
			}
			snprintf(yt, (size_t)(sizeof(final_path) - (size_t)(yt - final_path)),
			         ".m4a");

			remux_detail[0] = '\0';
			remux.src = dl.destination;
			remux.dst = final_path;
			remux.detail = remux_detail;
			remux.detail_size = sizeof(remux_detail);
			if (ui_loading_run("Extracting audio…", yt_remux_worker, &remux,
			                   NULL, NULL, NULL) != 0) {
				sceIoRemove(dl.destination);
				sceIoRemove(final_path);
				ui_message_show("Audio extract failed",
				                remux_detail[0] ? remux_detail
				                                : "Could not create M4A",
				                3200);
				return 0;
			}
			sceIoRemove(dl.destination);
			ui_message_show("Audio saved", final_path, 3200);
			return 0;
		}

		ui_message_show("Download failed", "No audio stream URL", 2800);
		return 0;
	}
	return 0;
}

int ui_yt_screen(UiYtSelection *selection)
{
	YtScreenState state;
	SceCtrlData previous;
	UiNavRepeat nav_repeat;

	memset(&state, 0, sizeof(state));
	snprintf(state.status, sizeof(state.status),
	         "Square=Search  Cross=Open  Triangle=Download  Circle=Back");
	memset(&previous, 0, sizeof(previous));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_nav_repeat_reset(&nav_repeat);

	for (;;) {
		draw_yt_screen(&state);
		{
			SceCtrlData controls;
			unsigned pressed;
			unsigned nav;
			sceCtrlPeekBufferPositive(0, &controls, 1);
			pressed = controls.buttons & ~previous.buttons;
			previous = controls;
			nav = ui_nav_repeat_update(&nav_repeat, pressed, controls.buttons,
			                           controls.lx, controls.ly,
			                           SCE_CTRL_UP | SCE_CTRL_DOWN);

			if ((nav & SCE_CTRL_UP) && state.selected > 0) {
				state.selected--;
				if (state.selected < state.top) state.top = state.selected;
			}
			if ((nav & SCE_CTRL_DOWN) && state.selected + 1 < state.count) {
				state.selected++;
				if (state.selected >= state.top + YT_VISIBLE)
					state.top = state.selected - YT_VISIBLE + 1;
			}

			if (pressed & SCE_CTRL_SQUARE) {
				char query[128];
				snprintf(query, sizeof(query), "%s", state.query);
				if (ui_text_input("Search YouTube", query, query,
				                  sizeof(query)) > 0) {
					YtSearchJob job;
					char detail[192];
					snprintf(state.query, sizeof(state.query), "%s", query);
					detail[0] = '\0';
					job.query = state.query;
					job.results = state.results;
					job.max_results = YT_MAX_RESULTS;
					job.count = &state.count;
					job.detail = detail;
					job.detail_size = sizeof(detail);
					state.count = 0;
					state.selected = state.top = 0;
					if (ui_loading_run("Searching…", yt_search_worker, &job,
					                   NULL, NULL, NULL) != 0) {
						snprintf(state.status, sizeof(state.status), "%s",
						         detail[0] ? detail : "Search failed");
						ui_message_show("Search failed",
						                detail[0] ? detail : "Try again later",
						                3000);
					} else {
						snprintf(state.status, sizeof(state.status),
						         "%d result%s", state.count,
						         state.count == 1 ? "" : "s");
					}
					ui_touch_reset();
					sceCtrlPeekBufferPositive(0, &previous, 1);
					ui_nav_repeat_reset(&nav_repeat);
				}
			}

			if ((pressed & (SCE_CTRL_CROSS | SCE_CTRL_TRIANGLE)) &&
			    state.count > 0) {
				int played = 0;
				/* Triangle jumps straight to download menu still via action menu */
				(void)pressed;
				if (resolve_and_act(&state.results[state.selected], selection,
				                    &played) &&
				    played)
					return UI_YT_ACTION_PLAY;
				ui_touch_reset();
				sceCtrlPeekBufferPositive(0, &previous, 1);
				ui_nav_repeat_reset(&nav_repeat);
			}

			if (pressed & SCE_CTRL_CIRCLE)
				return UI_YT_ACTION_BACK;
		}
		sceKernelDelayThread(16 * 1000);
	}
}
