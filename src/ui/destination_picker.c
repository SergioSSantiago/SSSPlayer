#include "ui/destination_picker.h"

#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "network/network_source.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/font.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define DESTINATION_MAX_FOLDERS 96

static int destination_join(const char *directory, const char *name,
                            char *out, size_t out_size)
{
	size_t length = strlen(directory);
	const char *separator = length && directory[length - 1] == ':' ? "" : "/";
	int written = snprintf(out, out_size, "%s%s%s", directory, separator, name);
	return written > 0 && written < (int)out_size ? 0 : -1;
}

static void destination_parent(char *path)
{
	char *colon, *slash;
	if (!path || !path[0]) return;
	colon = strchr(path, ':');
	slash = strrchr(path, '/');
	if (slash && (!colon || slash > colon)) *slash = '\0';
	else if (colon && colon[1]) colon[1] = '\0';
}

static int destination_read_folders(const char *path, char names[][128],
                                    int capacity)
{
	SceUID directory = sceIoDopen(path);
	int count = 0;
	if (directory < 0) return directory;
	while (count < capacity) {
		SceIoDirent entry;
		memset(&entry, 0, sizeof(entry));
		int result = sceIoDread(directory, &entry);
		if (result <= 0) break;
		if (entry.d_name[0] == '.' || !SCE_S_ISDIR(entry.d_stat.st_mode)) continue;
		snprintf(names[count++], 128, "%s", entry.d_name);
	}
	sceIoDclose(directory);
	return count;
}

static int destination_name_valid(const char *name)
{
	return name && name[0] && !strchr(name, '/') && !strchr(name, ':') &&
	       strcmp(name, ".") && strcmp(name, "..");
}

static void draw_destination_picker(const char *path, char names[][128],
                                    int count, int selected, int top)
{
	vita2d_font *body;
	vita2d_font *small;
	int i;

	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
	ui_brand_draw_header(vt_i18n_str(VT_STR_NETWORK_DESTINATION_TITLE));
	body = ui_runtime_font(UI_FONT_BODY);
	small = ui_runtime_font(UI_FONT_SMALL);
	ui_scene_identity(66, 68, 720, "LOCAL/DEST",
	                  vt_i18n_str(VT_STR_NETWORK_DESTINATION_TITLE),
	                  vt_i18n_str(VT_STR_NETWORK_DESTINATION_DETAIL));
	ui_panel(66, 124, 828, 54, VT_THEME_SURFACE_RAISED, VT_THEME_BLUE_LIGHT, 0);
	if (small) {
		char fitted[VT_NETWORK_PATH_MAX];
		ui_font_draw_text(small, 84, 145, VT_THEME_SIGNAL_LIGHT,
		                  UI_FONT_SMALL, vt_i18n_str(VT_STR_NETWORK_CURRENT_DESTINATION));
		ui_font_fit_text(small, UI_FONT_SMALL, path, fitted, sizeof(fitted), 602);
		ui_font_draw_text(small, 274, 163, VT_THEME_TEXT, UI_FONT_SMALL, fitted);
	}
	if (!count) {
		ui_panel(66, 190, 828, 220, VT_THEME_SURFACE, VT_THEME_BLUE_LIGHT, 0);
		if (body)
			ui_font_draw_text(body, 94, 274, VT_THEME_TEXT, UI_FONT_BODY,
			                  vt_i18n_str(VT_STR_NETWORK_EMPTY_FOLDER));
	} else {
		for (i = top; i < count && i < top + 4; i++) {
			int y = 190 + (i - top) * 56;
			ui_panel(66, y, 828, 54, VT_THEME_SURFACE, VT_THEME_BLUE_LIGHT, 0);
			if (i == selected)
				vita2d_draw_rectangle(66, y, 4, 54, VT_THEME_SIGNAL_BRIGHT);
			if (body)
				ui_font_draw_text(body, 92, y + 34, VT_THEME_TEXT, UI_FONT_BODY,
				                  names[i]);
		}
	}
	ui_action_button(66, 468, 250, 42, VT_THEME_BLUE_BRIGHT, "Start",
	                 vt_i18n_str(VT_STR_NETWORK_USE_THIS_FOLDER), 0);
	ui_action_button(350, 468, 250, 42, VT_THEME_SURFACE_RAISED, "Triangle",
	                 vt_i18n_str(VT_STR_NETWORK_NEW_FOLDER), 0);
	ui_action_button(634, 468, 260, 42, VT_THEME_SURFACE, "Circle",
	                 !strcmp(path, "ux0:") ? vt_i18n_str(VT_STR_NETWORK_CANCEL)
	                                      : "Up",
	                 0);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_destination_picker(const char *start_path, char *out, size_t out_size)
{
	char path[VT_NETWORK_PATH_MAX];
	char names[DESTINATION_MAX_FOLDERS][128];
	SceCtrlData previous;
	UiNavRepeat nav_repeat;
	int selected = 0, top = 0;

	if (!out || out_size == 0) return 0;
	if (start_path && start_path[0])
		snprintf(path, sizeof(path), "%s", start_path);
	else
		snprintf(path, sizeof(path), "ux0:download");
	sceIoMkdir("ux0:download", 0777);
	sceIoMkdir("ux0:video", 0777);
	sceIoMkdir("ux0:music", 0777);
	memset(&previous, 0, sizeof(previous));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_nav_repeat_reset(&nav_repeat);

	for (;;) {
		int count = destination_read_folders(path, names, DESTINATION_MAX_FOLDERS);
		if (count < 0) {
			ui_message_show(vt_i18n_str(VT_STR_NETWORK_FOLDER_CREATE_FAILED), path,
			                2600);
			return 0;
		}
		if (selected >= count) selected = count > 0 ? count - 1 : 0;
		if (top > selected) top = selected;
		if (selected >= top + 4) top = selected - 3;
		draw_destination_picker(path, names, count, selected, top);

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
			if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
			if ((nav & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
			if (pressed & SCE_CTRL_START) {
				snprintf(out, out_size, "%s", path);
				return 1;
			}
			if ((pressed & SCE_CTRL_CROSS) && count > 0) {
				char next[VT_NETWORK_PATH_MAX];
				if (destination_join(path, names[selected], next, sizeof(next)) == 0) {
					snprintf(path, sizeof(path), "%s", next);
					selected = top = 0;
				}
			}
			if (pressed & SCE_CTRL_TRIANGLE) {
				char name[128] = "";
				if (ui_text_input(vt_i18n_str(VT_STR_NETWORK_FOLDER_NAME_PROMPT), "",
				                  name, sizeof(name)) > 0 &&
				    destination_name_valid(name)) {
					char next[VT_NETWORK_PATH_MAX];
					if (destination_join(path, name, next, sizeof(next)) < 0 ||
					    sceIoMkdir(next, 0777) < 0)
						ui_message_show(vt_i18n_str(VT_STR_NETWORK_FOLDER_CREATE_FAILED),
						                name, 2600);
				}
				ui_touch_reset();
				sceCtrlPeekBufferPositive(0, &previous, 1);
				ui_nav_repeat_reset(&nav_repeat);
			}
			if (pressed & SCE_CTRL_CIRCLE) {
				if (!strcmp(path, "ux0:")) return 0;
				destination_parent(path);
				selected = top = 0;
			}
		}
		sceKernelDelayThread(16 * 1000);
	}
}
