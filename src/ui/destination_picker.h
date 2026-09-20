#ifndef SSSPLAYER_UI_DESTINATION_PICKER_H
#define SSSPLAYER_UI_DESTINATION_PICKER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	UI_DEST_KIND_ANY = 0,
	UI_DEST_KIND_VIDEO,
	UI_DEST_KIND_AUDIO
} UiDestKind;

/* Folder browser for downloads. Shows the same ux0/uma0 mounts as the app
 * home (filtered by kind), then lets you browse subfolders — no path typing.
 * start_path may be NULL. Returns 1 and writes the chosen folder into out,
 * or 0 if cancelled. */
int ui_destination_picker(const char *start_path, char *out, size_t out_size);

/* Same as ui_destination_picker, with mount shortcuts filtered for video or
 * audio downloads. */
int ui_destination_picker_kind(UiDestKind kind, const char *start_path,
                               char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
