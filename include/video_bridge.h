#ifndef SSSPLAYER_VIDEO_BRIDGE_H
#define SSSPLAYER_VIDEO_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if the path looks like a playable video container. */
int sss_video_is_path(const char *path);

/* Play a local video file with the SSSPlayer decoder stack.
 * Returns 0 on clean exit, <0 on failure. Music audio should be stopped
 * by the caller before invoking this. */
int sss_video_play_local(const char *path, const char *title);

/* Open the local media browser (videos/images). */
int sss_video_browse_library(void);

/* Open the network sources browser (SMB/SFTP/WebDAV/Jellyfin). */
int sss_video_browse_network(void);

/* Open YouTube search / play / download. */
int sss_video_browse_youtube(void);

/* Tear down video/network workers before process exit (must run before
 * vita2d_fini). Safe to call if video was never opened. */
void sss_video_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
