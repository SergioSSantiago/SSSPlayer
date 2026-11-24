#ifndef SSSPLAYER_MEDIA_VIDEO_FILE_PROBE_H
#define SSSPLAYER_MEDIA_VIDEO_FILE_PROBE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Summary of a local video's container and its first audio/video tracks.
 * This describes the file. It does not promise that Vita hardware can decode it.
 */
typedef struct VtVideoFileProbe {
    int64_t file_bytes;
    int has_video;
    int has_audio;
    int video_is_h264;
    int audio_is_aac;
    int width;
    int height;
    int video_profile;
    int video_level;
    int field_order;
    char container[32];
    char video_codec[32];
    char audio_codec[32];
    char error[128];
} VtVideoFileProbe;

/**
 * Inspect a local file with FFmpeg's demuxer, without decoding any frames.
 * Returns zero when the container can be read and a video track is present.
 * The caller owns the result and may display error on a negative return.
 */
int vt_video_file_probe(const char *path, VtVideoFileProbe *result);

#endif /* SSSPLAYER_MEDIA_VIDEO_FILE_PROBE_H */
