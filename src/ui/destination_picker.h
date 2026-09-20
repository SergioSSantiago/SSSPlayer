#ifndef SSSPLAYER_UI_DESTINATION_PICKER_H
#define SSSPLAYER_UI_DESTINATION_PICKER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Folder browser for downloads. start_path may be NULL (defaults to ux0:download).
 * Returns 1 and writes the chosen folder into out, or 0 if cancelled. */
int ui_destination_picker(const char *start_path, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
