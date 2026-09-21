# Agent / contributor build notes

## Fast iteration (required)

Incremental app changes must stay fast. The previous multi-minute delay was **not** “Vita is slow” — it was **FFmpeg linked with `-flto`**, so every `SSSPlayer` link re-ran ~36 LTO jobs (~5 minutes).

### Do

- Use `scripts/quick-vpk.sh` for normal VPK builds.
- For compile/link-only checks: `SSSPLAYER_SKIP_VPK=1 ./scripts/quick-vpk.sh` (skips ~47MB zip).
- Keep `SSSPLAYER_ENABLE_LTO=OFF` and `SSSPLAYER_FAST_DEV=ON` (CMake defaults).
- Rebuild only what changed; leave `build/CMakeFiles/SSSPlayer.dir` intact.
- Reconfigure only when needed: `SSSPLAYER_RECONFIGURE=1 ./scripts/quick-vpk.sh`.

### Do not

- Delete `build/CMakeFiles/SSSPlayer.dir` (or the whole `build/` tree) unless deps are broken.
- Enable LTO for routine fixes.
- Rebuild FFmpeg unless `tools/build-ffmpeg-vita-hw.sh` or codecs changed.

### One-time: non-LTO FFmpeg

If links still print `lto-wrapper` / `LTRANS jobs`, FFmpeg archives still contain slim LTO. Rebuild once:

```bash
export VITASDK=/Users/sss/vitasdk
./scripts/rebuild-ffmpeg-nolto.sh
```

Or manually:

```bash
export VITASDK=/Users/sss/vitasdk
./tools/build-ffmpeg-vita-hw.sh
./scripts/quick-vpk.sh
```

After that, a one-file change should compile + link in roughly **seconds**; packing the ~47MB VPK may still take tens of seconds (`SSSPLAYER_SKIP_VPK=1` skips pack).
