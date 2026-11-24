/**
 * @file yt_favorites_screen.c
 * @brief Refresh saved YouTube channels and videos without losing local favorites.
 */

#include "ui/yt_favorites_screen.h"
#include "ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "livearea_events.h"
#include "network/yt_client.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/font.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define FAV_ROW_X 52
#define FAV_ROW_Y 155
#define FAV_ROW_W 856
#define FAV_ROW_H 52
#define FAV_VISIBLE 6
#define REFRESH_BATCH_SIZE 7
#define REFRESH_BATCH_DELAY_US 3000000

typedef struct {
    YtFavorites *favorites;
    volatile int cancel;
    volatile long progress;
    long total;
    int changed;
} ChannelRefreshJob;

typedef struct {
    int order[YT_FAVORITES_LIMIT];
    int count;
    int selected;
    int top;
    int sort;
    char filter[96];
} FavoritesListView;

/** Case-insensitive substring search for a compact on-screen title filter. */
static int contains_text(const char *text, const char *needle)
{
    const char *a, *b;
    if (!needle || !needle[0]) return 1;
    if (!text) return 0;
    for (; *text; text++) {
        a = text;
        b = needle;
        while (*a && *b && tolower((unsigned char)*a) ==
                            tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

/** Compare two favorite records according to the active view's sorting mode. */
static int compare_favorites(const YtFavorites *favorites, UiYtFavoritesKind kind,
                             int sort, int first, int second)
{
    if (kind == UI_YT_FAVORITES_CHANNELS) {
        const YtFavoriteChannel *a = &favorites->channels[first];
        const YtFavoriteChannel *b = &favorites->channels[second];
        int date = strcmp(b->latest_date, a->latest_date);
        if (sort == 1) return strcmp(a->name, b->name);
        return date ? date : strcmp(a->name, b->name);
    }
    const YtSearchResult *a = &favorites->videos[first];
    const YtSearchResult *b = &favorites->videos[second];
    if (sort == 1) return strcmp(a->uploaded_date, b->uploaded_date);
    if (sort == 2) return strcmp(a->title, b->title);
    if (sort == 3) return strcmp(a->author, b->author);
    if (sort == 4) return a->length_seconds == b->length_seconds ? 0
                             : (b->length_seconds > a->length_seconds ? 1 : -1);
    int date = strcmp(b->uploaded_date, a->uploaded_date);
    return date ? date : strcmp(a->title, b->title);
}

/** Rebuild filtered indices without changing the on-disk favorites order. */
static void rebuild_view(FavoritesListView *view, const YtFavorites *favorites,
                          UiYtFavoritesKind kind)
{
    int total = kind == UI_YT_FAVORITES_CHANNELS
              ? favorites->channel_count : favorites->video_count;
    view->count = 0;
    for (int i = 0; i < total; i++) {
        const char *name = kind == UI_YT_FAVORITES_CHANNELS
                         ? favorites->channels[i].name : favorites->videos[i].title;
        const char *author = kind == UI_YT_FAVORITES_CHANNELS
                           ? "" : favorites->videos[i].author;
        if (contains_text(name, view->filter) || contains_text(author, view->filter))
            view->order[view->count++] = i;
    }
    /* Stable insertion sort is ample for the bounded 128-item collection. */
    for (int i = 1; i < view->count; i++) {
        int value = view->order[i];
        int j = i;
        while (j > 0 && compare_favorites(favorites, kind, view->sort,
                                         value, view->order[j - 1]) < 0) {
            view->order[j] = view->order[j - 1];
            j--;
        }
        view->order[j] = value;
    }
    if (view->selected >= view->count) view->selected = view->count - 1;
    if (view->selected < 0) view->selected = 0;
    if (view->top > view->selected) view->top = view->selected;
    if (view->top < view->selected - FAV_VISIBLE + 1)
        view->top = view->selected - FAV_VISIBLE + 1;
}

/** Check saved channels seven at a time, cooling down for 3s between batches. */
static int refresh_channel_worker(void *opaque)
{
    ChannelRefreshJob *job = opaque;
    for (int i = 0; i < job->favorites->channel_count; i++) {
        YtFavoriteChannel *channel = &job->favorites->channels[i];
        YtSearchResult latest;
        char detail[160];
        int status;
        if (job->cancel || sss_livearea_exit_requested()) return -1;
        status = yt_client_channel_latest(channel->id, &latest,
                                          detail, sizeof(detail));
        if (status > 0) {
            if (latest.uploaded_date[0])
                snprintf(channel->latest_date, sizeof(channel->latest_date),
                         "%s", latest.uploaded_date);
            snprintf(channel->latest_video_id, sizeof(channel->latest_video_id),
                     "%s", latest.id);
            job->changed = 1;
        }
        job->progress = i + 1;
        if ((i + 1) % REFRESH_BATCH_SIZE == 0 &&
            i + 1 < job->favorites->channel_count) {
            /* Keep this cancellable; a long uninterruptible sleep can delay exit. */
            for (int pause = 0; pause < REFRESH_BATCH_DELAY_US / 100000; pause++) {
                if (job->cancel || sss_livearea_exit_requested()) return -1;
                sceKernelDelayThread(100000);
            }
        }
    }
    return 0;
}

/** Update cached last-upload dates, preserving cached values on network errors. */
static void refresh_channels(YtFavorites *favorites)
{
    ChannelRefreshJob job = {0};
    if (favorites->channel_count == 0) return;
    job.favorites = favorites;
    job.total = favorites->channel_count;
    ui_loading_run("Refreshing channels (7 per batch)", refresh_channel_worker,
                   &job, &job.cancel, &job.progress, &job.total);
    if (job.changed && yt_favorites_save(favorites) != 0)
        ui_message_show("Favorites not saved", "Check available storage", 2400);
    ui_touch_reset();
}

/** Render an individual row, with its saved metadata beneath the title. */
static void draw_favorite_row(const YtFavorites *favorites, UiYtFavoritesKind kind,
                              int index, int y, int selected)
{
    vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
    vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
    char title[YT_TITLE_MAX];
    char metadata[160];
    ui_panel(FAV_ROW_X, y, FAV_ROW_W, FAV_ROW_H - 4,
             selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
             VT_THEME_BLUE_LIGHT, 0);
    if (selected)
        vita2d_draw_rectangle(FAV_ROW_X, y, 4, FAV_ROW_H - 4,
                              VT_THEME_SIGNAL_BRIGHT);
    if (kind == UI_YT_FAVORITES_CHANNELS) {
        const YtFavoriteChannel *item = &favorites->channels[index];
        snprintf(title, sizeof(title), "%s", item->name[0] ? item->name : item->id);
        snprintf(metadata, sizeof(metadata), "Latest upload: %s",
                 item->latest_date[0] ? item->latest_date : "Not available");
    } else {
        const YtSearchResult *item = &favorites->videos[index];
        snprintf(title, sizeof(title), "%s", item->title);
        snprintf(metadata, sizeof(metadata), "%s  |  %s%s  |  %d:%02d",
                 item->author,
                 item->published_text[0] && item->uploaded_date[0] ? "~" : "",
                 item->uploaded_date[0] ? item->uploaded_date : "Date unknown",
                 item->length_seconds / 60, item->length_seconds % 60);
    }
    if (body) {
        char fit[YT_TITLE_MAX];
        ui_font_fit_text(body, UI_FONT_BODY, title, fit, sizeof(fit), FAV_ROW_W - 34);
        ui_font_draw_text(body, FAV_ROW_X + 18, y + 25,
                          VT_THEME_TEXT, UI_FONT_BODY, fit);
    }
    if (small)
        ui_font_draw_text(small, FAV_ROW_X + 18, y + 44,
                          VT_THEME_TEXT_DIM, UI_FONT_SMALL, metadata);
}

/** Draw the favorites collection with the YouTube screen's action-button band. */
static void draw_favorites(const YtFavorites *favorites, UiYtFavoritesKind kind,
                           const FavoritesListView *view)
{
    vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
    vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
    const char *label = kind == UI_YT_FAVORITES_CHANNELS
                      ? "Favorite channels" : "Favorite videos";
    const char *sort_labels[] = {"Newest", "Oldest", "Title", "Channel", "Length"};
    const char *sort = kind == UI_YT_FAVORITES_CHANNELS
                     ? (view->sort == 1 ? "Name" : "Newest")
                     : sort_labels[view->sort];
    char header[160];
    vita2d_start_drawing();
    vita2d_clear_screen();
    ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
    ui_brand_draw_header("YouTube");
    ui_scene_identity(52, 68, 856, "YT/FAV", label,
                      view->filter[0] ? view->filter : "Saved on this Vita");
    snprintf(header, sizeof(header), "%d visible  |  Sort: %s  |  Select clears filter", view->count, sort);
    ui_panel(FAV_ROW_X, 110, FAV_ROW_W, 34, VT_THEME_SURFACE_RAISED,
             VT_THEME_BLUE_LIGHT, 0);
    if (small)
        ui_font_draw_text(small, FAV_ROW_X + 14, 132,
                          VT_THEME_SIGNAL_LIGHT, UI_FONT_SMALL, header);
    if (view->count == 0) {
        ui_panel(FAV_ROW_X, FAV_ROW_Y, FAV_ROW_W, 190,
                 VT_THEME_SURFACE, VT_THEME_BLUE_LIGHT, 0);
        if (body)
            ui_font_draw_text(body, FAV_ROW_X + 24, FAV_ROW_Y + 90,
                              VT_THEME_TEXT, UI_FONT_BODY,
                              view->filter[0] ? "No matching favorites" : "No favorites saved yet");
    }
    for (int i = view->top; i < view->count && i < view->top + FAV_VISIBLE; i++)
        draw_favorite_row(favorites, kind, view->order[i],
                          FAV_ROW_Y + (i - view->top) * FAV_ROW_H,
                          i == view->selected);
    if (view->count > 0) {
        ui_draw_controller_footer(19, kind == UI_YT_FAVORITES_CHANNELS
            ? "[S]Filter  [X]Channel  [T]Sort  R:Remove  [STA]Sync  [O]Back"
            : "[S]Filter  [X]Open  [T]Sort  R:Remove  [O]Back");
    } else {
        ui_draw_controller_footer(19, kind == UI_YT_FAVORITES_CHANNELS &&
                                         favorites->channel_count > 0
            ? "[S]Filter  [STA]Sync  [O]Back"
            : "[S]Filter  [O]Back");
    }
    vita2d_end_drawing();
    vita2d_wait_rendering_done();
    vita2d_swap_buffers();
}

/** Ask for confirmation before removing one saved item. */
static int confirm_remove(void)
{
    SceCtrlData previous = {0};
    sceCtrlPeekBufferPositive(0, &previous, 1);
    for (;;) {
        SceCtrlData controls;
        unsigned pressed;
        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
        ui_brand_draw_header("YouTube favorites");
        ui_scene_identity(90, 160, 780, "YT/DEL", "Remove this favorite?",
                          "This does not delete downloaded media.");
        ui_draw_controller_footer(19, "[X]Remove  [O]Cancel");
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
        if (sss_livearea_exit_requested()) return 0;
        sceCtrlPeekBufferPositive(0, &controls, 1);
        pressed = controls.buttons & ~previous.buttons;
        previous = controls;
        if (pressed & SCE_CTRL_CROSS) return 1;
        if (pressed & SCE_CTRL_CIRCLE) return 0;
        sceKernelDelayThread(16000);
    }
}

/** Display saved videos or channels and return a picked item to YouTube. */
int ui_yt_favorites_screen(YtFavorites *favorites, UiYtFavoritesKind kind,
                           YtSearchResult *video, YtFavoriteChannel *channel)
{
    FavoritesListView view = {0};
    SceCtrlData previous = {0};
    UiNavRepeat repeat;
    if (!favorites) return 0;
    if (kind == UI_YT_FAVORITES_CHANNELS) refresh_channels(favorites);
    rebuild_view(&view, favorites, kind);
    sceCtrlPeekBufferPositive(0, &previous, 1);
    ui_nav_repeat_reset(&repeat);
    for (;;) {
        SceCtrlData ctrl;
        unsigned pressed, nav;
        if (sss_livearea_exit_requested()) return 0;
        draw_favorites(favorites, kind, &view);
        sceCtrlPeekBufferPositive(0, &ctrl, 1);
        pressed = ctrl.buttons & ~previous.buttons;
        previous = ctrl;
        nav = ui_nav_repeat_update(&repeat, pressed, ctrl.buttons,
                                   ctrl.lx, ctrl.ly,
                                   SCE_CTRL_UP | SCE_CTRL_DOWN);
        if ((nav & SCE_CTRL_UP) && view.selected > 0) view.selected--;
        if ((nav & SCE_CTRL_DOWN) && view.selected + 1 < view.count)
            view.selected++;
        if (view.selected < view.top) view.top = view.selected;
        if (view.selected >= view.top + FAV_VISIBLE)
            view.top = view.selected - FAV_VISIBLE + 1;
        if (pressed & SCE_CTRL_SQUARE) {
            char filter[sizeof(view.filter)];
            snprintf(filter, sizeof(filter), "%s", view.filter);
            if (ui_text_input("Filter favorites", filter, filter,
                              sizeof(filter)) > 0) {
                snprintf(view.filter, sizeof(view.filter), "%s", filter);
                view.selected = view.top = 0;
                rebuild_view(&view, favorites, kind);
            }
        }
        if (pressed & SCE_CTRL_TRIANGLE) {
            if (kind == UI_YT_FAVORITES_CHANNELS) {
                view.sort = (view.sort + 1) % 2;
            } else {
                view.sort = (view.sort + 1) % 5;
            }
            view.selected = view.top = 0;
            rebuild_view(&view, favorites, kind);
        }
        if (pressed & SCE_CTRL_SELECT) {
            view.filter[0] = '\0';
            view.selected = view.top = 0;
            rebuild_view(&view, favorites, kind);
        }
        if ((pressed & SCE_CTRL_START) &&
            kind == UI_YT_FAVORITES_CHANNELS) {
            refresh_channels(favorites);
            view.selected = view.top = 0;
            rebuild_view(&view, favorites, kind);
        }
        if ((pressed & SCE_CTRL_RTRIGGER) && view.count > 0) {
            int index = view.order[view.selected];
            if (confirm_remove()) {
                int removed = kind == UI_YT_FAVORITES_CHANNELS
                    ? yt_favorites_remove_channel(favorites, favorites->channels[index].id)
                    : yt_favorites_remove_video(favorites, favorites->videos[index].id);
                if (removed > 0 && yt_favorites_save(favorites) < 0)
                    ui_message_show("Favorites not saved", "Check storage", 2600);
                rebuild_view(&view, favorites, kind);
            }
        }
        if ((pressed & SCE_CTRL_CROSS) && view.count > 0) {
            int index = view.order[view.selected];
            if (kind == UI_YT_FAVORITES_CHANNELS && channel)
                *channel = favorites->channels[index];
            else if (kind == UI_YT_FAVORITES_VIDEOS && video)
                *video = favorites->videos[index];
            return 1;
        }
        if (pressed & SCE_CTRL_CIRCLE) return 0;
        if (pressed & (SCE_CTRL_SQUARE | SCE_CTRL_RTRIGGER | SCE_CTRL_TRIANGLE |
                       SCE_CTRL_START | SCE_CTRL_SELECT)) {
            sceCtrlPeekBufferPositive(0, &previous, 1);
            ui_nav_repeat_reset(&repeat);
            ui_touch_reset();
        }
        sceKernelDelayThread(16000);
    }
}
