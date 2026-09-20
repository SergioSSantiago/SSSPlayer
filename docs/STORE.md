# Store submission — VitaDB, VitaAlive & NeoVitaDB

Use this sheet when submitting **SSSPlayer** to [VitaDB](https://www.rinnegatamante.eu/vitadb/#/submit), VitaAlive (same metadata), and [NeoVitaDB Catalog-Test](https://github.com/robin994/NeoVitaDB-Catalog-Test).

## Identity

| Field | Value |
|---|---|
| **Name** | SSSPlayer |
| **Author / Developer** | SergioSSantiago |
| **TITLEID** | `SSSP00001` |
| **Version** | 1.1.20 |
| **Category** | Utility / Media player |
| **License** | GPL-3.0-only |
| **Source** | https://github.com/SergioSSantiago/SSSPlayer |
| **Download** | https://github.com/SergioSSantiago/SSSPlayer/releases/latest |
| **Direct VPK** | https://github.com/SergioSSantiago/SSSPlayer/releases/download/v1.1.20/SSSPlayer-1.1.20.vpk |
| **Icon** | `assets/icons/icon0.png` (128×128) |
| **Data dir** | `ux0:data/SSSPlayer/` |
| **NeoVitaDB id** | `1941` (`1941-sssplayer`) |

`SSSP00001` is unique to this app (not VitaWave, not VitaMediaDeck / VideoSSS).

## Short description (English)

Music and video player for PS Vita by SergioSSantiago. VitaWave-style music (EQ, visualizer, playlists, themes), VitaMediaDeck-class H.264 video with precise seek, network streaming (SMB, SFTP, WebDAV, Jellyfin), plus YouTube search/play/download and in-app update check.

## Short description (Spanish)

Reproductor de música y vídeo para PS Vita por SergioSSantiago. Música estilo VitaWave (EQ, visualizer, playlists, temas), vídeo H.264 con seek preciso al estilo VitaMediaDeck, streaming de red (SMB, SFTP, WebDAV, Jellyfin), más búsqueda/reproducción/descarga de YouTube y comprobación de actualizaciones en la app.

## Long description (English)

SSSPlayer is an all-in-one media player for PlayStation Vita.

**Music**
- Browse `ux0:/music` and `uma0:/music` (MP3, FLAC, OGG, M4A/AAC)
- Now Playing with touch scrub on the progress bar
- 10-band equalizer, spectrum visualizer, M3U playlists
- Theme system (Terminus CRT theme by default)

**Video**
- Browse `ux0:/video`, `uma0:/video`, `ux0:/movies`, `uma0:/movies`
- Hardware H.264 playback with exact seek / scrub
- Subtitles and audio tracks
- Network sources: SMB, SFTP, WebDAV, Jellyfin

**YouTube**
- Search and stream playback on-device
- Download video (H.264 up to 720p remuxed to MP4) or audio (MP3 / M4A)
- Destination folder picker; in-app delete (Square); download progress

**Extras**
- In-app check for updates (GitHub Releases); VitaShell-style VPK promote when possible
- Local file browser with delete

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
- Network features and YouTube need Wi‑Fi

## Screenshots checklist

Before submitting, capture on-device screenshots of:

1. Browser root (music + video mounts)
2. Now Playing (music) with Terminus theme
3. Video playback HUD / seek bar
4. YouTube search / download screen
5. Settings (Video Library / Network Videos / Update)

Place optional store assets under `docs/store/` if you add them later.

## VitaDB submit checklist

VitaDB accounts need **submit privileges** (role Founder/Admin/Developer — typically role ≤ 2). A normal User account (role 5) can log in but **cannot** upload icons or call `submit.php`.

- [ ] Logged in at https://www.rinnegatamante.eu/vitadb/
- [ ] Account has submit rights (if icon upload says “correct privileges”, ask VitaDB staff to promote the account)
- [ ] New homebrew → name **SSSPlayer**, author **SergioSSantiago**
- [ ] TITLEID **SSSP00001**
- [ ] Version **1.1.20**
- [ ] Paste short + long description from this file (or `docs/store/SUBMIT_PASTE.md`)
- [ ] Link latest GitHub release / VPK (`SSSPlayer-1.1.20.vpk`)
- [ ] Upload icon0.png
- [ ] Upload screenshots
- [ ] Confirm GPL-3.0 / open source link
- [ ] Submit

Paste-ready copy: [`docs/store/SUBMIT_PASTE.md`](store/SUBMIT_PASTE.md)

VitaDB has **no public anonymous submit API** — submission requires your logged-in account **with elevated role**.

## VitaAlive

Use the **same** TITLEID, author, description, icon, and VPK URL as VitaDB so both stores stay in sync when you publish updates. (`vitaalive.com` has been offline; keep metadata ready for when the mirror is back.)

## NeoVitaDB

Staging PR (Catalog-Test): https://github.com/robin994/NeoVitaDB-Catalog-Test/pull/11  
Entry: `apps/vita/1941-sssplayer.json` — catalog resolves the latest `*.vpk` from GitHub Releases automatically (no version bump required when only the release changes).
