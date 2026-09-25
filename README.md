# SSSPlayer

A music and video player for the PS Vita.

[Latest release](https://github.com/SergioSSantiago/SSSPlayer/releases/latest)

## Why I built this

I wanted a player that could reach media on **several memory-card mounts** — especially `ux0:` and `uma0:` — not just one fixed folder.

I also wanted **both music and video** in one app: scrub the progress bar to the **exact second**, keep music playing with the **screen off**, and watch or download **YouTube** (full video or audio-only).

## Features

- Music: MP3, FLAC, OGG, M4A/AAC from `ux0:music/` and `uma0:music/`
- Now Playing with touch scrub, 10-band EQ, spectrum visualizer, M3U playlists
- Theme system (Terminus by default)
- Background audio when the screen is off
- Video: hardware H.264 decode with precise seek from local folders (`ux0` / `uma0` video paths)
- Subtitles and audio tracks
- Network streaming: SMB, SFTP, WebDAV, Jellyfin
- YouTube search, playback, and download (video ~360p, or audio as M4A)
- In-app update check (downloads a VPK for VitaShell to install)

**Title ID:** `SSSP00001`

## Installation

1. Download the VPK from the [latest release](https://github.com/SergioSSantiago/SSSPlayer/releases/latest).
2. On the Vita, open **VitaShell** and enable **FTP** mode.
3. On your computer, connect with **Cyberduck** (or any FTP client) to the Vita.
4. Copy the VPK to **`ux0:`**.
5. In VitaShell, open the VPK on `ux0:` and install it.

You can also install SSSPlayer from homebrew stores such as **[PS Vita Alive](https://vegettosan.github.io/PSVitaAlive/app.html?title_id=SSSP00001)** and **[VitaDB](https://www.rinnegatamante.eu/vitadb/)**.

## Recommendations / tips

- **Exit the app from Settings** — Home is forced closed / disabled for a clean quit so you avoid suspend glitches. Use **Settings → Exit SSSPlayer**.
- **Updates:** use **Check for updates** in the app. The new VPK is saved on **`ux0:`**; open **VitaShell** and install that VPK to upgrade.
- Enjoy — I hope it works as well for you as it does for me.

## Building

Requires [VitaSDK](https://vitasdk.org).

```bash
git clone https://github.com/SergioSSantiago/SSSPlayer.git
cd SSSPlayer
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVITASDK=$VITASDK
make -j4
```

## Credits

Based on work from [VitaWave](https://github.com/Jyotiraditya-Samal/Vitawave) and [VitaMediaDeck](https://github.com/spyro-98/VitaMediaDeck).

## License

GPL-3.0-only. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
