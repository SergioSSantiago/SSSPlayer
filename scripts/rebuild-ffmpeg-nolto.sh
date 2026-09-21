#!/usr/bin/env bash
# One-time: rebuild FFmpeg without LTO, swap into build/deps/ffmpeg-vita-hw, then
# time an incremental app link. Safe to re-run; keeps previous tree as *-lto-backup.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-/Users/sss/vitasdk}"
export PATH="$VITASDK/bin:${PATH:-/usr/bin:/bin}"
export VITAMEDIADECK_FFMPEG_JOBS="${VITAMEDIADECK_FFMPEG_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 8)}"

old_prefix="$repo_root/build/deps/ffmpeg-vita-hw"
new_prefix="$repo_root/build/deps/ffmpeg-vita-hw-nolto"
backup_prefix="$repo_root/build/deps/ffmpeg-vita-hw-lto-backup"
log="${FFMPEG_NOLTO_LOG:-/tmp/ffmpeg-nolto.log}"

echo "==> building non-LTO FFmpeg → $new_prefix (log: $log)"
rm -rf "$new_prefix"
export VITAMEDIADECK_H264_VITA_ROOT="$new_prefix"
"$repo_root/tools/build-ffmpeg-vita-hw.sh" 2>&1 | tee "$log"

test -f "$new_prefix/lib/libavcodec.a"
test -f "$new_prefix/lib/libavformat.a"
test -f "$new_prefix/lib/libavutil.a"

# Confirm no slim LTO in a sample object
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
member="$("$VITASDK/bin/arm-vita-eabi-ar" t "$new_prefix/lib/libavcodec.a" | grep '\.o$' | head -1)"
"$VITASDK/bin/arm-vita-eabi-ar" x "$new_prefix/lib/libavcodec.a" "$member" --output="$tmpdir" 2>/dev/null \
  || (cd "$tmpdir" && "$VITASDK/bin/arm-vita-eabi-ar" x "$new_prefix/lib/libavcodec.a" "$member")
if "$VITASDK/bin/arm-vita-eabi-readelf" -S "$tmpdir/$member" 2>/dev/null | grep -qi '\.gnu\.lto'; then
  echo "ERROR: rebuilt FFmpeg still has .gnu.lto — check configure flags" >&2
  exit 1
fi
echo "==> sample object $member has no .gnu.lto (good)"

if [[ -d "$old_prefix" ]]; then
  rm -rf "$backup_prefix"
  mv "$old_prefix" "$backup_prefix"
fi
mv "$new_prefix" "$old_prefix"
echo "==> installed at $old_prefix (previous → $backup_prefix)"

chmod +x "$repo_root/scripts/quick-vpk.sh"
touch "$repo_root/src/system/app_update.c"
echo "==> timing incremental VPK"
SSSPLAYER_RECONFIGURE=1 "$repo_root/scripts/quick-vpk.sh"
