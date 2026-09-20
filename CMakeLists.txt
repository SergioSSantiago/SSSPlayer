cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your VitaSDK installation")
  endif()
endif()

project(SSSPlayer C)

## Hardware decode stack (pinned deps under build/deps).
option(SSSPLAYER_ENABLE_H264_VITA
  "Use the complete FFmpeg h264_vita hardware decoder" ON)
if(NOT SSSPLAYER_ENABLE_H264_VITA)
  message(FATAL_ERROR "SSSPlayer requires vita-hw-decoder; software fallback is runtime-only.")
endif()

set(VITAMEDIADECK_H264_VITA_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/ffmpeg-vita-hw"
  CACHE PATH "Prefix produced by tools/build-ffmpeg-vita-hw.sh")
set(VITAMEDIADECK_HW_DECODER_PACKAGE
  "${CMAKE_CURRENT_SOURCE_DIR}/packages/vita-hw-decoder"
  CACHE PATH "Path to the vita-hw-decoder package")
set(VITAMEDIADECK_SW_DECODER_PACKAGE
  "${CMAKE_CURRENT_SOURCE_DIR}/packages/vita-sw-decoder"
  CACHE PATH "Path to the vita-sw-decoder package")
set(VITAMEDIADECK_HTTPS_PACKAGE
  "${CMAKE_CURRENT_SOURCE_DIR}/packages/vita-https"
  CACHE PATH "Path to the vita-https package")
set(VITAMEDIADECK_HTTPS_CURL_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/curl-mbedtls"
  CACHE PATH "Pinned curl/Mbed TLS prefix produced by vita-https")

foreach(package_root IN ITEMS
    "${VITAMEDIADECK_HW_DECODER_PACKAGE}"
    "${VITAMEDIADECK_SW_DECODER_PACKAGE}"
    "${VITAMEDIADECK_HTTPS_PACKAGE}")
  if(NOT EXISTS "${package_root}/CMakeLists.txt")
    message(FATAL_ERROR "Missing package: ${package_root}")
  endif()
endforeach()

set(VITAMEDIADECK_LIBSSH2_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/libssh2-vita"
  CACHE PATH "Prefix produced by tools/build-libssh2-vita.sh")
set(VITAMEDIADECK_LIBSSH2_ARCHIVE "${VITAMEDIADECK_LIBSSH2_ROOT}/lib/libssh2.a")
if(NOT EXISTS "${VITAMEDIADECK_LIBSSH2_ARCHIVE}")
  message(FATAL_ERROR "Missing ${VITAMEDIADECK_LIBSSH2_ARCHIVE}; build libssh2 first.")
endif()
add_library(vitamediadeck_libssh2 STATIC IMPORTED GLOBAL)
set_target_properties(vitamediadeck_libssh2 PROPERTIES
  IMPORTED_LOCATION "${VITAMEDIADECK_LIBSSH2_ARCHIVE}"
  INTERFACE_LINK_LIBRARIES "mbedtls;mbedx509;mbedcrypto;z")

set(VITAMEDIADECK_JANSSON_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/jansson-vita"
  CACHE PATH "Prefix produced by tools/build-jansson-vita.sh")
set(VITAMEDIADECK_JANSSON_ARCHIVE "${VITAMEDIADECK_JANSSON_ROOT}/lib/libjansson.a")
if(NOT EXISTS "${VITAMEDIADECK_JANSSON_ARCHIVE}")
  message(FATAL_ERROR "Missing ${VITAMEDIADECK_JANSSON_ARCHIVE}; build jansson first.")
endif()
add_library(vitamediadeck_jansson STATIC IMPORTED GLOBAL)
set_target_properties(vitamediadeck_jansson PROPERTIES
  IMPORTED_LOCATION "${VITAMEDIADECK_JANSSON_ARCHIVE}"
  INTERFACE_INCLUDE_DIRECTORIES "${VITAMEDIADECK_JANSSON_ROOT}/include")

set(VITAMEDIADECK_QUIRC_ARCHIVE
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/quirc-vita/lib/libquirc.a"
  CACHE FILEPATH "quirc archive built by tools/build-quirc-vita.sh")
if(NOT EXISTS "${VITAMEDIADECK_QUIRC_ARCHIVE}")
  message(FATAL_ERROR "Missing ${VITAMEDIADECK_QUIRC_ARCHIVE}; build quirc first.")
endif()
add_library(vitamediadeck_quirc STATIC IMPORTED GLOBAL)
set_target_properties(vitamediadeck_quirc PROPERTIES
  IMPORTED_LOCATION "${VITAMEDIADECK_QUIRC_ARCHIVE}"
  INTERFACE_INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/quirc-vita/include")

set(VITAMEDIADECK_STB_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/stb"
  CACHE PATH "Pinned stb_image prefix")
if(NOT EXISTS "${VITAMEDIADECK_STB_ROOT}/include/stb_image.h")
  message(FATAL_ERROR "Missing stb_image; run tools/prepare-stb-image.sh")
endif()

set(VITAMEDIADECK_RELEASE_LICENSE_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/release-licenses"
  CACHE PATH "License bundle from tools/prepare-release-licenses.sh")

set(VITAMEDIADECK_FFMPEG_ROOT "${VITAMEDIADECK_H264_VITA_ROOT}")
foreach(ffmpeg_lib avformat avcodec avutil)
  set(_archive "${VITAMEDIADECK_FFMPEG_ROOT}/lib/lib${ffmpeg_lib}.a")
  if(NOT EXISTS "${_archive}")
    message(FATAL_ERROR "Missing ${_archive}; run tools/build-ffmpeg-vita-hw.sh")
  endif()
  add_library(vitamediadeck_${ffmpeg_lib} STATIC IMPORTED GLOBAL)
  set_target_properties(vitamediadeck_${ffmpeg_lib} PROPERTIES
    IMPORTED_LOCATION "${_archive}")
endforeach()
set(VITAMEDIADECK_AVUTIL_LINK vitamediadeck_avutil)
set(VITAMEDIADECK_FFMPEG_LICENSE
    "${VITAMEDIADECK_FFMPEG_ROOT}/share/licenses/FFmpeg-LGPL-2.1.txt")
set(VITAMEDIADECK_WILIWILI_LICENSE
    "${VITAMEDIADECK_FFMPEG_ROOT}/share/licenses/wiliwili-GPL-3.0.txt")
set(VITAMEDIADECK_FFMPEG_LICENSE_RESOURCE
    FILE "${VITAMEDIADECK_FFMPEG_LICENSE}" licenses/FFmpeg-LGPL-2.1.txt)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()
option(SSSPLAYER_ENABLE_LTO "Enable LTO for production Vita builds" ON)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "SSSPlayer")
set(VITA_TITLEID  "SSSP00001")
set(VITA_VERSION  "01.01")
set(SSSPLAYER_VERSION_LABEL "1.1.3")
set(SSSPLAYER_VPK_NAME "SSSPlayer-${SSSPLAYER_VERSION_LABEL}.vpk")

set(VITA_MKSFOEX_FLAGS
  "-d ATTRIBUTE=17338376"
  "-d ATTRIBUTE_MINOR=0"
  "-s CATEGORY=gd"
  "-s CONTENT_ID=HB0000-SSSP00001_00-SSSPLAYER000000"
  "-d EBOOT_APP_MEMSIZE=67108864"
  "-d EBOOT_PHY_MEMSIZE=4259840"
  "-d GC_RO_SIZE=0"
  "-d GC_RW_SIZE=0"
  "-d PARENTAL_LEVEL=1"
  "-s PSP2_DISP_VER=03.600"
  "-d PSP2_SYSTEM_VER=56623104"
  "-d REGION_DENY=0"
  "-d SAVEDATA_MAX_SIZE=0"
  "-s VERSION=01.01"
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=gnu11")
add_link_options("LINKER:-z,noexecstack")

include_directories(
  include
  sqlite
  src
  ${VITAMEDIADECK_H264_VITA_ROOT}/include
  ${VITAMEDIADECK_LIBSSH2_ROOT}/include
  ${VITAMEDIADECK_STB_ROOT}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/build/deps/quirc-vita/include
  ${VITASDK}/arm-vita-eabi/include
  ${VITASDK}/arm-vita-eabi/include/freetype2
  ${VITASDK}/arm-vita-eabi/include/libxml2
)

set(VITA_HW_DECODER_FFMPEG_ROOT "${VITAMEDIADECK_H264_VITA_ROOT}" CACHE PATH "" FORCE)
set(VITA_SW_DECODER_FFMPEG_ROOT "${VITAMEDIADECK_H264_VITA_ROOT}" CACHE PATH "" FORCE)
set(VITA_HTTPS_CURL_ROOT "${VITAMEDIADECK_HTTPS_CURL_ROOT}" CACHE PATH "" FORCE)
add_subdirectory("${VITAMEDIADECK_HW_DECODER_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-hw-decoder")
add_subdirectory("${VITAMEDIADECK_SW_DECODER_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-sw-decoder")
add_subdirectory("${VITAMEDIADECK_HTTPS_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-https")
target_compile_options(vita_https PRIVATE $<$<CONFIG:Release>:-O3>)

## Music shell + video playback / network stack.
set(SOURCES
  ## Music shell
  src/main.c
  src/audio_engine.c
  src/decoder_mp3.c
  src/decoder_flac.c
  src/decoder_ogg.c
  src/file_browser.c
  src/media_db.c
  src/metadata.c
  src/playlist.c
  src/ui.c
  src/visualizer.c
  src/equalizer.c
  src/kiss_fft.c
  src/vita_shims.c
  src/vita_sqlite.c
  src/font_renderer.c
  src/noto_compat.c
  src/theme.c
  src/gifdec.c
  src/noto.c
  sqlite/sqlite3.c
  ## Video bridge
  src/video_bridge.c
  ## Common / i18n / settings / system / history
  src/common/debugScreen.c
  src/common/text_log.c
  src/i18n/i18n.c
  src/i18n/strings.c
  src/history/playback_history.c
  src/settings/preferences.c
  src/system/background_audio.c
  src/system/display_awake.c
  src/system/performance.c
  ## Network
  src/network/network_source.c
  src/network/webdav_source.c
  src/network/sftp_source.c
  src/network/smb_source.c
  src/network/jellyfin_source.c
  src/network/download_manager.c
  ## Media / decoder UI
  src/media/background_playback.c
  src/media/audio_volume.c
  src/media/avplayer_init.c
  src/media/media_tracks.c
  src/media/vita_decoder.c
  src/media/video_thumbnail.c
  src/media/hw_player_screen.c
  src/media/image_loader.c
  src/media/music_metadata.c
  ## Video UI surfaces
  src/ui/font.c
  src/ui/format.c
  src/ui/components.c
  src/ui/runtime.c
  src/ui/focus_glow.c
  src/ui/brand.c
  src/ui/touch.c
  src/ui/text_input.c
  src/ui/loading_screen.c
  src/ui/image_viewer.c
  src/ui/mini_player.c
  src/ui/sections_sidebar.c
  src/ui/local_media_screen.c
  src/ui/local_files_screen.c
  src/ui/music_player.c
  src/ui/settings_screen.c
  src/ui/about_screen.c
  src/ui/log_viewer.c
  src/ui/text_reader.c
  src/ui/network_sources_screen.c
  src/ui/qr_scanner.c
)

set_source_files_properties(sqlite/sqlite3.c PROPERTIES
  COMPILE_FLAGS "-DSQLITE_OS_OTHER=1 -DSQLITE_TEMP_STORE=3 -DSQLITE_THREADSAFE=0 -w"
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_options(${PROJECT_NAME} PRIVATE
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
  -ffunction-sections -fdata-sections
  $<$<CONFIG:Release>:-O3>)
target_link_options(${PROJECT_NAME} PRIVATE
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
  -Wl,--gc-sections)
if(SSSPLAYER_ENABLE_LTO)
  target_compile_options(${PROJECT_NAME} PRIVATE -flto=4 -fuse-linker-plugin)
  target_link_options(${PROJECT_NAME} PRIVATE -flto=4 -fuse-linker-plugin)
endif()

target_link_libraries(${PROJECT_NAME}
  SceDisplay_stub
  SceCtrl_stub
  SceNet_stub
  SceNetCtl_stub
  SceSysmodule_stub
  SceRegistryMgr_stub
  ScePower_stub
  SceRtc_stub
  vita2d
  ScePgf_stub
  ScePvf_stub
  jpeg
  freetype
  bz2
  png
  z
  webpdecoder
  VitaHwDecoder::VitaHwDecoder
  VitaSwDecoder::VitaSwDecoder
  VitaHttps::VitaHttps
  vitamediadeck_avformat
  vitamediadeck_avcodec
  ${VITAMEDIADECK_AVUTIL_LINK}
  mpg123
  vorbisfile
  vorbis
  FLAC
  ogg
  vitamediadeck_libssh2
  smb2
  xml2
  vitamediadeck_jansson
  SceAvPlayer_stub
  SceVideodec_stub
  SceCodecEngine_stub
  SceAudiodec_stub
  SceGxm_stub
  SceAppMgr_stub
  SceCommonDialog_stub
  SceAppUtil_stub
  SceShellSvc_stub
  SceIme_stub
  SceTouch_stub
  SceCamera_stub
  SceAudio_stub
  SceIofilemgr_stub
  SceKernelThreadMgr_stub
  m
  pthread
  vitamediadeck_quirc
  c
)

set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)

vita_create_vpk(${SSSPLAYER_VPK_NAME} ${VITA_TITLEID} ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE assets/icons/icon0.png    sce_sys/icon0.png
  FILE assets/icons/bg.png       sce_sys/livearea/contents/bg.png
  FILE assets/icons/startup.png  sce_sys/livearea/contents/startup.png
  FILE assets/livearea/template.xml  sce_sys/livearea/contents/template.xml
  FILE assets/fonts/NotoSans-Regular.ttf    fonts/NotoSans-Regular.ttf
  FILE assets/fonts/NotoSansJP-Regular.ttf  fonts/NotoSansJP-Regular.ttf
  FILE assets/fonts/NotoSansKR-Regular.ttf  fonts/NotoSansKR-Regular.ttf
  FILE assets/fonts/NotoSansSC-Regular.ttf  fonts/NotoSansSC-Regular.ttf
  FILE assets/fonts/Inter-Medium.ttf        fonts/Inter-Medium.ttf
  FILE assets/fonts/Inter-SemiBold.ttf      fonts/Inter-SemiBold.ttf
  FILE assets/fonts/Inter-OFL.txt           licenses/Inter-OFL.txt
  FILE LICENSE licenses/SSSPlayer-GPLv3.txt
  FILE THIRD_PARTY_NOTICES.md licenses/THIRD_PARTY_NOTICES.md
  FILE sce_modules/reAvPlayer.suprx modules/reAvPlayer.suprx
  FILE themes/classic-light/theme.ini                       themes/classic-light/theme.ini
  FILE themes/damask/theme.ini                              themes/damask/theme.ini
  FILE themes/damask/bg_default.png                         themes/damask/bg_default.png
  FILE themes/damask/bg_now_playing.png                     themes/damask/bg_now_playing.png
  FILE themes/damask/bg_settings.png                        themes/damask/bg_settings.png
  FILE themes/damask/font.ttf                               themes/damask/font.ttf
  FILE themes/halcyon/theme.ini                             themes/halcyon/theme.ini
  FILE themes/halcyon/bg_default.png                        themes/halcyon/bg_default.png
  FILE themes/halcyon/font.ttf                              themes/halcyon/font.ttf
  FILE themes/lazuli/theme.ini                              themes/lazuli/theme.ini
  FILE themes/lazuli/bg_default.png                         themes/lazuli/bg_default.png
  FILE themes/lazuli/bg_now_playing.png                     themes/lazuli/bg_now_playing.png
  FILE themes/lazuli/bg_settings.png                        themes/lazuli/bg_settings.png
  FILE themes/lazuli/font.ttf                               themes/lazuli/font.ttf
  FILE themes/nexus/theme.ini                               themes/nexus/theme.ini
  FILE themes/nexus/bg_default.png                          themes/nexus/bg_default.png
  FILE themes/nexus/bg_now_playing.png                      themes/nexus/bg_now_playing.png
  FILE themes/nexus/bg_settings.png                         themes/nexus/bg_settings.png
  FILE themes/nexus/font.ttf                                themes/nexus/font.ttf
  FILE themes/terminus/theme.ini                            themes/terminus/theme.ini
  FILE themes/terminus/bg_default.png                       themes/terminus/bg_default.png
  FILE themes/terminus/bg_now_playing.png                   themes/terminus/bg_now_playing.png
  FILE themes/terminus/bg_settings.png                      themes/terminus/bg_settings.png
  FILE themes/terminus/bg_visualizer.png                    themes/terminus/bg_visualizer.png
  FILE themes/terminus/album_art_frame.png                  themes/terminus/album_art_frame.png
  FILE themes/terminus/album_art_overlay.png                themes/terminus/album_art_overlay.png
  FILE themes/terminus/thumb_progress.png                   themes/terminus/thumb_progress.png
  FILE themes/terminus/thumb_volume.png                     themes/terminus/thumb_volume.png
  FILE themes/terminus/font.ttf                             themes/terminus/font.ttf
  FILE themes/voltaic/theme.ini                             themes/voltaic/theme.ini
  FILE themes/voltaic/bg_default.png                        themes/voltaic/bg_default.png
  FILE themes/voltaic/font.ttf                              themes/voltaic/font.ttf
)
