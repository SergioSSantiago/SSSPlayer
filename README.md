# SSSPlayer

Vitawave music shell + VideoSSS video/network playback for PS Vita.

**Title ID:** `SSSP00001`  
**Default theme:** Terminus  
**Data:** `ux0:data/SSSPlayer/`

## Features

### Music (Vitawave)
- MP3 / FLAC / OGG browser and Now Playing
- Equalizer, visualizer, playlists, themes (Terminus default)

### Video (VideoSSS stack)
- Local H.264 hardware decode with software fallback
- Seek / scrub, subtitles, audio tracks
- Video Library + Network Videos (SMB / SFTP / WebDAV / Jellyfin)
- Open video files directly from the file browser (mp4, mkv, avi, mov, webm, …)

## Building

Requires [VitaSDK](https://vitasdk.org) and the same pinned `build/deps` used by VideoSSS (FFmpeg h264_vita, libssh2, jansson, quirc, stb, curl/mbedtls, release licenses).

```bash
git clone https://github.com/SergioSSantiago/SSSPlayer.git
cd SSSPlayer
# Prepare deps (scripts under tools/, or symlink build/deps from a VideoSSS tree)
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVITASDK=$VITASDK
make -j$(sysctl -n hw.ncpu)
```

Install `build/SSSPlayer.vpk` via VitaShell.

## Store distribution

Unique TITLEID `SSSP00001` is reserved for VitaDB / VitaAlive submissions.
