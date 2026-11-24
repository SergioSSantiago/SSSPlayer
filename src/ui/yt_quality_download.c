/**
 * @file yt_quality_download.c
 * @brief Offer YouTube 360p, 480p, and 720p MP4 downloads when available.
 * Higher qualities may need separate AVC and AAC files remuxed together.
 * This path is experimental: availability, CDN access, and remuxing can fail.
 */

#include "ui/yt_quality_download.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include "common/text_log.h"
#include "i18n/i18n.h"
#include "media/video_file_probe.h"
#include "network/download_manager.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/destination_picker.h"
#include "ui/font.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/theme.h"
#include "livearea_events.h"

#define YT_ANDROID_UA "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip"

typedef struct {
    int height;
    const char *video_url;
    int needs_audio;
} QualityOption;

typedef struct {
    const char *video;
    const char *audio;
    const char *output;
    char detail[160];
} MuxRequest;

/** @brief Return the exact AVC qualities exposed by the player response. */
static int available_options(const YtResolvedMedia *media, QualityOption options[3]) {
    int count = 0;
    const int have_aac = media->audio_url[0] && !strcmp(media->audio_ext, "m4a");
    if (media->video_url[0]) {
        options[count++] = (QualityOption){
            media->progressive_height > 0 ? media->progressive_height : 360,
            media->video_url, 0
        };
    } else if (media->video_360_url[0] && have_aac) {
        options[count++] = (QualityOption){360, media->video_360_url, 1};
    }
    /* Adaptive 480p/720p is experimental. No AAC track means no
     * downloadable, playable file, so hide that quality entirely. */
    if (have_aac && media->video_480_url[0])
        options[count++] = (QualityOption){480, media->video_480_url, 1};
    if (have_aac && media->video_720_url[0])
        options[count++] = (QualityOption){720, media->video_720_url, 1};
    return count;
}

/** @brief Show only downloadable resolutions; -1 means the user cancelled. */
static int choose_quality(const QualityOption *options, int count) {
    SceCtrlData previous = {0};
    int selected = 0;
    sceCtrlPeekBufferPositive(0, &previous, 1);
    for (;;) {
        if (sss_livearea_exit_requested()) return -1;
        vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
        ui_brand_draw_header(NULL);
        ui_panel(80, 92, 800, 62, VT_THEME_SURFACE_RAISED,
                 VT_THEME_BLUE_LIGHT, 0);
        if (body)
            ui_font_draw_text(body, 104, 132, VT_THEME_TEXT, UI_FONT_BODY,
                              vt_i18n_str(VT_STR_YT_QUALITY_TITLE));
        for (int i = 0; i < count; ++i) {
            char label[80];
            int y = 184 + i * 64;
            snprintf(label, sizeof(label), "%dp %s", options[i].height,
                     vt_i18n_str(options[i].needs_audio
                         ? VT_STR_YT_QUALITY_COMBINE : VT_STR_YT_QUALITY_READY));
            ui_panel(80, y, 800, 56,
                     i == selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
                     VT_THEME_BLUE_LIGHT, 0);
            if (i == selected)
                vita2d_draw_rectangle(80, y, 4, 56, VT_THEME_SIGNAL_BRIGHT);
            if (body)
                ui_font_draw_text(body, 108, y + 36, VT_THEME_TEXT, UI_FONT_BODY,
                                  label);
        }
        ui_draw_controller_footer(19,
                                  "[DUD]Select  [X]Download  [O]Back");
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();

        SceCtrlData controls;
        sceCtrlPeekBufferPositive(0, &controls, 1);
        unsigned pressed = controls.buttons & ~previous.buttons;
        previous = controls;
        if (pressed & SCE_CTRL_UP) selected = (selected + count - 1) % count;
        if (pressed & SCE_CTRL_DOWN) selected = (selected + 1) % count;
        if (pressed & SCE_CTRL_CROSS) return selected;
        if (pressed & SCE_CTRL_CIRCLE) return -1;
        sceKernelDelayThread(16000);
    }
}

/** @brief Download a single named stream into an already chosen directory. */
static int download_track(const char *url, const char *folder, const char *name,
                          const char *title, char *path, size_t capacity) {
    VtDownloadJob job;
    vt_download_job_init_url(&job, url);
    vt_download_job_set_destination(&job, folder);
    vt_download_job_set_filename(&job, name);
    vt_download_job_set_user_agent(&job, YT_ANDROID_UA);
    int result = ui_loading_run_download(title, vt_download_run, &job,
                                         &job.paused, &job.cancel,
                                         &job.progress_current, &job.progress_total);
    if (result != 0) {
        if (!job.cancel)
            ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_FAILED),
                            job.detail[0] ? job.detail : "Transfer failed", 3200);
        return -1;
    }
    snprintf(path, capacity, "%s", job.destination);
    return 0;
}

/** @brief Choose a destination name without overwriting existing media. */
static int output_path(const char *folder, const char *base, int height,
                       char *out, size_t capacity, char *temporary,
                       size_t temporary_capacity) {
    SceIoStat existing;
    for (int suffix = 0; suffix < 1000; suffix++) {
        int count;
        if (suffix > 0)
            count = snprintf(out, capacity, "%s/%s_%dp_%d.mp4",
                             folder, base, height, suffix);
        else
            count = snprintf(out, capacity, "%s/%s_%dp.mp4",
                             folder, base, height);
        if (count <= 0 || (size_t)count >= capacity) return -1;
        count = snprintf(temporary, temporary_capacity, "%s.assembling", out);
        if (count <= 0 || (size_t)count >= temporary_capacity) return -1;
        if (sceIoGetstat(out, &existing) < 0 &&
            sceIoGetstat(temporary, &existing) < 0) return 0;
    }
    return -1;
}

/** @brief Run the stream-copy remux on the loading worker thread. */
static int mux_worker(void *opaque) {
    MuxRequest *job = opaque;
    return yt_client_remux_av_mp4(job->video, job->audio, job->output,
                                   job->detail, sizeof(job->detail));
}

/** @brief Verify a completed MP4 has both expected codec tracks. */
static int verify_video(const char *path, int height) {
    VtVideoFileProbe probe = {0};
    int result = vt_video_file_probe(path, &probe);
    log_printf("youtube saved video: ret=%d height=%d expected=%d H264=%d AAC=%d",
               result, probe.height, height, probe.video_is_h264, probe.audio_is_aac);
    return result == 0 && probe.video_is_h264 && probe.has_audio &&
           probe.audio_is_aac && probe.height == height ? 0 : -1;
}

/** @brief Download the selected progressive or adaptive MP4 without re-encoding. */
int ui_yt_quality_download(const YtResolvedMedia *media, const char *basename) {
    QualityOption options[3] = {0};
    char folder[512], video_path[512] = "", audio_path[512] = "";
    char filename[128], final_path[512], temporary[544];
    int count, chosen;
    if (!media || !basename || !basename[0]) return -1;
    count = available_options(media, options);
    log_printf("youtube download qualities: count=%d 360=%d 480=%d 720=%d aac=%d",
               count, media->video_360_url[0] != 0,
               media->video_480_url[0] != 0,
               media->video_720_url[0] != 0, media->audio_url[0] != 0);
    if (count == 0) {
        ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_FAILED),
                        vt_i18n_str(VT_STR_YT_QUALITY_UNAVAILABLE), 3000);
        return -1;
    }
    chosen = choose_quality(options, count);
    if (chosen < 0) return 0;
    const QualityOption *selected = &options[chosen];
    if (!ui_destination_picker_kind(UI_DEST_KIND_VIDEO, NULL, folder, sizeof(folder)))
        return 0;

    if (!selected->needs_audio) {
        snprintf(filename, sizeof(filename), "%s_%dp.mp4", basename,
                 selected->height);
        if (download_track(selected->video_url, folder, filename,
                           vt_i18n_str(VT_STR_NETWORK_DOWNLOADING),
                           video_path, sizeof(video_path)) < 0) return -1;
        if (verify_video(video_path, selected->height) < 0) {
            ui_message_show(vt_i18n_str(VT_STR_YT_QUALITY_VERIFY_FAILED),
                            video_path, 4200);
            sceIoRemove(video_path);
            return -1;
        }
        ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_COMPLETE),
                        video_path, 3200);
        return 0;
    }

    /* The adaptive stream contains no sound. Never publish its temporary
     * video-only file as a successful offline download. */
    if (output_path(folder, basename, selected->height, final_path,
                    sizeof(final_path), temporary, sizeof(temporary)) < 0) {
        ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_FAILED),
                        vt_i18n_str(VT_STR_YT_QUALITY_PATH_FAILED), 3200);
        return -1;
    }
    snprintf(filename, sizeof(filename), "%s_%dp.yt_video.mp4", basename,
             selected->height);
    if (download_track(selected->video_url, folder, filename,
                       vt_i18n_str(VT_STR_YT_QUALITY_DOWNLOADING_VIDEO),
                       video_path, sizeof(video_path)) < 0) return -1;
    if (!yt_client_file_has_h264(video_path)) {
        ui_message_show(vt_i18n_str(VT_STR_YT_QUALITY_VERIFY_FAILED),
                        vt_i18n_str(VT_STR_YT_QUALITY_BAD_VIDEO), 3200);
        sceIoRemove(video_path);
        return -1;
    }
    snprintf(filename, sizeof(filename), "%s_%dp.yt_audio.m4a", basename,
             selected->height);
    if (download_track(media->audio_url, folder, filename,
                       vt_i18n_str(VT_STR_YT_QUALITY_DOWNLOADING_AUDIO),
                       audio_path, sizeof(audio_path)) < 0) {
        sceIoRemove(video_path);
        return -1;
    }

    MuxRequest request = {video_path, audio_path, temporary, {0}};
    int result = ui_loading_run(vt_i18n_str(VT_STR_YT_QUALITY_COMBINING),
                                mux_worker, &request, NULL, NULL, NULL);
    if (result == 0) result = verify_video(temporary, selected->height);
    if (result == 0) result = sceIoRename(temporary, final_path);
    if (result != 0) {
        sceIoRemove(temporary);
        ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_FAILED),
                        request.detail[0] ? request.detail :
                        vt_i18n_str(VT_STR_YT_QUALITY_VERIFY_FAILED), 4000);
    } else {
        ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_COMPLETE),
                        final_path, 3400);
    }
    sceIoRemove(video_path);
    sceIoRemove(audio_path);
    return result == 0 ? 0 : -1;
}
