#ifndef VITAMEDIADECK_UI_THEME_H
#define VITAMEDIADECK_UI_THEME_H

#include <vita2d.h>

/* SSSPlayer video surfaces use the Vitawave Terminus CRT palette so the
 * player HUD matches the default music shell instead of Spectral cyan. */
#define VT_THEME_BG             RGBA8(4, 10, 4, 255)
#define VT_THEME_BG_SOFT        RGBA8(10, 26, 10, 255)
#define VT_THEME_MEDIA_BACKDROP RGBA8(4, 10, 4, 255)
#define VT_THEME_SURFACE        RGBA8(10, 26, 10, 246)
#define VT_THEME_SURFACE_RAISED RGBA8(14, 40, 14, 250)
#define VT_THEME_SURFACE_FOCUS  RGBA8(26, 90, 26, 248)
#define VT_THEME_BORDER         RGBA8(26, 138, 26, 255)
#define VT_THEME_BORDER_DIM     RGBA8(14, 60, 14, 255)
#define VT_THEME_GLASS_A(a)     RGBA8(10, 40, 10, (a))

#define VT_THEME_SIGNAL_DIM     RGBA8(14, 90, 14, 255)
#define VT_THEME_SIGNAL         RGBA8(26, 138, 26, 255)
#define VT_THEME_SIGNAL_BRIGHT  RGBA8(68, 255, 68, 255)
#define VT_THEME_SIGNAL_LIGHT   RGBA8(180, 255, 180, 255)
#define VT_THEME_SIGNAL_A(a)    RGBA8(68, 255, 68, (a))

#define VT_THEME_WARM_DIM       RGBA8(70, 47, 29, 255)
#define VT_THEME_WARM           RGBA8(151, 91, 43, 255)
#define VT_THEME_WARM_LIGHT     RGBA8(213, 148, 78, 255)
#define VT_THEME_WARM_A(a)      RGBA8(175, 104, 47, (a))

#define VT_THEME_SPECTRAL       RGBA8(180, 255, 180, 255)
#define VT_THEME_SPECTRAL_LIGHT RGBA8(220, 255, 220, 255)
#define VT_THEME_SPECTRAL_A(a)  RGBA8(180, 255, 180, (a))
#define VT_THEME_PARTICLE_A(a)  VT_THEME_SPECTRAL_A(a)

#define VT_THEME_COLD_DIM       RGBA8(14, 60, 14, 255)
#define VT_THEME_COLD           RGBA8(26, 138, 26, 255)
#define VT_THEME_COLD_LIGHT     RGBA8(68, 255, 68, 255)
#define VT_THEME_COLD_A(a)      RGBA8(68, 255, 68, (a))

#define VT_THEME_BLUE_DIM       VT_THEME_COLD_DIM
#define VT_THEME_BLUE           VT_THEME_COLD
#define VT_THEME_BLUE_BRIGHT    VT_THEME_COLD
#define VT_THEME_BLUE_LIGHT     VT_THEME_COLD_LIGHT
#define VT_THEME_BLUE_A(a)      VT_THEME_COLD_A(a)
#define VT_THEME_HALO_A(a)      VT_THEME_SPECTRAL_A(a)

#define VT_THEME_TEXT           RGBA8(68, 255, 68, 255)
#define VT_THEME_TEXT_DIM       RGBA8(26, 138, 26, 255)
#define VT_THEME_TEXT_MUTED     RGBA8(20, 100, 20, 255)
#define VT_THEME_TEXT_FAINT     RGBA8(14, 70, 14, 255)

#define VT_THEME_SUCCESS        RGBA8(68, 255, 68, 255)
#define VT_THEME_WARNING        RGBA8(235, 166, 75, 255)
#define VT_THEME_DANGER         RGBA8(239, 96, 82, 255)

#endif /* VITAMEDIADECK_UI_THEME_H */
