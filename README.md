# SSSPlayer

**Latest release: [v1.1.2](https://github.com/SergioSSantiago/SSSPlayer/releases/tag/v1.1.2)**  
Download: `SSSPlayer-1.1.2.vpk`

Music + video media player for PlayStation Vita by **SergioSSantiago**.

SSSPlayer brings together a polished music experience (equalizer, visualizer, playlists, themes — Terminus by default) with a video player that lets you **seek to the exact moment** you want, plus local folders and network sources (SMB / SFTP / WebDAV / Jellyfin).

| | |
|---|---|
| **TITLEID** | `SSSP00001` |
| **Developer** | [SergioSSantiago](https://github.com/SergioSSantiago) |
| **Data** | `ux0:data/SSSPlayer/` |

## Features

### Music
- MP3, FLAC, OGG from `ux0:/music` and `uma0:/music`
- Now Playing, 10-band EQ, spectrum visualizer, M3U playlists
- Theme system (Terminus default CRT look)

### Video
- Open files from `ux0:/video`, `uma0:/video`, `ux0:/movies`, `uma0:/movies`, or any folder in the browser
- Hardware H.264 decode with precise seek / scrub
- Subtitles, audio tracks, Video Library + Network Videos
- Same playback stack used for local and remote streams

## Install

1. Download the latest `SSSPlayer-x.y.z.vpk` from [Releases](https://github.com/SergioSSantiago/SSSPlayer/releases).
2. Enable FTP in **VitaShell** on the Vita.
3. Connect with **Cyberduck** (or any FTP client) and copy the VPK to `ux0:` (for example `ux0:/`).
4. On the Vita, open **VitaShell**, select the VPK, and install it.

After install, put music under `ux0:/music` (or `uma0:/music`) and videos under `ux0:/video` / `uma0:/video` (or `movies`).

## Building from source

Requires [VitaSDK](https://vitasdk.org) and pinned deps under `build/deps` (FFmpeg h264_vita, libssh2, jansson, quirc, stb, curl/mbedtls).

```bash
git clone https://github.com/SergioSSantiago/SSSPlayer.git
cd SSSPlayer
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVITASDK=$VITASDK
make -j$(sysctl -n hw.ncpu)
```

The packaged file is `SSSPlayer-1.1.2.vpk` (version embedded in the filename).

## License

GPL-3.0-only. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
