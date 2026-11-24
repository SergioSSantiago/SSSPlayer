/**
 * @file media_locations.h
 * @brief Describe saved media folders and the media types each one accepts.
 */

#ifndef SSSPLAYER_MEDIA_LOCATIONS_H
#define SSSPLAYER_MEDIA_LOCATIONS_H

#include <stdint.h>
#include <stddef.h>

/** @brief One folder may supply multiple Library tabs and download targets. */
#define VT_MEDIA_LOC_VIDEO 1U
#define VT_MEDIA_LOC_AUDIO 2U
#define VT_MEDIA_LOC_IMAGE 4U
#define VT_MEDIA_LOC_ALL   7U
#define VT_MEDIA_LOC_MAX   24
#define VT_MEDIA_LOC_PATH_MAX 512

/** @brief Canonical folder path and the types of media it contains. */
typedef struct {
    char path[VT_MEDIA_LOC_PATH_MAX];
    uint32_t media_mask;
} VtMediaLocation;

/** @brief Bounded folder list stored on disk with a checksum and backup. */
typedef struct {
    uint32_t count;
    VtMediaLocation locations[VT_MEDIA_LOC_MAX];
} VtMediaLocations;

/** @brief Provide legacy folders only when there is no valid saved list.
 * An intentionally empty list stays empty. */
void vt_media_locations_defaults(VtMediaLocations *out);
/** @brief Read saved folders or recover their backup. */
int vt_media_locations_load(VtMediaLocations *out);
/** @brief Save folders through a temporary file, then rename it. */
int vt_media_locations_save(const VtMediaLocations *locations);
/** @brief Reject unsafe paths and overlapping scan roots. */
int vt_media_locations_validate(const VtMediaLocations *locations);
/** @brief Normalize a mount path; reject relative or parent-directory parts. */
int vt_media_location_normalize(const char *path, char *out, size_t size);
/** @brief Hash the saved roots so the library cache knows when to refresh. */
uint32_t vt_media_locations_hash(const VtMediaLocations *locations);
/** @brief Show a friendly name for a media-type mask. */
const char *vt_media_location_type_name(uint32_t mask);

#endif
