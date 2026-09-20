# SSSPlayer

**Latest release: [v1.1.14](https://github.com/SergioSSantiago/SSSPlayer/releases/tag/v1.1.14)**  
Download: `SSSPlayer-1.1.14.vpk`

Music + video media player for PlayStation Vita by **[SergioSSantiago](https://github.com/SergioSSantiago)**.

SSSPlayer brings together a polished music experience (equalizer, visualizer, playlists, themes — Terminus by default) with a video player that lets you **seek to the exact moment** you want, plus local folders and network sources (SMB / SFTP / WebDAV / Jellyfin).

| | |
|---|---|
| **TITLEID** | `SSSP00001` |
| **Developer** | [SergioSSantiago](https://github.com/SergioSSantiago) |
| **Version** | 1.1.14 |
| **Data** | `ux0:data/SSSPlayer/` |
| **License** | GPL-3.0-only |

## Acknowledgments

SSSPlayer stands on the shoulders of two excellent Vita homebrew projects. Thank you:

- **[VitaWave](https://github.com/Jyotiraditya-Samal/Vitawave)** — music shell, browser, equalizer, visualizer, playlists, and theme system that define SSSPlayer’s look and feel.
- **[VitaMediaDeck](https://github.com/spyro-98/VitaMediaDeck)** (VideoSSS stack) — hardware H.264 decode, precise seek/scrub, subtitles, and network streaming (SMB / SFTP / WebDAV / Jellyfin).

This app reuses and combines parts of both codebases into one media player. All credit for those foundations belongs to their authors and contributors.

## Features

### Music
- MP3, FLAC, OGG, M4A/AAC from `ux0:/music` and `uma0:/music`
- Now Playing with **touch scrub** on the progress bar
- 10-band EQ, spectrum visualizer, M3U playlists
- Theme system (Terminus default CRT look)

### Video
- Open files from `ux0:/video`, `uma0:/video`, `ux0:/movies`, `uma0:/movies`, or any folder in the browser
- Hardware H.264 decode with precise seek / scrub
- Subtitles, audio tracks, Video Library + Network Videos
- **YouTube**: search, stream playback, download video or audio (MP3/M4A) with folder picker

## Install

1. Download the latest `SSSPlayer-x.y.z.vpk` from [Releases](https://github.com/SergioSSantiago/SSSPlayer/releases).
2. Enable FTP in **VitaShell** on the Vita.
3. Connect with **Cyberduck** (or any FTP client) and copy the VPK to `ux0:` (for example `ux0:/`).
4. On the Vita, open **VitaShell**, select the VPK, and install it.

After install, put music under `ux0:/music` (or `uma0:/music`) and videos under `ux0:/video` / `uma0:/video` (or `movies`).

## Store listing (VitaDB / VitaAlive / NeoVitaDB)

Ready for submission. See [`docs/STORE.md`](docs/STORE.md) and paste-ready [`docs/store/SUBMIT_PASTE.md`](docs/store/SUBMIT_PASTE.md).

- **Unique TITLEID:** `SSSP00001` (does not clash with VitaWave or VitaMediaDeck/VideoSSS)
- **Latest VPK:** [SSSPlayer-1.1.14.vpk](https://github.com/SergioSSantiago/SSSPlayer/releases/download/v1.1.14/SSSPlayer-1.1.14.vpk)
- **Icon:** `assets/icons/icon0.png` (128×128 indexed PNG)
- **Source:** https://github.com/SergioSSantiago/SSSPlayer
- **NeoVitaDB:** [PR #11 (Catalog-Test)](https://github.com/robin994/NeoVitaDB-Catalog-Test/pull/11) — id `1941`

Submit on [VitaDB](https://www.rinnegatamante.eu/vitadb/#/submit) (and VitaAlive with the same metadata when online).

## Building from source

Requires [VitaSDK](https://vitasdk.org) and pinned deps under `build/deps` (FFmpeg h264_vita, libssh2, jansson, quirc, stb, curl/mbedtls).

```bash
git clone https://github.com/SergioSSantiago/SSSPlayer.git
cd SSSPlayer
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVITASDK=$VITASDK
make -j$(sysctl -n hw.ncpu)
```

The packaged file is `SSSPlayer-1.1.14.vpk` (version embedded in the filename).

## License

GPL-3.0-only. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
