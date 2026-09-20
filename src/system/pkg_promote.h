#ifndef SSSPLAYER_PKG_PROMOTE_H
#define SSSPLAYER_PKG_PROMOTE_H

/* Generate sce_sys/package/head.bin for an extracted VPK directory, then
 * install it with ScePromoterUtil. path is the extracted package root
 * (contains eboot.bin + sce_sys/). Returns 0 on success. */
int sss_pkg_promote(const char *path);

#endif
