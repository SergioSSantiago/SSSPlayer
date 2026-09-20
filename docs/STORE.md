# Store submission — VitaDB & VitaAlive

Use this sheet when submitting **SSSPlayer** to [VitaDB](https://www.rinnegatamante.eu/vitadb/#/submit) and VitaAlive.

## Identity

| Field | Value |
|---|---|
| **Name** | SSSPlayer |
| **Author / Developer** | SergioSSantiago |
| **TITLEID** | `SSSP00001` |
| **Version** | 1.1.4 |
| **Category** | Utility / Media player |
| **License** | GPL-3.0-only |
| **Source** | https://github.com/SergioSSantiago/SSSPlayer |
| **Download** | https://github.com/SergioSSantiago/SSSPlayer/releases/latest |
| **Direct VPK** | https://github.com/SergioSSantiago/SSSPlayer/releases/download/v1.1.4/SSSPlayer-1.1.4.vpk |
| **Icon** | `assets/icons/icon0.png` (128×128) |
| **Data dir** | `ux0:data/SSSPlayer/` |

`SSSP00001` is unique to this app (not VitaWave, not VitaMediaDeck / VideoSSS).

## Short description (English)

Music and video player for PS Vita by SergioSSantiago. Combines a VitaWave-style music shell (EQ, visualizer, playlists, themes) with VitaMediaDeck-class video playback: H.264 hardware decode, precise touch seek, subtitles, and network streaming (SMB, SFTP, WebDAV, Jellyfin).

## Short description (Spanish)

Reproductor de música y vídeo para PS Vita por SergioSSantiago. Une un shell de música estilo VitaWave (EQ, visualizer, playlists, temas) con reproducción de vídeo al nivel de VitaMediaDeck: decode H.264 por hardware, seek táctil preciso, subtítulos y streaming de red (SMB, SFTP, WebDAV, Jellyfin).

## Long description (English)

SSSPlayer is an all-in-one media player for PlayStation Vita.

**Music**
- Browse `ux0:/music` and `uma0:/music` (MP3, FLAC, OGG)
- Now Playing with touch scrub on the progress bar
- 10-band equalizer, spectrum visualizer, M3U playlists
- Theme system (Terminus CRT theme by default)

**Video**
- Browse `ux0:/video`, `uma0:/video`, `ux0:/movies`, `uma0:/movies`
- Hardware H.264 playback with exact seek / scrub
- Subtitles and audio tracks
- Network sources: SMB, SFTP, WebDAV, Jellyfin

**Install**
1. Download `SSSPlayer-x.y.z.vpk` from GitHub Releases
2. Enable FTP in VitaShell
3. Copy the VPK to `ux0:` with Cyberduck (or any FTP client)
4. Install the VPK from VitaShell

**Credits**
Built by SergioSSantiago. Thanks to VitaWave and VitaMediaDeck — SSSPlayer reuses foundations from both projects.

## Requirements

- PS Vita / PSTV with HENkaku / enso (standard homebrew)
- No extra plugins required for local music/video
- Network features need Wi‑Fi

## Screenshots checklist

Before submitting, capture on-device screenshots of:

1. Browser root (music + video mounts)
2. Now Playing (music) with Terminus theme
3. Video playback HUD / seek bar
4. Settings (Video Library / Network Videos)

Place optional store assets under `docs/store/` if you add them later.

## VitaDB submit checklist

- [ ] Logged in at https://www.rinnegatamante.eu/vitadb/
- [ ] New homebrew → name **SSSPlayer**, author **SergioSSantiago**
- [ ] TITLEID **SSSP00001**
- [ ] Paste short + long description from this file
- [ ] Link latest GitHub release / VPK
- [ ] Upload icon0.png
- [ ] Upload screenshots
- [ ] Confirm GPL-3.0 / open source link
- [ ] Submit

## VitaAlive

Use the **same** TITLEID, author, description, icon, and VPK URL as VitaDB so both stores stay in sync when you publish updates.
