#include "ui/music_library_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include <vita_https.h>

#include "i18n/i18n.h"
#include "media/image_loader.h"
#include "media/music_library.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/focus_glow.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/theme.h"

enum {
	TAB_ARTISTS = 0,
	TAB_ALBUMS = 1,
	TAB_SONGS = 2,
	TAB_COUNT = 3
};

enum {
	VIEW_TABS = 0,
	VIEW_ARTIST_ALBUMS = 1,
	VIEW_ALBUM_SONGS = 2
};

#define GRID_COLS 4
#define GRID_CARD 148
#define GRID_GAP 12
#define GRID_X 48
#define GRID_Y 140
#define LIST_X 48
#define LIST_Y 140
#define LIST_W 864
#define LIST_ROW 52
#define ART_CACHE 24

typedef struct {
	char path[SSS_MUSIC_PATH_MAX];
	vita2d_texture *tex;
	uint64_t used;
} ArtSlot;

typedef struct {
	char root[SSS_MUSIC_PATH_MAX];
	SssMusicLibrary *lib;
	volatile int *cancel;
} ScanJob;

static ArtSlot g_art[ART_CACHE];
static uint64_t g_art_clock;

static void art_cache_clear(void) {
	int i;
	for (i = 0; i < ART_CACHE; i++) {
		if (g_art[i].tex) vita2d_free_texture(g_art[i].tex);
		memset(&g_art[i], 0, sizeof(g_art[i]));
	}
	g_art_clock = 0;
}

static vita2d_texture *art_get(const char *path) {
	int i, worst = 0;
	uint64_t oldest;
	VtImageInfo info;
	char error[64];
	if (!path || !path[0]) return NULL;
	g_art_clock++;
	for (i = 0; i < ART_CACHE; i++) {
		if (g_art[i].tex && !strcmp(g_art[i].path, path)) {
			g_art[i].used = g_art_clock;
			return g_art[i].tex;
		}
	}
	oldest = g_art[0].used;
	for (i = 1; i < ART_CACHE; i++) {
		if (g_art[i].used < oldest) {
			oldest = g_art[i].used;
			worst = i;
		}
	}
	if (g_art[worst].tex) vita2d_free_texture(g_art[worst].tex);
	memset(&g_art[worst], 0, sizeof(g_art[worst]));
	memset(&info, 0, sizeof(info));
	error[0] = '\0';
	g_art[worst].tex =
	    vt_image_load_texture(path, 256, &info, error, sizeof(error));
	if (!g_art[worst].tex) return NULL;
	snprintf(g_art[worst].path, sizeof(g_art[worst].path), "%s", path);
	g_art[worst].used = g_art_clock;
	return g_art[worst].tex;
}

static void draw_cover(float x, float y, float size, const char *path) {
	vita2d_texture *tex = art_get(path);
	ui_panel(x, y, size, size, VT_THEME_SURFACE_RAISED, VT_THEME_BLUE_LIGHT, 0);
	if (tex) {
		float tw = (float)vita2d_texture_get_width(tex);
		float th = (float)vita2d_texture_get_height(tex);
		float scale = size / (tw > th ? tw : th);
		float dw = tw * scale;
		float dh = th * scale;
		vita2d_draw_texture_scale(tex, x + (size - dw) * 0.5f,
		                          y + (size - dh) * 0.5f, scale, scale);
	} else {
		vita2d_font *font = ui_runtime_font(UI_FONT_DISPLAY);
		if (font)
			ui_font_draw_text(font, x + size * 0.38f, y + size * 0.55f,
			                  VT_THEME_TEXT_MUTED, UI_FONT_DISPLAY, "♪");
	}
}

static int scan_job_run(void *opaque) {
	ScanJob *job = opaque;
	if (!job || !job->lib) return -1;
	if (job->cancel && *job->cancel) return -1;
	return sss_music_library_scan(job->lib, job->root);
}

static void enrich_covers(SssMusicLibrary *lib, int limit) {
	int i, done = 0;
	if (!lib || !vita_https_is_connected()) return;
	for (i = 0; i < lib->album_count && done < limit; i++) {
		SssMusicAlbum *alb = &lib->albums[i];
		char out[SSS_MUSIC_PATH_MAX];
		if (alb->artwork[0]) continue;
		if (sss_music_cover_ensure(alb->artist, alb->name, NULL, out) == 0) {
			snprintf(alb->artwork, sizeof(alb->artwork), "%s", out);
			done++;
		}
	}
	for (i = 0; i < lib->artist_count; i++) {
		SssMusicArtist *art = &lib->artists[i];
		int a;
		if (art->artwork[0]) continue;
		for (a = 0; a < lib->album_count; a++) {
			if (!strcasecmp(lib->albums[a].artist, art->name) &&
			    lib->albums[a].artwork[0]) {
				snprintf(art->artwork, sizeof(art->artwork), "%s",
				         lib->albums[a].artwork);
				break;
			}
		}
	}
}

static int album_index_for_artist(const SssMusicLibrary *lib, const char *artist,
                                  int nth) {
	int i, seen = 0;
	for (i = 0; i < lib->album_count; i++) {
		if (strcasecmp(lib->albums[i].artist, artist)) continue;
		if (seen == nth) return i;
		seen++;
	}
	return -1;
}

static int track_index_filtered(const SssMusicLibrary *lib, const char *artist,
                                const char *album, int nth) {
	int i, seen = 0;
	for (i = 0; i < lib->track_count; i++) {
		if (artist && strcasecmp(lib->tracks[i].artist, artist)) continue;
		if (album && strcasecmp(lib->tracks[i].album, album)) continue;
		if (seen == nth) return i;
		seen++;
	}
	return -1;
}

static int count_albums_for_artist(const SssMusicLibrary *lib,
                                   const char *artist) {
	int i, n = 0;
	for (i = 0; i < lib->album_count; i++)
		if (!strcasecmp(lib->albums[i].artist, artist)) n++;
	return n;
}

static int count_tracks_filtered(const SssMusicLibrary *lib, const char *artist,
                                 const char *album) {
	int i, n = 0;
	for (i = 0; i < lib->track_count; i++) {
		if (artist && strcasecmp(lib->tracks[i].artist, artist)) continue;
		if (album && strcasecmp(lib->tracks[i].album, album)) continue;
		n++;
	}
	return n;
}

int ui_music_library_run(const char *root, char *play_path,
                         size_t play_path_size) {
	SssMusicLibrary lib;
	ScanJob job;
	int tab = TAB_ARTISTS;
	int view = VIEW_TABS;
	int selected = 0;
	int top = 0;
	int focus_tabs = 1;
	char filter_artist[SSS_MUSIC_ARTIST_MAX] = "";
	char filter_album[SSS_MUSIC_ALBUM_MAX] = "";
	SceCtrlData controls, previous;
	UiFocusMotion focus_motion;
	volatile int cancel = 0;

	if (!root || !root[0]) return UI_MUSIC_LIB_BACK;
	if (play_path && play_path_size) play_path[0] = '\0';

	memset(&lib, 0, sizeof(lib));
	memset(&job, 0, sizeof(job));
	snprintf(job.root, sizeof(job.root), "%s", root);
	job.lib = &lib;
	job.cancel = &cancel;

	if (ui_runtime_attach_existing() < 0) ui_runtime_init();
	ui_runtime_load_boot_assets();
	ui_runtime_load_assets();
	vita_https_init();

	if (ui_loading_run(vt_i18n_str(VT_STR_MUSIC_LIB_BUILDING), scan_job_run,
	                   &job, &cancel, NULL, NULL) != 0 ||
	    lib.track_count <= 0) {
		ui_message_show(vt_i18n_str(VT_STR_MUSIC_LIB_EMPTY),
		                vt_i18n_str(VT_STR_MUSIC_LIB_EMPTY_DETAIL), 2800);
		sss_music_library_free(&lib);
		return UI_MUSIC_LIB_BACK;
	}
	ui_message_show(vt_i18n_str(VT_STR_MUSIC_LIB_COVERS), "", 500);
	enrich_covers(&lib, 16);

	memset(&controls, 0, sizeof(controls));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_focus_motion_reset(&focus_motion);
	art_cache_clear();

	for (;;) {
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		int count = 0;
		int grid = 0;
		char title[96];
		char detail[128];
		int i;

		if (view == VIEW_TABS) {
			if (tab == TAB_ARTISTS) {
				count = lib.artist_count;
				grid = 1;
			} else if (tab == TAB_ALBUMS) {
				count = lib.album_count;
				grid = 1;
			} else {
				count = lib.track_count;
				grid = 0;
			}
			snprintf(title, sizeof(title), "%s",
			         vt_i18n_str(VT_STR_MUSIC_LIB_TITLE));
			snprintf(detail, sizeof(detail),
			         vt_i18n_str(VT_STR_MUSIC_LIB_SUMMARY), lib.artist_count,
			         lib.album_count, lib.track_count);
		} else if (view == VIEW_ARTIST_ALBUMS) {
			count = count_albums_for_artist(&lib, filter_artist);
			grid = 1;
			snprintf(title, sizeof(title), "%s", filter_artist);
			snprintf(detail, sizeof(detail), "%s",
			         vt_i18n_str(VT_STR_MUSIC_LIB_ALBUMS));
		} else {
			count = count_tracks_filtered(&lib, filter_artist, filter_album);
			grid = 0;
			snprintf(title, sizeof(title), "%s", filter_album);
			snprintf(detail, sizeof(detail), "%s", filter_artist);
		}

		if (count <= 0) selected = 0;
		else if (selected >= count) selected = count - 1;
		if (selected < 0) selected = 0;

		if (grid) {
			int rows = 2;
			int max_top =
			    count > rows * GRID_COLS ? (count - 1) / GRID_COLS - rows + 1
			                             : 0;
			if (max_top < 0) max_top = 0;
			top = selected / GRID_COLS;
			if (top > max_top) top = max_top;
			if (top < 0) top = 0;
		} else {
			int rows = 6;
			if (selected < top) top = selected;
			if (selected >= top + rows) top = selected - rows + 1;
			if (top < 0) top = 0;
		}

		vita2d_start_drawing();
		vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_SIGNAL);
		ui_brand_draw_header_placeholder(NULL, title);
		ui_scene_identity(48, 72, 860, "LIB/M", title, detail);

		if (view == VIEW_TABS) {
			const char *tabs[TAB_COUNT] = {
			    vt_i18n_str(VT_STR_MUSIC_LIB_ARTISTS),
			    vt_i18n_str(VT_STR_MUSIC_LIB_ALBUMS),
			    vt_i18n_str(VT_STR_MUSIC_LIB_SONGS)};
			for (i = 0; i < TAB_COUNT; i++) {
				float x = 48.f + i * 180.f;
				int on = (tab == i);
				ui_panel(x, 100, 168, 32,
				         on ? VT_THEME_SIGNAL : VT_THEME_SURFACE,
				         VT_THEME_BLUE_LIGHT, focus_tabs && on);
				if (small)
					ui_font_draw_text(
					    small, x + 16, 120,
					    on ? VT_THEME_BG : VT_THEME_TEXT, UI_FONT_SMALL,
					    tabs[i]);
			}
		}

		if (count <= 0) {
			if (body)
				ui_font_draw_text(body, 48, 220, VT_THEME_TEXT_MUTED,
				                  UI_FONT_BODY,
				                  vt_i18n_str(VT_STR_MUSIC_LIB_EMPTY));
		} else if (grid) {
			int slot;
			for (slot = 0; slot < 2 * GRID_COLS; slot++) {
				int idx = top * GRID_COLS + slot;
				float x = GRID_X + (slot % GRID_COLS) * (GRID_CARD + GRID_GAP);
				float y =
				    GRID_Y + (slot / GRID_COLS) * (GRID_CARD + 44);
				const char *name = "";
				const char *sub = "";
				const char *art = NULL;
				char sub_buf[96];
				int alb_i;
				if (idx >= count) break;
				sub_buf[0] = '\0';
				if (view == VIEW_TABS && tab == TAB_ARTISTS) {
					name = lib.artists[idx].name;
					art = lib.artists[idx].artwork;
					snprintf(sub_buf, sizeof(sub_buf),
					         vt_i18n_str(VT_STR_MUSIC_LIB_ALBUMS_COUNT),
					         lib.artists[idx].album_count);
					sub = sub_buf;
				} else if (view == VIEW_TABS && tab == TAB_ALBUMS) {
					name = lib.albums[idx].name;
					sub = lib.albums[idx].artist;
					art = lib.albums[idx].artwork;
				} else {
					alb_i = album_index_for_artist(&lib, filter_artist, idx);
					if (alb_i < 0) continue;
					name = lib.albums[alb_i].name;
					sub = lib.albums[alb_i].artist;
					art = lib.albums[alb_i].artwork;
				}
				ui_panel(x - 4, y - 4, GRID_CARD + 8, GRID_CARD + 40,
				         VT_THEME_SURFACE, VT_THEME_BLUE_LIGHT,
				         !focus_tabs && idx == selected);
				if (!focus_tabs && idx == selected)
					ui_focus_glow_draw(x - 4, y - 4, GRID_CARD + 8,
					                   GRID_CARD + 40, 0, 0, 544);
				draw_cover(x, y, GRID_CARD, art);
				if (small) {
					ui_font_draw_text(small, x, y + GRID_CARD + 14,
					                  VT_THEME_TEXT, UI_FONT_SMALL, name);
					ui_font_draw_text(small, x, y + GRID_CARD + 30,
					                  VT_THEME_TEXT_MUTED, UI_FONT_SMALL, sub);
				}
			}
		} else {
			int row;
			for (row = 0; row < 6; row++) {
				int idx = top + row;
				float y = LIST_Y + row * LIST_ROW;
				const char *line1 = "";
				const char *line2 = "";
				const char *art = NULL;
				char sub_buf[160];
				char num[8];
				int ti;
				if (idx >= count) break;
				sub_buf[0] = '\0';
				if (view == VIEW_TABS && tab == TAB_SONGS) {
					ti = idx;
					line1 = lib.tracks[ti].title;
					snprintf(sub_buf, sizeof(sub_buf), "%s · %s",
					         lib.tracks[ti].artist, lib.tracks[ti].album);
					line2 = sub_buf;
					art = lib.tracks[ti].artwork;
				} else {
					ti = track_index_filtered(&lib, filter_artist, filter_album,
					                          idx);
					if (ti < 0) continue;
					if (lib.tracks[ti].track_no > 0)
						snprintf(num, sizeof(num), "%02d",
						         lib.tracks[ti].track_no);
					else
						snprintf(num, sizeof(num), "%02d", idx + 1);
					line1 = lib.tracks[ti].title;
					line2 = num;
					art = lib.tracks[ti].artwork;
				}
				ui_panel(LIST_X, y, LIST_W, LIST_ROW - 6, VT_THEME_SURFACE,
				         VT_THEME_BLUE_LIGHT, !focus_tabs && idx == selected);
				draw_cover(LIST_X + 8, y + 6, 36, art);
				if (body)
					ui_font_draw_text(body, LIST_X + 56, y + 22, VT_THEME_TEXT,
					                  UI_FONT_BODY, line1);
				if (small)
					ui_font_draw_text(small, LIST_X + 56, y + 40,
					                  VT_THEME_TEXT_MUTED, UI_FONT_SMALL,
					                  line2);
			}
		}

		ui_action_button(48, 500, 200, 40, VT_THEME_SIGNAL, "Cross",
		                 vt_i18n_str(VT_STR_MUSIC_LIB_OPEN), 1);
		ui_action_button(268, 500, 200, 40, VT_THEME_SURFACE, "Circle",
		                 vt_i18n_str(VT_STR_MUSIC_LIB_BACK), 0);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();

		sceCtrlPeekBufferPositive(0, &controls, 1);
		{
			unsigned int pressed = controls.buttons & ~previous.buttons;
			previous = controls;

			if (view == VIEW_TABS && focus_tabs) {
				if ((pressed & SCE_CTRL_LEFT) && tab > 0) {
					tab--;
					selected = top = 0;
				}
				if ((pressed & SCE_CTRL_RIGHT) && tab + 1 < TAB_COUNT) {
					tab++;
					selected = top = 0;
				}
				if (pressed & SCE_CTRL_DOWN) focus_tabs = 0;
			} else if (grid) {
				if ((pressed & SCE_CTRL_LEFT) && selected > 0) selected--;
				if ((pressed & SCE_CTRL_RIGHT) && selected + 1 < count)
					selected++;
				if (pressed & SCE_CTRL_UP) {
					if (selected >= GRID_COLS) selected -= GRID_COLS;
					else if (view == VIEW_TABS) focus_tabs = 1;
				}
				if ((pressed & SCE_CTRL_DOWN) && selected + GRID_COLS < count)
					selected += GRID_COLS;
			} else {
				if (pressed & SCE_CTRL_UP) {
					if (selected > 0) selected--;
					else if (view == VIEW_TABS) focus_tabs = 1;
				}
				if ((pressed & SCE_CTRL_DOWN) && selected + 1 < count)
					selected++;
			}

			if (pressed & SCE_CTRL_CIRCLE) {
				if (view == VIEW_ALBUM_SONGS) {
					view = filter_artist[0] ? VIEW_ARTIST_ALBUMS : VIEW_TABS;
					filter_album[0] = '\0';
					if (view == VIEW_TABS) {
						filter_artist[0] = '\0';
						tab = TAB_ALBUMS;
						focus_tabs = 1;
					}
					selected = top = 0;
				} else if (view == VIEW_ARTIST_ALBUMS) {
					view = VIEW_TABS;
					tab = TAB_ARTISTS;
					filter_artist[0] = '\0';
					selected = top = 0;
					focus_tabs = 1;
				} else {
					art_cache_clear();
					sss_music_library_free(&lib);
					return UI_MUSIC_LIB_BACK;
				}
			}

			if ((pressed & SCE_CTRL_CROSS) && count > 0 && !focus_tabs) {
				if (view == VIEW_TABS && tab == TAB_ARTISTS) {
					snprintf(filter_artist, sizeof(filter_artist), "%s",
					         lib.artists[selected].name);
					view = VIEW_ARTIST_ALBUMS;
					selected = top = 0;
				} else if (view == VIEW_TABS && tab == TAB_ALBUMS) {
					snprintf(filter_artist, sizeof(filter_artist), "%s",
					         lib.albums[selected].artist);
					snprintf(filter_album, sizeof(filter_album), "%s",
					         lib.albums[selected].name);
					view = VIEW_ALBUM_SONGS;
					selected = top = 0;
				} else if (view == VIEW_ARTIST_ALBUMS) {
					int alb_i =
					    album_index_for_artist(&lib, filter_artist, selected);
					if (alb_i >= 0) {
						snprintf(filter_album, sizeof(filter_album), "%s",
						         lib.albums[alb_i].name);
						view = VIEW_ALBUM_SONGS;
						selected = top = 0;
					}
				} else if (view == VIEW_TABS && tab == TAB_SONGS) {
					if (play_path && play_path_size > 0) {
						snprintf(play_path, play_path_size, "%s",
						         lib.tracks[selected].path);
						art_cache_clear();
						sss_music_library_free(&lib);
						return UI_MUSIC_LIB_PLAY;
					}
				} else if (view == VIEW_ALBUM_SONGS) {
					int ti = track_index_filtered(&lib, filter_artist,
					                              filter_album, selected);
					if (ti >= 0 && play_path && play_path_size > 0) {
						snprintf(play_path, play_path_size, "%s",
						         lib.tracks[ti].path);
						art_cache_clear();
						sss_music_library_free(&lib);
						return UI_MUSIC_LIB_PLAY;
					}
				}
			}
		}
		sceKernelDelayThread(1000);
	}
}
