/**
 * @file yt_favorites.c
 * @brief Save YouTube videos and channels with stable IDs and upload dates.
 */

#include "network/yt_favorites.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "app_paths.h"

#define FAVORITES_PATH VITAMEDIADECK_DATA_DIR "/youtube_favorites.json"
#define FAVORITES_TEMP VITAMEDIADECK_DATA_DIR "/youtube_favorites.tmp"
#define FAVORITES_BACKUP VITAMEDIADECK_DATA_DIR "/youtube_favorites.bak"
#define FAVORITES_JSON_LIMIT (256 * 1024)

/** Return true if a persisted file exists. */
static int exists(const char *path)
{
    SceIoStat stat;
    return sceIoGetstat(path, &stat) >= 0;
}

/** Copy a JSON string into a bounded destination. */
static void read_string(json_t *obj, const char *key, char *out, size_t size)
{
    const char *value = json_string_value(json_object_get(obj, key));
    if (value && size) snprintf(out, size, "%s", value);
}

/** Read the complete JSON file while rejecting unexpected file sizes. */
static json_t *read_database(const char *path)
{
    SceIoStat stat;
    SceUID fd;
    char *buffer;
    size_t total = 0;
    json_error_t error;
    json_t *root;
    if (sceIoGetstat(path, &stat) < 0 || stat.st_size <= 0 ||
        stat.st_size > FAVORITES_JSON_LIMIT) return NULL;
    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return NULL;
    buffer = malloc((size_t)stat.st_size + 1);
    if (!buffer) { sceIoClose(fd); return NULL; }
    while (total < (size_t)stat.st_size) {
        int bytes = sceIoRead(fd, buffer + total, (size_t)stat.st_size - total);
        if (bytes <= 0) break;
        total += (size_t)bytes;
    }
    sceIoClose(fd);
    buffer[total] = '\0';
    root = total == (size_t)stat.st_size
         ? json_loadb(buffer, total, 0, &error) : NULL;
    free(buffer);
    return root;
}

/** Decode saved records, leaving out malformed or duplicate entries. */
static int parse_database(YtFavorites *favorites, json_t *root)
{
    json_t *videos, *channels;
    size_t i;
    if (!json_is_object(root) ||
        json_integer_value(json_object_get(root, "version")) != 1) return -1;
    videos = json_object_get(root, "videos");
    channels = json_object_get(root, "channels");
    if (!json_is_array(videos) || !json_is_array(channels)) return -1;
    for (i = 0; i < json_array_size(videos); i++) {
        YtSearchResult video = {0};
        json_t *row = json_array_get(videos, i);
        read_string(row, "id", video.id, sizeof(video.id));
        read_string(row, "title", video.title, sizeof(video.title));
        read_string(row, "channel", video.author, sizeof(video.author));
        read_string(row, "channel_id", video.channel_id, sizeof(video.channel_id));
        read_string(row, "uploaded", video.uploaded_date, sizeof(video.uploaded_date));
        read_string(row, "published_text", video.published_text,
                    sizeof(video.published_text));
        video.length_seconds = (int)json_integer_value(json_object_get(row, "length"));
        yt_favorites_add_video(favorites, &video);
    }
    for (i = 0; i < json_array_size(channels); i++) {
        YtFavoriteChannel channel = {0};
        json_t *row = json_array_get(channels, i);
        read_string(row, "id", channel.id, sizeof(channel.id));
        read_string(row, "name", channel.name, sizeof(channel.name));
        read_string(row, "latest_date", channel.latest_date,
                    sizeof(channel.latest_date));
        read_string(row, "latest_video_id", channel.latest_video_id,
                    sizeof(channel.latest_video_id));
        yt_favorites_add_channel(favorites, &channel);
    }
    return 0;
}

/** Load favorites, recovering the previous snapshot if the primary was lost. */
int yt_favorites_load(YtFavorites *favorites)
{
    json_t *root;
    int status;
    if (!favorites) return -1;
    memset(favorites, 0, sizeof(*favorites));
    root = read_database(FAVORITES_PATH);
    if (!root) root = read_database(FAVORITES_BACKUP);
    if (!root) return exists(FAVORITES_PATH) ? -1 : 0;
    status = parse_database(favorites, root);
    json_decref(root);
    if (status < 0) memset(favorites, 0, sizeof(*favorites));
    return status;
}

/** Save all favorites, preserving the previous complete file on failure. */
int yt_favorites_save(const YtFavorites *favorites)
{
    json_t *root, *videos, *channels;
    char *text;
    size_t length, offset = 0;
    SceUID fd;
    int had_previous;
    int result = -1;
    if (!favorites) return -1;
    root = json_object();
    videos = json_array();
    channels = json_array();
    if (!root || !videos || !channels) goto fail_json;
    for (int i = 0; i < favorites->video_count; i++) {
        const YtSearchResult *v = &favorites->videos[i];
        json_t *row = json_pack("{s:s,s:s,s:s,s:s,s:s,s:s,s:i}",
            "id", v->id, "title", v->title, "channel", v->author,
            "channel_id", v->channel_id, "uploaded", v->uploaded_date,
            "published_text", v->published_text, "length", v->length_seconds);
        if (!row || json_array_append_new(videos, row) < 0) goto fail_json;
    }
    for (int i = 0; i < favorites->channel_count; i++) {
        const YtFavoriteChannel *c = &favorites->channels[i];
        json_t *row = json_pack("{s:s,s:s,s:s,s:s}", "id", c->id,
            "name", c->name, "latest_date", c->latest_date,
            "latest_video_id", c->latest_video_id);
        if (!row || json_array_append_new(channels, row) < 0) goto fail_json;
    }
    json_object_set_new(root, "version", json_integer(1));
    json_object_set_new(root, "videos", videos); videos = NULL;
    json_object_set_new(root, "channels", channels); channels = NULL;
    text = json_dumps(root, JSON_COMPACT | JSON_ENSURE_ASCII);
    json_decref(root);
    if (!text) return -1;
    length = strlen(text);
    if (length > FAVORITES_JSON_LIMIT) { free(text); return -1; }
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(VITAMEDIADECK_DATA_DIR, 0777);
    fd = sceIoOpen(FAVORITES_TEMP, SCE_O_CREAT | SCE_O_WRONLY | SCE_O_TRUNC, 0777);
    if (fd < 0) { free(text); return -1; }
    while (offset < length) {
        int written = sceIoWrite(fd, text + offset, length - offset);
        if (written <= 0) break;
        offset += (size_t)written;
    }
    if (offset == length && sceIoSyncByFd(fd, 0) >= 0) result = 0;
    sceIoClose(fd);
    free(text);
    if (result < 0) { sceIoRemove(FAVORITES_TEMP); return -1; }
    had_previous = exists(FAVORITES_PATH);
    sceIoRemove(FAVORITES_BACKUP);
    if (had_previous && sceIoRename(FAVORITES_PATH, FAVORITES_BACKUP) < 0) {
        sceIoRemove(FAVORITES_TEMP);
        return -1;
    }
    if (sceIoRename(FAVORITES_TEMP, FAVORITES_PATH) < 0) {
        if (had_previous) sceIoRename(FAVORITES_BACKUP, FAVORITES_PATH);
        sceIoRemove(FAVORITES_TEMP);
        return -1;
    }
    return 0;
fail_json:
    if (channels) json_decref(channels);
    if (videos) json_decref(videos);
    if (root) json_decref(root);
    return -1;
}

/** Insert one video without duplicating its stable ID. */
int yt_favorites_add_video(YtFavorites *favorites, const YtSearchResult *video)
{
    if (!favorites || !video || strlen(video->id) != 11) return -1;
    for (int i = 0; i < favorites->video_count; i++) {
        if (strcmp(favorites->videos[i].id, video->id) == 0) {
            YtSearchResult updated = *video;
            /* A retry without metadata must not erase a verified upload date. */
            if ((!updated.uploaded_date[0] || updated.published_text[0]) &&
                favorites->videos[i].uploaded_date[0] &&
                !favorites->videos[i].published_text[0]) {
                snprintf(updated.uploaded_date, sizeof(updated.uploaded_date),
                         "%s", favorites->videos[i].uploaded_date);
                updated.published_text[0] = '\0';
            }
            favorites->videos[i] = updated;
            return 0;
        }
    }
    if (favorites->video_count == YT_FAVORITES_LIMIT) return -1;
    favorites->videos[favorites->video_count++] = *video;
    return 1;
}

/** Insert one channel without duplicating its stable UC ID. */
int yt_favorites_add_channel(YtFavorites *favorites, const YtFavoriteChannel *channel)
{
    if (!favorites || !channel || strncmp(channel->id, "UC", 2) != 0)
        return -1;
    for (int i = 0; i < favorites->channel_count; i++) {
        if (strcmp(favorites->channels[i].id, channel->id) == 0) {
            YtFavoriteChannel updated = *channel;
            /* Retain last known upload information if YouTube is unavailable. */
            if (!updated.latest_date[0]) {
                snprintf(updated.latest_date, sizeof(updated.latest_date), "%s",
                         favorites->channels[i].latest_date);
                snprintf(updated.latest_video_id, sizeof(updated.latest_video_id), "%s",
                         favorites->channels[i].latest_video_id);
            }
            favorites->channels[i] = updated;
            return 0;
        }
    }
    if (favorites->channel_count == YT_FAVORITES_LIMIT) return -1;
    favorites->channels[favorites->channel_count++] = *channel;
    return 1;
}

/** Remove a saved video and compact the remaining entries. */
int yt_favorites_remove_video(YtFavorites *favorites, const char *id)
{
    if (!favorites || !id) return -1;
    for (int i = 0; i < favorites->video_count; i++) {
        if (strcmp(favorites->videos[i].id, id) != 0) continue;
        if (i + 1 < favorites->video_count)
            memmove(&favorites->videos[i], &favorites->videos[i + 1],
                    (size_t)(favorites->video_count - i - 1) * sizeof(YtSearchResult));
        favorites->video_count--;
        return 1;
    }
    return 0;
}

/** Remove a saved channel and compact the remaining entries. */
int yt_favorites_remove_channel(YtFavorites *favorites, const char *id)
{
    if (!favorites || !id) return -1;
    for (int i = 0; i < favorites->channel_count; i++) {
        if (strcmp(favorites->channels[i].id, id) != 0) continue;
        if (i + 1 < favorites->channel_count)
            memmove(&favorites->channels[i], &favorites->channels[i + 1],
                    (size_t)(favorites->channel_count - i - 1) * sizeof(YtFavoriteChannel));
        favorites->channel_count--;
        return 1;
    }
    return 0;
}
