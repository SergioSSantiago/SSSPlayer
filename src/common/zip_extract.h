#ifndef SSSPLAYER_ZIP_EXTRACT_H
#define SSSPLAYER_ZIP_EXTRACT_H

/* Extract a ZIP (VPK) archive into dest_dir. Creates nested folders as needed.
 * Supports store and deflate entries. Returns 0 on success. */
int sss_zip_extract(const char *zip_path, const char *dest_dir);

#endif
