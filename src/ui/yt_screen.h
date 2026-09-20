#ifndef SSSPLAYER_UI_YT_SCREEN_H
#define SSSPLAYER_UI_YT_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#define UI_YT_ACTION_BACK 0
#define UI_YT_ACTION_PLAY 1

typedef struct {
	char video_url[2048];
	char title[160];
	char author[96];
	char video_id[24];
} UiYtSelection;

/* Search / browse / download screen. Returns UI_YT_ACTION_PLAY with selection
 * filled when the user wants to watch a resolved progressive stream. */
int ui_yt_screen(UiYtSelection *selection);

#ifdef __cplusplus
}
#endif

#endif
