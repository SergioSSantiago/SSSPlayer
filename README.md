# SSSPlayer

A music and video player for the PS Vita.

[Latest release](https://github.com/SergioSSantiago/SSSPlayer/releases/latest)

## Features

- Music: MP3, FLAC, OGG, M4A/AAC from `ux0:music/` and `uma0:music/`
- Now Playing with touch scrub, 10-band EQ, spectrum visualizer, M3U playlists
- Theme system (Terminus by default)
- Background audio when the screen is off
- Video: hardware H.264 decode with precise seek from local folders
- Subtitles and audio tracks
- Network streaming: SMB, SFTP, WebDAV, Jellyfin
- YouTube search, playback, and download (video or audio)
- In-app update check (downloads a VPK for VitaShell to install)

**Title ID:** `SSSP00001`  
Quit from **Settings → Exit SSSPlayer** (Home is disabled to avoid a bad suspend).

## Building

Requires [VitaSDK](https://vitasdk.org).

```bash
git clone https://github.com/SergioSSantiago/SSSPlayer.git
cd SSSPlayer
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVITASDK=$VITASDK
make -j4
```

Install `build/SSSPlayer-*.vpk` with VitaShell.

## Credits

Based on work from [VitaWave](https://github.com/Jyotiraditya-Samal/Vitawave) and [VitaMediaDeck](https://github.com/spyro-98/VitaMediaDeck).

## License

GPL-3.0-only. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
