/**
 * @file media_locations_screen.c
 * @brief Let users add, edit, type, and remove media-library folders.
 * Removing a folder entry never deletes its files.
 */

#include "ui/media_locations_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "media/media_locations.h"
#include "livearea_events.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/font.h"
#include "ui/runtime.h"
#include "ui/loading_screen.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define LOCATION_LIST_X 68
#define LOCATION_LIST_Y 132
#define LOCATION_ROW_H 53
#define LOCATION_VISIBLE 6
#define PICKER_MAX_FOLDERS 256

static const uint32_t type_options[] = {
    VT_MEDIA_LOC_VIDEO, VT_MEDIA_LOC_AUDIO, VT_MEDIA_LOC_IMAGE,
    VT_MEDIA_LOC_VIDEO | VT_MEDIA_LOC_AUDIO, VT_MEDIA_LOC_ALL
};

typedef struct { char path[VT_MEDIA_LOC_PATH_MAX]; } PickerFolder;
static PickerFolder picker_folders[PICKER_MAX_FOLDERS];

/** @brief Check that a folder can actually be opened. */
static int directory_exists(const char *path) {
    if (!path || !path[0]) return 0;
    SceUID fd = sceIoDopen(path);
    if (fd < 0) return 0;
    sceIoDclose(fd);
    return 1;
}

/** @brief Shorten long folder paths without letting text spill off screen. */
static void text_line(vita2d_font *font, unsigned size, int x, int y,
                      unsigned int color, const char *s, int width) {
    if (!font || !s) return;
    char fitted[512];
    ui_font_fit_text(font, size, s, fitted, sizeof(fitted), width);
    ui_font_draw_text(font, x, y, color, size, fitted);
}

/** @brief Draw the shared folder-manager header. */
static void draw_base(const char *title, const char *hint) {
    ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
    ui_brand_draw_header(title);
    ui_scene_identity(68, 64, 820, "LOCAL/CONFIG", title, hint);
}

/** @brief Present one completed folder-manager frame. */
static void finish_frame(void) {
    vita2d_end_drawing();
    vita2d_wait_rendering_done();
    vita2d_swap_buffers();
}

/** @brief Show saved folders and their assigned media types. */
static void draw_manage(const VtMediaLocations *locations, int selected,
                        int top, int confirm) {
    vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
    vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
    vita2d_start_drawing();
    vita2d_clear_screen();
    draw_base("Media locations", "Custom folders for each Library tab");
    if (locations->count == 0) {
        ui_panel(68, 150, 824, 185, VT_THEME_SURFACE,
                 VT_THEME_BLUE_LIGHT, 0);
        text_line(body, UI_FONT_BODY, 108, 234, VT_THEME_TEXT,
                  "No media folders configured", 720);
        text_line(small, UI_FONT_SMALL, 108, 278, VT_THEME_TEXT_MUTED,
                  "Press Triangle to add a folder", 720);
    }
    for (int i = top; i < (int)locations->count && i < top + LOCATION_VISIBLE; ++i) {
        int y = LOCATION_LIST_Y + (i - top) * LOCATION_ROW_H;
        const VtMediaLocation *loc = &locations->locations[i];
        ui_panel(LOCATION_LIST_X, y, 824, LOCATION_ROW_H - 4,
                 i == selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
                 VT_THEME_BLUE_LIGHT, 0);
        if (i == selected)
            vita2d_draw_rectangle(LOCATION_LIST_X, y, 4, LOCATION_ROW_H - 4,
                                  VT_THEME_SIGNAL_BRIGHT);
        text_line(body, UI_FONT_BODY, 90, y + 29, VT_THEME_TEXT,
                  loc->path, 590);
        text_line(small, UI_FONT_SMALL, 722, y + 29, VT_THEME_SIGNAL_LIGHT,
                  vt_media_location_type_name(loc->media_mask), 150);
    }
    if (small) {
        char count[60];
        snprintf(count, sizeof(count), "%u / %u folders", locations->count,
                 (unsigned)VT_MEDIA_LOC_MAX);
        text_line(small, UI_FONT_SMALL, 72, 471, VT_THEME_TEXT_MUTED, count, 160);
    }
    ui_action_button(246, 458, 140, 42, VT_THEME_BLUE_BRIGHT, "Triangle", "Add", 0);
    ui_action_button(396, 458, 142, 42, VT_THEME_SURFACE_RAISED, "Cross", "Edit", 0);
    ui_action_button(548, 458, 154, 42, VT_THEME_SURFACE_RAISED, "Square", "Remove", 0);
    ui_action_button(712, 458, 180, 42, VT_THEME_SURFACE, "Circle", "Done", 0);
    if (confirm) {
        vita2d_draw_rectangle(0, 54, 960, 490, RGBA8(0, 3, 7, 190));
        ui_panel(176, 168, 608, 202, VT_THEME_SURFACE_RAISED,
                 VT_THEME_DANGER, 0);
        text_line(body, UI_FONT_BODY, 207, 219, VT_THEME_TEXT,
                  "Remove this media location?", 550);
        text_line(small, UI_FONT_SMALL, 207, 252, VT_THEME_TEXT_MUTED,
                  "Only the library entry is removed; no files are deleted.", 550);
        ui_action_button(206, 296, 238, 50, VT_THEME_SURFACE,
                         "Circle", "Cancel", 0);
        ui_action_button(464, 296, 278, 50, VT_THEME_DANGER,
                         "Cross", "Remove from list", 0);
    }
    finish_frame();
}

/** @brief Sort folder choices so browsing is predictable. */
static int picker_compare(const void *a, const void *b) {
    return strcasecmp(((const PickerFolder *)a)->path,
                      ((const PickerFolder *)b)->path);
}

/** @brief List immediate subfolders rather than scanning the whole device. */
static int read_folders(const char *path) {
    if (!path[0]) {
        static const char *mounts[] = {
            "ux0:", "uma0:", "imc0:", "xmc0:", "grw0:", "ur0:"
        };
        int count = 0;
        for (unsigned i = 0; i < sizeof(mounts) / sizeof(mounts[0]); ++i) {
            if (directory_exists(mounts[i])) {
                snprintf(picker_folders[count++].path, VT_MEDIA_LOC_PATH_MAX,
                         "%s", mounts[i]);
            }
        }
        return count;
    }
    SceUID fd = sceIoDopen(path);
    if (fd < 0) return -1;
    int count = 0;
    while (count < PICKER_MAX_FOLDERS) {
        SceIoDirent item;
        memset(&item, 0, sizeof(item));
        if (sceIoDread(fd, &item) <= 0) break;
        if (item.d_name[0] == '.' || !SCE_S_ISDIR(item.d_stat.st_mode)) continue;
        const char *separator = path[strlen(path) - 1] == ':' ? "" : "/";
        int n = snprintf(picker_folders[count].path, VT_MEDIA_LOC_PATH_MAX,
                         "%s%s%s", path, separator, item.d_name);
        if (n > 0 && n < VT_MEDIA_LOC_PATH_MAX) count++;
    }
    sceIoDclose(fd);
    qsort(picker_folders, count, sizeof(picker_folders[0]), picker_compare);
    return count;
}

/** @brief Return a folder parent while respecting mount roots. */
static void parent_folder(char *path) {
    if (!path[0]) return;
    char *slash = strrchr(path, '/');
    if (slash) *slash = 0;
    else {
        char *colon = strchr(path, ':');
        if (colon && colon[1]) colon[1] = 0; /* First child -> mount root. */
        else path[0] = 0; /* Mount root -> mounted-storage list. */
    }
}

/** @brief Show the folder browser with controller hints. */
static void draw_folder_picker(const char *path, int count, int selected,
                               int top) {
    vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
    vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
    vita2d_start_drawing();
    vita2d_clear_screen();
    draw_base("Choose media folder", "Select storage and browse subfolders");
    vita2d_draw_rectangle(0, 54, 960, 490, RGBA8(0, 3, 7, 165));
    ui_panel(100, 92, 760, 390, VT_THEME_SURFACE_RAISED,
             VT_THEME_BLUE_LIGHT, 0);
    text_line(body, UI_FONT_BODY, 126, 133, VT_THEME_TEXT,
              path[0] ? "Select a subfolder" : "Select a storage device", 690);
    text_line(small, UI_FONT_SMALL, 126, 160, VT_THEME_SIGNAL_LIGHT,
              path[0] ? path : "Mounted storage", 690);
    for (int i = top; i < count && i < top + 5; ++i) {
        int y = 178 + (i - top) * 47;
        ui_panel(124, y, 712, 42,
                 i == selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
                 VT_THEME_BLUE_LIGHT, 0);
        text_line(small, UI_FONT_SMALL, 143, y + 27, VT_THEME_TEXT,
                  picker_folders[i].path, 660);
    }
    if (count == 0)
        text_line(small, UI_FONT_SMALL, 143, 258, VT_THEME_TEXT_MUTED,
                  "No subfolders. You can still select the current directory.", 660);
    ui_action_button(126, 426, 205, 42, VT_THEME_BLUE_BRIGHT,
                     "Start", "Use folder", 0);
    ui_action_button(342, 426, 220, 42, VT_THEME_SURFACE,
                     "Cross", "Open folder", 0);
    ui_action_button(573, 426, 261, 42, VT_THEME_SURFACE,
                     "Circle", "Up / Cancel", 0);
    finish_frame();
}

/* Called from the editor popup. Returns 1 on selection, 0 on cancel. */
static int pick_folder(const char *initial, char out[VT_MEDIA_LOC_PATH_MAX]) {
    char path[VT_MEDIA_LOC_PATH_MAX] = "";
    if (initial && initial[0] && directory_exists(initial))
        snprintf(path, sizeof(path), "%s", initial);
    int selected = 0, top = 0;
    SceCtrlData old, pad;
    sceCtrlPeekBufferPositive(0, &old, 1);
    UiNavRepeat repeat;
    ui_nav_repeat_reset(&repeat);
    ui_touch_reset();
    for (;;) {
        if (sss_livearea_exit_requested()) {
            ui_touch_reset();
            return 0;
        }
        int count = read_folders(path);
        if (count < 0) { path[0] = '\0'; count = read_folders(path); }
        if (selected >= count) selected = count > 0 ? count - 1 : 0;
        if (selected < top) top = selected;
        if (selected >= top + 5) top = selected - 4;
        draw_folder_picker(path, count, selected, top);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~old.buttons;
        old = pad;
        UiTouchEvent touch;
        unsigned flags = ui_touch_poll(&touch);
        if (flags & UI_TOUCH_EVENT_TAP) {
            for (int row = 0; row < 5; ++row) {
                if (top + row >= count) break;
                if (ui_touch_hit_rect(touch.x, touch.y, 124, 178 + 47 * row,
                                      712, 42)) {
                    selected = top + row;
                    pressed |= SCE_CTRL_CROSS;
                    break;
                }
            }
            if (ui_touch_hit_rect(touch.x, touch.y, 126, 426, 205, 42))
                pressed |= SCE_CTRL_START;
            if (ui_touch_hit_rect(touch.x, touch.y, 342, 426, 220, 42))
                pressed |= SCE_CTRL_CROSS;
            if (ui_touch_hit_rect(touch.x, touch.y, 573, 426, 261, 42))
                pressed |= SCE_CTRL_CIRCLE;
        }
        unsigned nav = ui_nav_repeat_update(&repeat, pressed, pad.buttons,
                                             pad.lx, pad.ly,
                                             SCE_CTRL_UP | SCE_CTRL_DOWN);
        if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
        if ((nav & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
        if ((pressed & SCE_CTRL_CROSS) && count) {
            snprintf(path, sizeof(path), "%s", picker_folders[selected].path);
            selected = top = 0;
            ui_nav_repeat_reset(&repeat);
            ui_touch_reset();
        }
        if ((pressed & SCE_CTRL_START) && path[0] && directory_exists(path)) {
            if (vt_media_location_normalize(path, out, VT_MEDIA_LOC_PATH_MAX) == 0) {
                ui_touch_reset();
                return 1;
            }
        }
        if (pressed & SCE_CTRL_SELECT) { path[0] = '\0'; selected = top = 0; }
        if (pressed & SCE_CTRL_CIRCLE) {
            if (!path[0]) { ui_touch_reset(); return 0; }
            parent_folder(path);
            selected = top = 0;
        }
        sceKernelDelayThread(16 * 1000);
    }
}

/** @brief Show the type editor for the selected folder. */
static void draw_edit(const VtMediaLocation *draft, int editing, int selected) {
    vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
    vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
    vita2d_start_drawing();
    vita2d_clear_screen();
    draw_base("Media locations", "Change the folder and its media type");
    vita2d_draw_rectangle(0, 54, 960, 490, RGBA8(0, 3, 7, 170));
    ui_panel(148, 102, 664, 338, VT_THEME_SURFACE_RAISED,
             VT_THEME_BLUE_LIGHT, 0);
    text_line(body, UI_FONT_BODY, 184, 144, VT_THEME_TEXT,
              editing ? "Edit location" : "Add location", 580);
    const char *labels[] = { "Folder (Cross to browse)",
                             "Media type (Left / Right)", "Save location", "Cancel" };
    for (int i = 0; i < 4; i++) {
        int y = 157 + i * 66;
        ui_panel(176, y, 608, 59,
                 i == selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
                 VT_THEME_BLUE_LIGHT, 0);
        text_line(small, UI_FONT_SMALL, 194, y + 23, VT_THEME_TEXT_MUTED,
                  labels[i], 565);
        if (i == 0)
            text_line(small, UI_FONT_SMALL, 194, y + 47, VT_THEME_SIGNAL_LIGHT,
                      draft->path[0] ? draft->path : "Choose a folder...", 555);
        else if (i == 1)
            text_line(small, UI_FONT_SMALL, 490, y + 45, VT_THEME_SIGNAL_LIGHT,
                      vt_media_location_type_name(draft->media_mask), 270);
    }
    finish_frame();
}

/** @brief Cycle through video, audio, image, or combined media types. */
static void cycle_type(uint32_t *type, int direction) {
    int selected = 0;
    for (int i = 0; i < (int)(sizeof(type_options) / sizeof(type_options[0])); ++i)
        if (*type == type_options[i]) selected = i;
    int count = sizeof(type_options) / sizeof(type_options[0]);
    *type = type_options[(selected + direction + count) % count];
}

/* Edit dialog with an integrated folder browser. Changes are saved atomically. */
static int edit_location(VtMediaLocations *locations, int position) {
    int editing = position >= 0;
    VtMediaLocation draft;
    memset(&draft, 0, sizeof(draft));
    draft.media_mask = VT_MEDIA_LOC_VIDEO;
    if (editing) draft = locations->locations[position];
    int selected = 0;
    SceCtrlData old, pad;
    sceCtrlPeekBufferPositive(0, &old, 1);
    UiNavRepeat repeat;
    ui_nav_repeat_reset(&repeat);
    ui_touch_reset();
    for (;;) {
        if (sss_livearea_exit_requested()) {
            ui_touch_reset();
            return 0;
        }
        draw_edit(&draft, editing, selected);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~old.buttons;
        old = pad;
        UiTouchEvent touch;
        unsigned flags = ui_touch_poll(&touch);
        if (flags & UI_TOUCH_EVENT_TAP) {
            for (int row = 0; row < 4; ++row)
                if (ui_touch_hit_rect(touch.x, touch.y, 176, 157 + row * 66,
                                      608, 59)) {
                    selected = row;
                    pressed |= SCE_CTRL_CROSS;
                    break;
                }
        }
        unsigned nav = ui_nav_repeat_update(&repeat, pressed, pad.buttons,
                    pad.lx, pad.ly, SCE_CTRL_UP | SCE_CTRL_DOWN |
                    SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
        if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
        if ((nav & SCE_CTRL_DOWN) && selected < 3) selected++;
        if (selected == 1 && (nav & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)))
            cycle_type(&draft.media_mask, (nav & SCE_CTRL_LEFT) ? -1 : 1);
        if (pressed & SCE_CTRL_CIRCLE ||
            ((pressed & SCE_CTRL_CROSS) && selected == 3)) {
            ui_touch_reset();
            return 0;
        }
        if ((pressed & SCE_CTRL_CROSS) && selected == 0) {
            char chosen[VT_MEDIA_LOC_PATH_MAX];
            if (pick_folder(draft.path, chosen))
                snprintf(draft.path, sizeof(draft.path), "%s", chosen);
            sceCtrlPeekBufferPositive(0, &old, 1);
            ui_nav_repeat_reset(&repeat);
            ui_touch_reset();
        }
        if ((pressed & SCE_CTRL_CROSS) && selected == 1)
            cycle_type(&draft.media_mask, 1);
        if ((pressed & SCE_CTRL_START) ||
            ((pressed & SCE_CTRL_CROSS) && selected == 2)) {
            if (!directory_exists(draft.path)) {
                ui_message_show("Choose an existing folder", "", 2300);
            } else {
                VtMediaLocations updated = *locations;
                if (editing) updated.locations[position] = draft;
                else if (updated.count < VT_MEDIA_LOC_MAX)
                    updated.locations[updated.count++] = draft;
                else {
                    ui_message_show("Folder list is full", "", 2300);
                    sceKernelDelayThread(16 * 1000);
                    continue;
                }
                int valid = vt_media_locations_validate(&updated);
                if (valid < 0) {
                    ui_message_show(valid == -2 ?
                        "Overlapping folder and media type" :
                        "Invalid folder", "", 2500);
                } else if (vt_media_locations_save(&updated) < 0) {
                    ui_message_show("Could not save locations", "", 2500);
                } else {
                    *locations = updated;
                    ui_touch_reset();
                    return 1;
                }
            }
        }
        sceKernelDelayThread(16 * 1000);
    }
}

/** @brief Save user choices when closing the folder manager. */
int ui_media_locations_screen(void) {
    VtMediaLocations locations;
    vt_media_locations_load(&locations);
    int selected = 0, top = 0, changed = 0, confirm = 0;
    SceCtrlData old, pad;
    sceCtrlPeekBufferPositive(0, &old, 1);
    UiNavRepeat repeat;
    ui_nav_repeat_reset(&repeat);
    ui_touch_reset();
    for (;;) {
        if (sss_livearea_exit_requested()) {
            ui_touch_reset();
            return changed;
        }
        if (selected >= (int)locations.count)
            selected = locations.count ? (int)locations.count - 1 : 0;
        if (selected < top) top = selected;
        if (selected >= top + LOCATION_VISIBLE)
            top = selected - LOCATION_VISIBLE + 1;
        draw_manage(&locations, selected, top, confirm);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~old.buttons;
        old = pad;
        UiTouchEvent touch;
        unsigned flags = ui_touch_poll(&touch);
        if (flags & UI_TOUCH_EVENT_TAP) {
            if (confirm) {
                if (ui_touch_hit_rect(touch.x, touch.y, 206, 296, 238, 50))
                    pressed |= SCE_CTRL_CIRCLE;
                if (ui_touch_hit_rect(touch.x, touch.y, 464, 296, 278, 50))
                    pressed |= SCE_CTRL_CROSS;
            } else {
                for (int row = 0; row < LOCATION_VISIBLE; ++row)
                    if (top + row < (int)locations.count &&
                        ui_touch_hit_rect(touch.x, touch.y, LOCATION_LIST_X,
                                          LOCATION_LIST_Y + row * LOCATION_ROW_H,
                                          824, LOCATION_ROW_H - 4)) {
                        selected = top + row;
                        pressed |= SCE_CTRL_CROSS;
                        break;
                    }
                if (ui_touch_hit_rect(touch.x, touch.y, 246, 458, 140, 42))
                    pressed |= SCE_CTRL_TRIANGLE;
                if (ui_touch_hit_rect(touch.x, touch.y, 396, 458, 142, 42))
                    pressed |= SCE_CTRL_CROSS;
                if (ui_touch_hit_rect(touch.x, touch.y, 548, 458, 154, 42))
                    pressed |= SCE_CTRL_SQUARE;
                if (ui_touch_hit_rect(touch.x, touch.y, 712, 458, 180, 42))
                    pressed |= SCE_CTRL_CIRCLE;
            }
        }
        if (confirm) {
            if (pressed & SCE_CTRL_CIRCLE) confirm = 0;
            else if (pressed & SCE_CTRL_CROSS) {
                VtMediaLocations updated = locations;
                for (uint32_t i = (uint32_t)selected;
                     i + 1 < updated.count; ++i)
                    updated.locations[i] = updated.locations[i + 1];
                updated.count--;
                memset(&updated.locations[updated.count], 0,
                       sizeof(updated.locations[0]));
                if (vt_media_locations_save(&updated) == 0) {
                    locations = updated;
                    changed = 1;
                } else ui_message_show("Could not save locations", "", 2500);
                confirm = 0;
            }
        } else {
            unsigned nav = ui_nav_repeat_update(&repeat, pressed, pad.buttons,
                                      pad.lx, pad.ly,
                                      SCE_CTRL_UP | SCE_CTRL_DOWN);
            if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
            if ((nav & SCE_CTRL_DOWN) && selected + 1 < (int)locations.count)
                selected++;
            if (pressed & SCE_CTRL_CIRCLE) { ui_touch_reset(); return changed; }
            if (pressed & SCE_CTRL_TRIANGLE) {
                if (locations.count >= VT_MEDIA_LOC_MAX)
                    ui_message_show("Folder list is full", "", 2400);
                else if (edit_location(&locations, -1)) {
                    changed = 1;
                    selected = (int)locations.count - 1;
                }
                sceCtrlPeekBufferPositive(0, &old, 1);
                ui_nav_repeat_reset(&repeat);
            }
            else if (locations.count && (pressed & SCE_CTRL_SQUARE)) confirm = 1;
            else if (locations.count && (pressed & SCE_CTRL_CROSS)) {
                if (edit_location(&locations, selected)) changed = 1;
                sceCtrlPeekBufferPositive(0, &old, 1);
                ui_nav_repeat_reset(&repeat);
            }
        }
        sceKernelDelayThread(16 * 1000);
    }
}
