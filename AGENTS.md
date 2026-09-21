# SSSPlayer — agent notes

## Build speed (mandatory)

Small code changes must stay fast. Do **not** treat Vita builds as “always multi-minute.”

Root cause of past slow links: **FFmpeg archives built with `-flto`**. Every app link then re-ran dozens of LTRANS jobs.

### Required workflow

1. Prefer `scripts/quick-vpk.sh` (incremental, `SSSPLAYER_ENABLE_LTO=OFF`, `SSSPLAYER_FAST_DEV=ON`).
2. Never wipe `build/` or `build/CMakeFiles/SSSPlayer.dir` for a one-file edit.
3. Never rebuild FFmpeg unless `tools/build-ffmpeg-vita-hw.sh` / codec deps changed (or the one-time `scripts/rebuild-ffmpeg-nolto.sh`).
4. Do not create GitHub releases or store submissions unless the user asked.
5. **Keep all GitHub releases** — never `gh release delete` older tags/assets (users may stay on older builds to test in-app update).
6. After a one-time non-LTO FFmpeg install (`scripts/rebuild-ffmpeg-nolto.sh`), expect compile+link of a small change in seconds; VPK zip of ~47MB may still take tens of seconds (`SSSPLAYER_SKIP_VPK=1` skips pack).

### If link logs show `lto-wrapper` / `LTRANS`

FFmpeg still has slim LTO objects. Rebuild once with the current `tools/build-ffmpeg-vita-hw.sh` (no `-flto`), then continue with `scripts/quick-vpk.sh`.
