/**
 * @file media_locations.c
 * @brief Persist the user's media folders safely and without duplicate scans.
 * These folders are also the allowed targets for media downloads.
 */

#include "media/media_locations.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "app_paths.h"

#define MEDIA_LOCATIONS_PATH VITAMEDIADECK_DATA_DIR "/media_locations.bin"
#define MEDIA_LOCATIONS_TMP  VITAMEDIADECK_DATA_DIR "/media_locations.tmp"
#define MEDIA_LOCATIONS_BAK  VITAMEDIADECK_DATA_DIR "/media_locations.bak"

/* The checksum also catches incomplete writes, rather than silently resetting
 * user-selected folders after a power failure. */
typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t count;
    uint32_t checksum;
    VtMediaLocation entries[VT_MEDIA_LOC_MAX];
} MediaLocationsDisk;

/** @brief Update the checksum for one block of saved settings. */
static uint32_t fnv_bytes(uint32_t hash, const void *data, size_t size) {
    const unsigned char *p = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint32_t)p[i];
        hash *= 16777619U;
    }
    return hash;
}

/** @brief Reject dot and parent-directory components. */
static int component_invalid(const char *p, size_t n) {
    return (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.');
}

/** @brief Normalize a Vita path and block directory traversal. */
int vt_media_location_normalize(const char *path, char *out, size_t size) {
    if (!path || !out || size < 5) return -1;
    /* Vita mount syntax, e.g. ux0:, uma0:/Movies, imc0:music. */
    const char *colon = strchr(path, ':');
    if (!colon || colon == path || (size_t)(colon - path) > 8) return -1;
    for (const char *p = path; p < colon; ++p)
        if (!isalnum((unsigned char)*p)) return -1;
    size_t at = 0;
    for (const char *p = path; p <= colon; ++p) {
        if (at + 1 >= size) return -1;
        out[at++] = (char)tolower((unsigned char)*p);
    }
    const char *p = colon + 1;
    while (*p == '/') ++p;
    while (*p) {
        const char *segment = p;
        while (*p && *p != '/') {
            if (*p == '\\' || (unsigned char)*p < 32 || *p == ':') return -1;
            ++p;
        }
        size_t n = (size_t)(p - segment);
        if (component_invalid(segment, n)) return -1;
        if (n) {
            if (at != (size_t)(colon - path + 1)) {
                if (at + 1 >= size) return -1;
                out[at++] = '/';
            }
            if (at + n >= size) return -1;
            memcpy(out + at, segment, n);
            at += n;
        }
        while (*p == '/') ++p;
    }
    out[at] = 0;
    return 0;
}

/** @brief Check whether one folder contains another folder. */
static int ancestor_of(const char *parent, const char *child) {
    size_t n = strlen(parent);
    if (strncmp(parent, child, n)) return 0;
    return child[n] == '\0' || child[n] == '/' || parent[n - 1] == ':';
}

/** @brief Reject invalid, duplicate, or overlapping scan roots. */
int vt_media_locations_validate(const VtMediaLocations *locations) {
    if (!locations || locations->count > VT_MEDIA_LOC_MAX) return -1;
    for (uint32_t i = 0; i < locations->count; ++i) {
        const VtMediaLocation *a = &locations->locations[i];
        char normalized[VT_MEDIA_LOC_PATH_MAX];
        /* Configuration files can be corrupted: reject non-terminated path
         * fields before strchr/strlen can read past this entry. */
        if (!memchr(a->path, '\0', sizeof(a->path))) return -1;
        if (!(a->media_mask & VT_MEDIA_LOC_ALL) ||
            (a->media_mask & ~VT_MEDIA_LOC_ALL) ||
            vt_media_location_normalize(a->path, normalized, sizeof(normalized)) ||
            strcmp(normalized, a->path)) return -1;
        for (uint32_t j = 0; j < i; ++j) {
            const VtMediaLocation *b = &locations->locations[j];
            /* Overlapping trees for the SAME media category would index files
             * twice. Overlapping trees with distinct categories are fine. */
            if ((a->media_mask & b->media_mask) &&
                (ancestor_of(a->path, b->path) ||
                 ancestor_of(b->path, a->path))) return -2;
        }
    }
    return 0;
}

/** @brief Use legacy folders only when no valid settings were saved. */
void vt_media_locations_defaults(VtMediaLocations *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    static const struct { const char *path; uint32_t mask; } original[] = {
        {"ux0:video", 3}, {"ux0:movies", 3}, {"ux0:music", 3},
        {"ux0:picture", 4}, {"ux0:photo", 4}, {"ux0:download", 7},
        {"uma0:video", 3}, {"uma0:movies", 3}, {"uma0:music", 3},
        {"uma0:picture", 4}, {"uma0:photo", 4}, {"uma0:download", 7},
    };
    out->count = sizeof(original) / sizeof(original[0]);
    for (uint32_t i = 0; i < out->count; ++i) {
        snprintf(out->locations[i].path, sizeof(out->locations[i].path), "%s",
                 original[i].path);
        out->locations[i].media_mask = original[i].mask;
    }
}

/** @brief Invalidate cached library scans after a folder change. */
uint32_t vt_media_locations_hash(const VtMediaLocations *locations) {
    if (!locations) return 0;
    uint32_t hash = fnv_bytes(2166136261U, &locations->count,
                              sizeof(locations->count));
    for (uint32_t i = 0; i < locations->count && i < VT_MEDIA_LOC_MAX; ++i) {
        const VtMediaLocation *entry = &locations->locations[i];
        hash = fnv_bytes(hash, entry->path, strlen(entry->path) + 1);
        hash = fnv_bytes(hash, &entry->media_mask, sizeof(entry->media_mask));
    }
    return hash;
}

/** @brief Give a readable label to a folder media mask. */
const char *vt_media_location_type_name(uint32_t mask) {
    switch (mask) {
        case VT_MEDIA_LOC_VIDEO: return "Video";
        case VT_MEDIA_LOC_AUDIO: return "Music";
        case VT_MEDIA_LOC_IMAGE: return "Images";
        case VT_MEDIA_LOC_VIDEO | VT_MEDIA_LOC_AUDIO: return "Video + Music";
        case VT_MEDIA_LOC_ALL: return "All media";
        default: return "Mixed media";
    }
}

/** @brief Accept a saved file only when its version and checksum match. */
static int read_locations(const char *path, VtMediaLocations *out) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return -1;
    MediaLocationsDisk *disk = malloc(sizeof(*disk));
    if (!disk) { sceIoClose(fd); return -1; }
    size_t got = 0;
    while (got < sizeof(*disk)) {
        int n = sceIoRead(fd, (unsigned char *)disk + got, sizeof(*disk) - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    sceIoClose(fd);
    int ret = -1;
    if (got == sizeof(*disk) && !memcmp(disk->magic, "SSSLOC01", 8) &&
        disk->version == 1 && disk->count <= VT_MEDIA_LOC_MAX) {
        uint32_t check = fnv_bytes(2166136261U, &disk->count, sizeof(disk->count));
        check = fnv_bytes(check, disk->entries, sizeof(disk->entries));
        if (check == disk->checksum) {
            memset(out, 0, sizeof(*out));
            out->count = disk->count;
            memcpy(out->locations, disk->entries, sizeof(disk->entries));
            ret = vt_media_locations_validate(out);
        }
    }
    free(disk);
    return ret;
}

/** @brief Recover a valid backup before falling back to defaults. */
int vt_media_locations_load(VtMediaLocations *out) {
    if (!out) return -1;
    if (read_locations(MEDIA_LOCATIONS_PATH, out) == 0) return 0;
    /* A broken primary save must not discard the last valid folder list. */
    if (read_locations(MEDIA_LOCATIONS_BAK, out) == 0) return 0;
    vt_media_locations_defaults(out);
    return 0;
}

/** @brief Write new settings to a temporary file before replacing them. */
int vt_media_locations_save(const VtMediaLocations *locations) {
    if (vt_media_locations_validate(locations)) return -1;
    MediaLocationsDisk *disk = calloc(1, sizeof(*disk));
    if (!disk) return -1;
    memcpy(disk->magic, "SSSLOC01", 8);
    disk->version = 1;
    disk->count = locations->count;
    memcpy(disk->entries, locations->locations, sizeof(disk->entries));
    disk->checksum = fnv_bytes(2166136261U, &disk->count, sizeof(disk->count));
    disk->checksum = fnv_bytes(disk->checksum, disk->entries, sizeof(disk->entries));
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(VITAMEDIADECK_DATA_DIR, 0777);
    SceUID fd = sceIoOpen(MEDIA_LOCATIONS_TMP,
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) { free(disk); return -1; }
    size_t written = 0;
    while (written < sizeof(*disk)) {
        int n = sceIoWrite(fd, (unsigned char *)disk + written,
                           sizeof(*disk) - written);
        if (n <= 0) break;
        written += (size_t)n;
    }
    int sync = written == sizeof(*disk) ? sceIoSyncByFd(fd, 0) : -1;
    sceIoClose(fd);
    free(disk);
    if (sync < 0) { sceIoRemove(MEDIA_LOCATIONS_TMP); return -1; }
    /* Keep the last complete settings file until the replacement is in place. */
    sceIoRemove(MEDIA_LOCATIONS_BAK);
    SceIoStat stat;
    memset(&stat, 0, sizeof(stat));
    int had_original = sceIoGetstat(MEDIA_LOCATIONS_PATH, &stat) >= 0;
    if (had_original && sceIoRename(MEDIA_LOCATIONS_PATH, MEDIA_LOCATIONS_BAK) < 0) {
        sceIoRemove(MEDIA_LOCATIONS_TMP);
        return -1;
    }
    if (sceIoRename(MEDIA_LOCATIONS_TMP, MEDIA_LOCATIONS_PATH) < 0) {
        if (had_original) sceIoRename(MEDIA_LOCATIONS_BAK, MEDIA_LOCATIONS_PATH);
        sceIoRemove(MEDIA_LOCATIONS_TMP);
        return -1;
    }
    return 0;
}
