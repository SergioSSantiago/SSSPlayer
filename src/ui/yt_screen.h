#ifndef SSSPLAYER_UI_YT_SCREEN_H
#define SSSPLAYER_UI_YT_SCREEN_H

#include "network/yt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_YT_ACTION_BACK 0
#define UI_YT_ACTION_PLAY 1

typedef struct {
	char video_url[YT_URL_MAX];
	char title[YT_TITLE_MAX];
	char author[YT_AUTHOR_MAX];
	char video_id[YT_ID_MAX];
	int quality_height; /* expected height for HUD (0 = unknown) */
} UiYtSelection;

/* Search / browse / download screen. Returns UI_YT_ACTION_PLAY with selection
 * filled when the user wants to watch the progressive stream. */
int ui_yt_screen(UiYtSelection *selection);

#ifdef __cplusplus
}
#endif

#endif
