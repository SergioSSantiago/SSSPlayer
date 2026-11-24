/**
 * @file video_file_probe.c
 * @brief Inspect actual container tracks rather than trusting file extensions.
 */

#include "media/video_file_probe.h"

#include <stdio.h>
#include <string.h>

#include <psp2/io/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>

/** Describe an FFmpeg demux error without leaking the requested filename. */
static void probe_error(VtVideoFileProbe *result, const char *stage, int code)
{
    char description[AV_ERROR_MAX_STRING_SIZE];
    if (av_strerror(code, description, sizeof(description)) < 0)
        snprintf(description, sizeof(description), "error %d", code);
    snprintf(result->error, sizeof(result->error), "%s: %.88s", stage,
             description);
}

/**
 * Inspect a downloaded or local video with the same demuxer used by playback.
 * A successful probe means the track metadata is readable, not that all frames
 * or every Vita hardware codec profile are supported.
 */
int vt_video_file_probe(const char *path, VtVideoFileProbe *result)
{
    AVFormatContext *input = NULL;
    SceIoStat stat;
    int status;
    unsigned int index;

    if (!result) return -1;
    memset(result, 0, sizeof(*result));
    snprintf(result->video_codec, sizeof(result->video_codec), "none");
    snprintf(result->audio_codec, sizeof(result->audio_codec), "none");
    if (!path || !path[0]) {
        snprintf(result->error, sizeof(result->error), "Missing file path");
        return -1;
    }

    memset(&stat, 0, sizeof(stat));
    if (sceIoGetstat(path, &stat) < 0 || stat.st_size <= 0) {
        snprintf(result->error, sizeof(result->error),
                 "File is missing or empty");
        return -1;
    }
    result->file_bytes = (int64_t)stat.st_size;

    status = avformat_open_input(&input, path, NULL, NULL);
    if (status < 0) {
        probe_error(result, "Cannot open container", status);
        return -1;
    }
    if (input->iformat && input->iformat->name)
        snprintf(result->container, sizeof(result->container), "%s",
                 input->iformat->name);

    status = avformat_find_stream_info(input, NULL);
    if (status < 0) {
        probe_error(result, "Cannot inspect tracks", status);
        avformat_close_input(&input);
        return -1;
    }

    for (index = 0; index < input->nb_streams; index++) {
        const AVCodecParameters *track = input->streams[index]->codecpar;
        if (!result->has_video && track->codec_type == AVMEDIA_TYPE_VIDEO) {
            result->has_video = 1;
            result->video_is_h264 = track->codec_id == AV_CODEC_ID_H264;
            result->width = track->width;
            result->height = track->height;
            result->video_profile = track->profile;
            result->video_level = track->level;
            result->field_order = track->field_order;
            snprintf(result->video_codec, sizeof(result->video_codec), "%s",
                     avcodec_get_name(track->codec_id));
        } else if (!result->has_audio &&
                   track->codec_type == AVMEDIA_TYPE_AUDIO) {
            result->has_audio = 1;
            result->audio_is_aac = track->codec_id == AV_CODEC_ID_AAC;
            snprintf(result->audio_codec, sizeof(result->audio_codec), "%s",
                     avcodec_get_name(track->codec_id));
        }
    }
    avformat_close_input(&input);
    if (!result->has_video) {
        snprintf(result->error, sizeof(result->error),
                 "No video track in the file");
        return -1;
    }
    return 0;
}
